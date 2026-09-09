#include "cursor_hook.h"

#include <atomic>
#include <intrin.h>
#include <windows.h>

#include <cameraunlock/hooks/hook_manager.h>

#include "builds/build_registry.h"
#include "hook_install.h"
#include "logging.h"

namespace kcd_ht::cursor
{
    namespace
    {
        namespace hooks = cameraunlock::hooks;

        // (this, element, const float pos[2], int mode)
        using SetCursorPosition_t = void(__fastcall*)(void*, void*, const float*, int);
        using GetDimension_t = int(__fastcall*)(void*);

        SetCursorPosition_t g_orig = nullptr;
        std::uintptr_t g_moduleBase = 0;

        struct AimState
        {
            std::atomic<float> tanRight{0.0f};
            std::atomic<float> tanUp{0.0f};
            std::atomic<float> fovRadians{0.0f};
            std::atomic<float> projectionRatio{0.0f};
            std::atomic<bool> inFront{false};
            std::atomic<std::uint64_t> stampMs{0};
            std::atomic<float> headYaw{0.0f};
            std::atomic<float> headPitch{0.0f};
        };

        AimState g_aim;

        // Several frames even at 30 fps, so a stutter never blinks the crosshair
        // back to centre, and still short enough that it is under the game's own
        // control by the time a menu has finished opening.
        constexpr std::uint64_t kFreshnessMs = 250;

        // What a back buffer can plausibly be, from the smallest windowed mode
        // the game will open at to well past an 8K display. A pinned vtable slot
        // on a pinned global is exactly the kind of thing a patch moves, and a
        // wrong slot returns something that is not a resolution at all.
        constexpr int kMinBackBufferWidth = 320;
        constexpr int kMinBackBufferHeight = 240;
        constexpr int kMaxBackBufferExtent = 16384;

        bool IsPlausibleResolution(int width, int height)
        {
            return width >= kMinBackBufferWidth && width <= kMaxBackBufferExtent
                && height >= kMinBackBufferHeight && height <= kMaxBackBufferExtent;
        }

        // The same reasoning one field over. The field of view and the projection
        // ratio are read out of the live CCamera through pinned offsets, and
        // ToScreen divides by tan(fov/2) - so a slot a patch has moved can leave a
        // float that is still positive but nowhere near a frustum, and a fov of
        // 1e-20 rad turns a one-degree head turn into a cursor thousands of
        // screens away. The band is far wider than any FOV slider reaches; it
        // exists to catch a wrong offset, not to second-guess a setting.
        constexpr float kMinFovRadians = 0.087f;  // 5 degrees
        constexpr float kMaxFovRadians = 3.05f;   // 175 degrees
        // Keeps a clamped crosshair inside the frame rather than exactly on its
        // border, where the HUD clips it.
        constexpr float kEdgeMarginPixels = 8.0f;

        constexpr float kMinProjectionRatio = 0.1f;
        constexpr float kMaxProjectionRatio = 10.0f;

        bool IsPlausibleFrustum(float fovRadians, float projectionRatio)
        {
            return fovRadians >= kMinFovRadians && fovRadians <= kMaxFovRadians
                && projectionRatio >= kMinProjectionRatio
                && projectionRatio <= kMaxProjectionRatio;
        }

        bool CallSiteIsCursor(std::uintptr_t returnRva)
        {
            for (std::uint32_t site : builds::Offsets().kCursorReturnRvas)
            {
                if (site == 0) break;
                if (returnRva == site) return true;
            }
            return false;
        }

        bool ScreenSize(float& width, float& height)
        {
            // Install() has not run yet on the first few frames, and the
            // heartbeat asks for this size before it does. Reading a global
            // through a base of zero is an access violation, not a missing
            // feature, so it is checked here rather than assumed away.
            if (g_moduleBase == 0) return false;

            const auto& offsets = builds::Offsets();
            auto* renderer = *reinterpret_cast<void**>(g_moduleBase + offsets.kRendererGlobalRva);
            if (renderer == nullptr) return false;

            auto** vtable = *reinterpret_cast<void***>(renderer);
            const int w = reinterpret_cast<GetDimension_t>(
                vtable[offsets.kRendererWidthSlot / sizeof(void*)])(renderer);
            const int h = reinterpret_cast<GetDimension_t>(
                vtable[offsets.kRendererHeightSlot / sizeof(void*)])(renderer);

            // An implausible size means the profile is wrong, so say so once and
            // stop touching the cursor rather than flinging it off screen.
            if (!IsPlausibleResolution(w, h))
            {
                static std::atomic<bool> complained{false};
                if (!complained.exchange(true))
                    Log::Line("The renderer reported a %dx%d back buffer, which is not a "
                              "resolution - leaving the game's crosshair alone.", w, h);
                return false;
            }

            static std::atomic<bool> logged{false};
            if (!logged.exchange(true))
                Log::Line("HUD cursor: the HUD positions cursors in a %dx%d pixel space.", w, h);

            width = static_cast<float>(w);
            height = static_cast<float>(h);
            return true;
        }

