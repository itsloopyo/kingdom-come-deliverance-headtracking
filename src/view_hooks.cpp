#include "view_hooks.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <windows.h>

#include <cameraunlock/hooks/hook_manager.h>
#include <cameraunlock/time/frame_clock.h>

#include "aim_projection.h"
#include "builds/build_registry.h"
#include "cryengine_types.h"
#include "cursor_hook.h"
#include "diagnostics.h"
#include "fov_override.h"
#include "game_state.h"
#include "hook_install.h"
#include "logging.h"
#include "pose_channel.h"
#include "view_injection.h"

namespace kcd_ht
{
    namespace
    {
        namespace hooks = cameraunlock::hooks;
        using cameraunlock::time::FrameClock;

        using CViewUpdate_t = void(__fastcall*)(void* self, float frameTime, bool isActive);
        using PrepareCullBuffer_t = void(__fastcall*)(void* cullBuffer, void* camera);
        using UpdateFrustum_t = void(__fastcall*)(void* camera);
        using PassInfoFromCamera_t = void*(__fastcall*)(void* passInfo, void* camera, int flags);
        using SystemRender_t = void(__fastcall*)(void* self);

        CViewUpdate_t g_origCViewUpdate = nullptr;
        PrepareCullBuffer_t g_origPrepareCullBuffer = nullptr;
        PassInfoFromCamera_t g_origPassInfo = nullptr;
        SystemRender_t g_origSystemRender = nullptr;
        UpdateFrustum_t g_updateFrustum = nullptr;

        // What the detours need from the bootstrap. Written before a single hook
        // is enabled and never again, so the detours read it without
        // synchronisation.
        std::uintptr_t g_moduleBase = 0;
        Session* g_session = nullptr;
        float g_fieldOfViewDegrees = 0.0f;

        // Which camera is the player's view, learned from CSystem::Render rather
        // than assumed: the pass-info builder runs for shadow and reflection
        // cameras too.
        std::atomic<void*> g_systemViewCamera{nullptr};

        // The cameras handed to the renderer. A ring rather than one buffer:
        // several passes can be built per frame, possibly from different threads,
        // and the pass info keeps its own copy only if CryEngine's per-thread
        // camera pool behaves as read. A ring costs 8 KB and removes the question.
        constexpr int kCameraSlots = 8;
        constexpr std::size_t kCameraSlotSize = 0x400;
        alignas(16) std::uint8_t g_cameraCopies[kCameraSlots][kCameraSlotSize];
        std::atomic<unsigned> g_cameraSlot{0};

        // The engine's asynchronous occlusion check does NOT test against the
        // camera the frame is drawn from. It builds its own screen-space matrix
        // in the cull-buffer prep below and tests every object's bounding box
        // against the coverage buffer through that. The camera it is handed is
        // the game's own - the one this mod deliberately never writes - so once
        // the head turns far enough, objects that ARE on screen project outside
        // that matrix's view, come back occluded, and vanish. Terrain and near
        // foliage survive because they are not tested this way, and very distant
        // objects survive because e_OcclusionCullingViewDistRatio skips the test
        // for them - which is exactly the pattern this showed up as.
        //
        // Measured, not inferred: a write watchpoint on the matrix named this
        // function, and before the fix the matrix projected the CLEAN aim to the
        // exact centre of the 256x128 coverage buffer on every sample.
        //
        // Both of its call sites exist only to prepare occlusion culling, so
        // giving it the tracked camera moves nothing else. Aim, raycasts, weapon
        // direction and interaction all read the view camera through other paths
        // and still see the game's own rotation.
        constexpr std::uint64_t kTrackedCameraFreshnessMs = 250;

        alignas(16) std::uint8_t g_trackedViewCamera[kCameraSlotSize];
        std::atomic<std::uint64_t> g_trackedViewCameraStamp{0};