        // Where the crosshair was last put. Held so it can stay at the edge it
        // was last pushed to when the aim point leaves the view: snapping it back
        // to screen centre would be the one position that reads as "your shot
        // goes here".
        struct HeldOffset
        {
            std::atomic<float> dx{0.0f};
            std::atomic<float> dy{0.0f};
            std::atomic<bool> valid{false};
        };

        HeldOffset g_held;

        bool IsAimFresh()
        {
            const std::uint64_t stamp = g_aim.stampMs.load(std::memory_order_relaxed);
            return stamp != 0 && (GetTickCount64() - stamp) < kFreshnessMs;
        }

        bool ReadHeldOffset(float& dx, float& dy)
        {
            if (!g_held.valid.load(std::memory_order_relaxed)) return false;
            dx = g_held.dx.load(std::memory_order_relaxed);
            dy = g_held.dy.load(std::memory_order_relaxed);
            return true;
        }

        bool FrustumIsUsable(float fovRadians, float projectionRatio)
        {
            if (IsPlausibleFrustum(fovRadians, projectionRatio)) return true;

            // Both read zero until the first active view update, which is the
            // normal startup state rather than a fault, so only a value that is
            // present and wrong is worth a line.
            static std::atomic<bool> complained{false};
            if (fovRadians != 0.0f && !complained.exchange(true))
                Log::Line("The camera reported a %.4f rad field of view at ratio %.3f, "
                          "which is not a frustum - leaving the game's crosshair alone.",
                          static_cast<double>(fovRadians), static_cast<double>(projectionRatio));
            return false;
        }

        bool AimOffset(float& dx, float& dy)
        {
            // Freshness FIRST. Held at the edge is only correct while aim is
            // still arriving; when it stops - a menu, an alt-tab, a tracker
            // unplugged - the crosshair has to go back to wherever the game
            // wants it, which is the liveness contract in the header.
            if (!IsAimFresh()) return false;

            if (!g_aim.inFront.load(std::memory_order_relaxed))
                return ReadHeldOffset(dx, dy);

            const float fov = g_aim.fovRadians.load(std::memory_order_relaxed);

            float width = 0.0f, height = 0.0f;
            if (!ScreenSize(width, height)) return false;

            // The back buffer IS the aspect the frame is drawn at, so it cannot
            // disagree with the picture the way a camera field can.
            const float ratio = width / height;
            if (!FrustumIsUsable(fov, ratio)) return false;

            AimProjection aim;
            aim.tanRight = g_aim.tanRight.load(std::memory_order_relaxed);
            aim.tanUp = g_aim.tanUp.load(std::memory_order_relaxed);
            aim.inFront = true;

            const ScreenOffset offset =
                ClampedCentreOffset(aim, width, height, fov, ratio, kEdgeMarginPixels);
            dx = offset.dx;
            dy = offset.dy;

            g_held.dx.store(dx, std::memory_order_relaxed);
            g_held.dy.store(dy, std::memory_order_relaxed);
            g_held.valid.store(true, std::memory_order_relaxed);
            return true;
        }