        // Stale means the render hook has stopped injecting - tracking toggled
        // off, a menu, a lost tracker. The game's own camera is right then, and
        // handing over a frozen one would cull against a view nobody is looking
        // through.
        void* TrackedViewCameraOrNull()
        {
            const std::uint64_t stamp = g_trackedViewCameraStamp.load(std::memory_order_acquire);
            if (stamp == 0) return nullptr;
            if (GetTickCount64() - stamp >= kTrackedCameraFreshnessMs) return nullptr;
            return g_trackedViewCamera;
        }

        void __fastcall PrepareCullBuffer_Detour(void* cullBuffer, void* camera)
        {
            if (void* tracked = TrackedViewCameraOrNull()) camera = tracked;
            g_origPrepareCullBuffer(cullBuffer, camera);
        }

        PoseChannel g_poses;

        // Ticked only by the active view's update, so the session sees exactly one
        // dt per rendered frame.
        FrameClock g_frameClock;

        // The console is created long before the first view update, but the
        // game applies its own video settings during startup, so the override
        // goes in after that rather than racing it - and only once, so the FOV
        // changes the game makes for itself (reading a book narrows it to 50)
        // still work, and are still restored to this value afterwards.
        void ApplyFieldOfViewOverride()
        {
            if (g_fieldOfViewDegrees <= 0.0f) return;
            static std::atomic<bool> applied{false};
            if (applied.load(std::memory_order_relaxed)) return;
            if (fov::Apply(g_moduleBase, g_fieldOfViewDegrees))
                applied.store(true, std::memory_order_relaxed);
        }

        std::uint8_t* NextCameraCopy()
        {
            const unsigned slot = g_cameraSlot.fetch_add(1, std::memory_order_relaxed);
            return g_cameraCopies[slot % kCameraSlots];
        }

        // Decides the frame's pose and writes NOTHING. The camera the engine
        // has just composed is what the game reads for its own purposes, through
        // ISystem::GetViewCamera, so leaving it alone here is most of what keeps
        // look and aim decoupled. The pose is applied to a copy in the render
        // hook below.
        void __fastcall CViewUpdate_Detour(void* self, float frameTime, bool isActive)
        {
            g_origCViewUpdate(self, frameTime, isActive);

            // Only the ACTIVE view decides the frame's pose. The engine walks
            // every view here, so clearing the pose for each one would leave it
            // cleared whenever an inactive view happens to come last in the walk
            // - which is most frames, and which showed up as the head pose never
            // reaching the render at all.
            if (!isActive || self == nullptr) return;
            g_poses.Invalidate();

            ApplyFieldOfViewOverride();
            diagnostics::NoteActiveViewUpdate(self);

            if (!Runtime().trackingEnabled.load()) return;

            const float dt = g_frameClock.Tick();
            if (!g_session->Update(dt)) return;

            // Gated AFTER the session update so the interpolator keeps being fed
            // through a menu: the pose is current the frame play resumes, rather
            // than replaying a stale one until the next tracker sample lands.
            if (!InActiveGameplay()) return;

            HeadPose pose;
            if (!g_session->GetRotation(pose.yaw, pose.pitch, pose.roll)) return;
            const bool positionActive = g_session->GetPositionOffset(pose.x, pose.y, pose.z);

            // The pose starts as six doubles on a UDP socket bound to every
            // interface. Core rejects a datagram whose values do not narrow to
            // finite floats, but a FINITE pair near the float maximum still
            // overflows the interpolator's (to - from) to an infinity, and one
            // infinity turns the whole basis into NaN through sin/asin. A NaN
            // camera matrix goes to CCamera::UpdateFrustum and on to the
            // renderer, so it is checked here - the last step before the pose
            // reaches a camera - rather than trusted.
            if (!IsFinitePose(pose))
            {
                static std::atomic<bool> complained{false};
                if (!complained.exchange(true))
                    Log::Line("A tracker sample produced a pose that is not a number "
                              "(Y=%g P=%g R=%g pos=%g %g %g) - dropping it rather than handing "
                              "the renderer a NaN camera. Check what else is sending to the "
                              "tracking port; a working tracker cannot produce this.",
                              static_cast<double>(pose.yaw), static_cast<double>(pose.pitch),
                              static_cast<double>(pose.roll), static_cast<double>(pose.x),
                              static_cast<double>(pose.y), static_cast<double>(pose.z));
                return;
            }

            g_poses.Publish(pose, positionActive, GetTickCount64());
        }