        // Which call site the game positions the crosshair through, what it
        // asked for, and what we did to it. Rate-limited and bounded the way
        // the culling probe is, because the question it answers - does the
        // crosshair move the right way, by the right amount, at the site the
        // player is actually looking at - cannot be answered from a screenshot
        // without knowing all four numbers at once.
        //
        // Read it as: a head turn one way should put `dx` the OTHER way, because
        // the aim point the player is looking at swings across the frame against
        // the head. `posIn` says whether the game handed us screen centre or an
        // already-aimed point, which decides whether adding an offset is exact
        // or a first-order shift.
        void LogReticleSample(std::uintptr_t returnRva, int mode, const float* pos,
                              float dx, float dy)
        {
            static std::atomic<int> left{24};
            static std::atomic<std::uint64_t> lastTick{0};
            if (left.load(std::memory_order_relaxed) <= 0) return;

            const std::uint64_t now = GetTickCount64();
            if (now - lastTick.load(std::memory_order_relaxed) < 400) return;
            lastTick.store(now, std::memory_order_relaxed);
            if (left.fetch_sub(1, std::memory_order_relaxed) <= 0) return;

            float width = 0.0f, height = 0.0f;
            ScreenSize(width, height);

            Log::Line("reticle site=0x%08X mode=%d posIn=(%.1f %.1f) centre=(%.1f %.1f) "
                      "dx=%+.1f dy=%+.1f tanRight=%+.4f tanUp=%+.4f head=(Y%+.2f P%+.2f) "
                      "vFov=%.1fdeg ratio=%.3f",
                      static_cast<unsigned>(returnRva), mode,
                      static_cast<double>(pos[0]), static_cast<double>(pos[1]),
                      static_cast<double>(width * 0.5f), static_cast<double>(height * 0.5f),
                      static_cast<double>(dx), static_cast<double>(dy),
                      static_cast<double>(g_aim.tanRight.load(std::memory_order_relaxed)),
                      static_cast<double>(g_aim.tanUp.load(std::memory_order_relaxed)),
                      static_cast<double>(g_aim.headYaw.load(std::memory_order_relaxed)),
                      static_cast<double>(g_aim.headPitch.load(std::memory_order_relaxed)),
                      static_cast<double>(g_aim.fovRadians.load(std::memory_order_relaxed)
                                          * 57.2957795f),
                      static_cast<double>(
                          g_aim.projectionRatio.load(std::memory_order_relaxed)));
        }

        void __fastcall SetCursorPosition_Detour(void* self, void* element, const float* pos, int mode)
        {
            const std::uintptr_t returnRva =
                reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - g_moduleBase;

            float dx = 0.0f, dy = 0.0f;
            if (pos != nullptr && CallSiteIsCursor(returnRva) && AimOffset(dx, dy))
            {
                static std::atomic<bool> announced{false};
                if (!announced.exchange(true))
                    Log::Line("HUD cursor: the crosshair is following your aim.");

                // For the site that hands in screen centre this is exact. For the
                // combat site, which hands in an already-aimed position, it is a
                // first-order shift of that point into the tracked view - right at
                // the centre and increasingly approximate towards the edges, which
                // is where that cursor never is.
                LogReticleSample(returnRva, mode, pos, dx, dy);

                const float moved[2] = { pos[0] + dx, pos[1] + dy };
                g_orig(self, element, moved, mode);
                return;
            }

            // A site we did NOT move, sampled the same way. If the crosshair the
            // player is watching comes through here, the offset above is landing
            // on something else and no amount of correcting its sign will show.
            if (pos != nullptr && IsAimFresh())
                LogReticleSample(returnRva, mode, pos, 0.0f, 0.0f);

            g_orig(self, element, pos, mode);
        }
    }

    bool Install(std::uintptr_t moduleBase)
    {
        g_moduleBase = moduleBase;

        const auto& offsets = builds::Offsets();
        void* target = reinterpret_cast<void*>(moduleBase + offsets.kSetCursorPositionRva);

        const hooks::HookStatus status = CreateAndEnableHook(
            target, reinterpret_cast<void*>(&SetCursorPosition_Detour),
            reinterpret_cast<void**>(&g_orig));
        if (status != hooks::HookStatus::Ok)
        {
            Log::Line("Could not hook the HUD cursor setter at RVA 0x%08X: %s - the game's "
                      "crosshair will stay at screen centre.",
                      offsets.kSetCursorPositionRva, hooks::HookStatusToString(status));
            return false;
        }

        Log::Line("HUD cursor hooked at RVA 0x%08X.", offsets.kSetCursorPositionRva);

        // Reports the pixel space if the renderer is already up; if it is not,
        // the same line comes out on the first frame that needs the size.
        float width = 0.0f, height = 0.0f;
        ScreenSize(width, height);
        return true;
    }

    bool HudPixelSize(float& width, float& height)
    {
        return ScreenSize(width, height);
    }

    void SubmitAim(const AimProjection& aim, float fovRadians,
                   float headYawDeg, float headPitchDeg)
    {
        g_aim.tanRight.store(aim.tanRight, std::memory_order_relaxed);
        g_aim.tanUp.store(aim.tanUp, std::memory_order_relaxed);
        g_aim.inFront.store(aim.inFront, std::memory_order_relaxed);
        g_aim.fovRadians.store(fovRadians, std::memory_order_relaxed);
        g_aim.headYaw.store(headYawDeg, std::memory_order_relaxed);
        g_aim.headPitch.store(headPitchDeg, std::memory_order_relaxed);
        g_aim.stampMs.store(GetTickCount64(), std::memory_order_relaxed);
    }
}