        // Records which camera the frame is being drawn from. Nothing is
        // modified here - see the comment on kPassInfoFromCameraRva for why the
        // injection cannot live in this call.
        void __fastcall SystemRender_Detour(void* self)
        {
            if (self != nullptr)
            {
                diagnostics::NoteSystemRender(self);
                g_systemViewCamera.store(
                    reinterpret_cast<std::uint8_t*>(self) + builds::Offsets().kSystemViewCameraOffset,
                    std::memory_order_relaxed);
            }
            g_origSystemRender(self);
        }

        // The whole injection, and the only place the head pose touches the game.
        //
        // The pass info the frame is drawn through is built from the camera here
        // and takes its own copy, so the pose goes in for the length of this call
        // and comes straight back out. Every other reader - and the game reads
        // this camera from 141 call sites for interaction focus, aim and
        // raycasts - sees the rotation the game computed.
        void* __fastcall PassInfoFromCamera_Detour(void* passInfo, void* camera, int flags)
        {
            PoseSnapshot snapshot;
            if (camera == nullptr
                || camera != g_systemViewCamera.load(std::memory_order_relaxed)
                || !g_poses.TryTake(snapshot, GetTickCount64()))
            {
                return g_origPassInfo(passInfo, camera, flags);
            }

            // The pose is NOT consumed here. The pass-info builder has 14 call
            // sites and more than one of them can build from the player's camera
            // in a single frame; taking the pose on the first call would draw the
            // rest of that frame's passes from a different view. The next active
            // view update clears it instead, and its stamp keeps a view that has
            // stopped updating - a load screen, a video - from being drawn
            // through a stale one.
            const auto& offsets = builds::Offsets();
            std::uint8_t* copy = NextCameraCopy();
            std::memcpy(copy, camera, offsets.kCameraSize);

            auto* matrix = reinterpret_cast<Matrix34f*>(copy);
            const Matrix34f clean = *matrix;
            const Matrix34f tracked = ApplyHeadPose(clean, snapshot.pose,
                                                    Runtime().worldSpaceYaw.load(),
                                                    snapshot.positionActive);

            // Field of view and aspect come off THIS camera, not off the one the
            // view update happened to look at. They are two different CCameras
            // and they do disagree within a session - the intro cinematic renders
            // at ratio 1.333 while play is 1.778 - and a reticle projected
            // through the wrong frustum is wrong by that ratio.
            cursor::SubmitAim(ProjectAim(clean, tracked),
                              *reinterpret_cast<const float*>(copy + offsets.kCCameraFovOffset),
                              *reinterpret_cast<const float*>(
                                  copy + offsets.kCCameraProjectionRatioOffset));

            diagnostics::ProbeCulling(g_moduleBase, clean, tracked);

            *matrix = tracked;
            // Culling, shadow cascades and the render camera all derive from the
            // frustum planes, which the engine built from the pre-injection
            // matrix. Rebuild them or the world is culled against a view the
            // player is no longer looking through.
            g_updateFrustum(copy);

            // Published after the frustum rebuild so the occlusion check never
            // sees a camera whose planes still belong to the clean view.
            std::memcpy(g_trackedViewCamera, copy, offsets.kCameraSize);
            g_trackedViewCameraStamp.store(GetTickCount64(), std::memory_order_release);

            return g_origPassInfo(passInfo, copy, flags);
        }

        // One status per site. Create and enable are a pair - a hook that exists
        // but is not enabled does nothing - and the first failing step decides
        // the outcome, so the two share a line.
        bool InstallHook(void* target, void* detour, void** original,
                         const char* site, std::uint32_t rva)
        {
            const hooks::HookStatus status = CreateAndEnableHook(target, detour, original);
            if (status == hooks::HookStatus::Ok) return true;
            Log::Line("Could not hook %s at RVA 0x%08X: %s - staying dormant.",
                      site, rva, hooks::HookStatusToString(status));
            return false;
        }

        // The renderer is handed a copy of the camera, so a camera bigger than
        // the copy would be truncated into the render path. Checked before any
        // hook goes in: the answer is to leave the game unmodified, which is only
        // true while nothing is hooked yet.
        bool CameraFitsTheCopyBuffer()
        {
            const std::uint32_t size = builds::Offsets().kCameraSize;
            if (size <= kCameraSlotSize) return true;
            Log::Line("profile %s says a camera is %u bytes, which does not fit the %zu-byte "
                      "copy the render hook makes - staying dormant rather than truncating a "
                      "camera into the renderer.",
                      builds::ActiveProfile().Name, size, kCameraSlotSize);
            return false;
        }
    }

    bool InstallViewHooks(std::uintptr_t moduleBase, Session& session, float fieldOfViewDegrees)
    {
        g_moduleBase = moduleBase;
        g_session = &session;
        g_fieldOfViewDegrees = fieldOfViewDegrees;

        if (!CameraFitsTheCopyBuffer()) return false;

        const auto& offsets = builds::Offsets();
        g_updateFrustum = reinterpret_cast<UpdateFrustum_t>(
            moduleBase + offsets.kCCameraUpdateFrustumRva);

        if (!InstallHook(reinterpret_cast<void*>(moduleBase + offsets.kCViewUpdateRva),
                         reinterpret_cast<void*>(&CViewUpdate_Detour),
                         reinterpret_cast<void**>(&g_origCViewUpdate),
                         "CView::Update", offsets.kCViewUpdateRva))
            return false;

        Log::Line("CView::Update hooked at RVA 0x%08X (camera at CView+0x%X, viewParams at "
                  "CView+0x%X left untouched).",
                  offsets.kCViewUpdateRva, offsets.kCViewCameraOffset,
                  offsets.kCViewParamsOffset);

        if (!InstallHook(reinterpret_cast<void*>(moduleBase + offsets.kSystemRenderRva),
                         reinterpret_cast<void*>(&SystemRender_Detour),
                         reinterpret_cast<void**>(&g_origSystemRender),
                         "CSystem::Render", offsets.kSystemRenderRva))
            return false;

        if (!InstallHook(reinterpret_cast<void*>(moduleBase + offsets.kPassInfoFromCameraRva),
                         reinterpret_cast<void*>(&PassInfoFromCamera_Detour),
                         reinterpret_cast<void**>(&g_origPassInfo),
                         "the rendering pass info builder", offsets.kPassInfoFromCameraRva))
            return false;

        if (!InstallHook(reinterpret_cast<void*>(moduleBase + offsets.kPrepareCullBufferRva),
                         reinterpret_cast<void*>(&PrepareCullBuffer_Detour),
                         reinterpret_cast<void**>(&g_origPrepareCullBuffer),
                         "the occlusion cull-buffer prep", offsets.kPrepareCullBufferRva))
            return false;

        Log::Line("Occlusion culling hooked at RVA 0x%08X - the coverage buffer is now built "
                  "from the head-tracked view, so objects the player can see are no longer "
                  "culled against the view the body is facing.",
                  offsets.kPrepareCullBufferRva);

        Log::Line("Render pass hooked at RVA 0x%08X (view camera identified through "
                  "CSystem::Render at 0x%08X) - the head pose is applied for the draw and "
                  "taken straight back out.",
                  offsets.kPassInfoFromCameraRva, offsets.kSystemRenderRva);
        return true;
    }
}
