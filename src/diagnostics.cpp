#include "diagnostics.h"

#include <atomic>
#include <cstdint>
#include <windows.h>
#include <tlhelp32.h>

#include <cameraunlock/math/angle_utils.h>

#include "builds/build_registry.h"
#include "cryengine_types.h"
#include "cursor_hook.h"
#include "game_state.h"
#include "logging.h"

namespace kcd_ht::diagnostics
{
    namespace
    {
        constexpr std::uint64_t kHeartbeatIntervalMs = 30000;
        constexpr float kRadToDeg = static_cast<float>(cameraunlock::math::kRadToDeg);

        const cameraunlock::UdpReceiver* g_receiver = nullptr;
        const Session* g_session = nullptr;

        std::atomic<std::uint64_t> g_viewUpdates{0};

        // The two frustum fields the reticle projection needs, captured off the
        // live camera every frame. Kept here rather than read at the point of use
        // so the heartbeat can report them even on frames where nothing was
        // injected: an aim offset computed from the wrong field of view is the one
        // reticle fault a log otherwise cannot distinguish from a bad hook.
        std::atomic<float> g_fovRadians{0.0f};
        std::atomic<float> g_projectionRatio{0.0f};

        // Whether the socket is up. A port held by another app is the one cause
        // of "no head tracking" the mod can name outright, and without this the
        // log reports only udpData=NO - which reads as a tracker problem and
        // sends the user off checking OpenTrack.
        const char* UdpPortState()
        {
            return g_receiver->IsRunning() ? "listening" : "WAITING (port held by another app)";
        }

        void LogHeartbeat()
        {
            static std::atomic<std::uint64_t> lastTick{0};
            const std::uint64_t now = GetTickCount64();
            const std::uint64_t updates = g_viewUpdates.load(std::memory_order_relaxed);
            if (updates != 1 && (now - lastTick.load(std::memory_order_relaxed)) < kHeartbeatIntervalMs)
                return;
            lastTick.store(now, std::memory_order_relaxed);

            float yaw = 0, pitch = 0, roll = 0;
            const bool haveData = g_receiver->GetRotation(yaw, pitch, roll);
            // Both leave their outputs alone when they have nothing, so the
            // zeros stand for "not reporting yet".
            float px = 0, py = 0, pz = 0;
            g_receiver->GetPosition(px, py, pz);
            float hudW = 0, hudH = 0;
            cursor::HudPixelSize(hudW, hudH);

            // rawPos is what the tracker actually sent, in metres, straight off
            // the wire. Since the mod maps the pose 1:1 it is also the answer to
            // "is this the mod or my tracker profile", which is nearly always the
            // question being asked.
            Log::Line("heartbeat viewUpdates=%llu enabled=%s udp=%s udpData=%s raw=(Y=%.2f P=%.2f R=%.2f) "
                      "rawPos=(%.3f %.3f %.3f) pos=%s yawMode=%s smoothing=%s gameplay=%s "
                      "vFov=%.1fdeg ratio=%.3f hud=%.0fx%.0f",
                      static_cast<unsigned long long>(updates),
                      Runtime().trackingEnabled.load() ? "ON" : "OFF",
                      UdpPortState(),
                      haveData ? "YES" : "NO",
                      static_cast<double>(yaw), static_cast<double>(pitch), static_cast<double>(roll),
                      static_cast<double>(px), static_cast<double>(py), static_cast<double>(pz),
                      g_session->IsPositionActive() ? "ON" : "OFF",
                      Runtime().worldSpaceYaw.load() ? "world" : "local",
                      g_session->IsRemoteConnection() ? "remote" : "local",
                      InActiveGameplay() ? "YES" : "NO",
                      static_cast<double>(g_fovRadians.load() * kRadToDeg),
                      static_cast<double>(g_projectionRatio.load()),
                      static_cast<double>(hudW), static_cast<double>(hudH));
        }

        // Said once. The reticle projection reads column 0 as RIGHT, and it can
        // only be right if the basis is right-handed: col0 = col1 x col2. A
        // determinant of -1 would mean the engine hands out a mirrored basis, in
        // which case the projection is horizontally inverted and the log says so
        // rather than leaving it to be guessed at from a drifting crosshair.
        void LogCameraBasisOnce(const Matrix34f& camera)
        {
            static std::atomic<bool> logged{false};
            if (logged.exchange(true)) return;

            const Vec3f r = camera.Column(0);
            const Vec3f f = camera.Column(1);
            const Vec3f u = camera.Column(2);
            const Vec3f fxu{ f.y * u.z - f.z * u.y,
                             f.z * u.x - f.x * u.z,
                             f.x * u.y - f.y * u.x };
            const float det = r.x * fxu.x + r.y * fxu.y + r.z * fxu.z;
            Log::Line("camera basis: right=(%.3f %.3f %.3f) forward=(%.3f %.3f %.3f) "
                      "up=(%.3f %.3f %.3f) det=%+.3f (%s)",
                      static_cast<double>(r.x), static_cast<double>(r.y), static_cast<double>(r.z),
                      static_cast<double>(f.x), static_cast<double>(f.y), static_cast<double>(f.z),
                      static_cast<double>(u.x), static_cast<double>(u.y), static_cast<double>(u.z),
                      static_cast<double>(det),
                      det > 0.0f ? "right-handed, column 0 is screen-right"
                                 : "MIRRORED, column 0 points screen-left");
        }

        // For the heartbeat only. The reticle takes its frustum from the camera
        // it is actually projecting through; this is the view's own copy, logged
        // so a wrong FOV offset is visible in a log rather than only as a reticle
        // that drifts with distance from centre.
        void CaptureFrustum(const void* view)
        {
            const auto& offsets = builds::Offsets();
            const auto* cameraBytes =
                reinterpret_cast<const std::uint8_t*>(view) + offsets.kCViewCameraOffset;
            LogCameraBasisOnce(*reinterpret_cast<const Matrix34f*>(cameraBytes));
            g_fovRadians.store(
                *reinterpret_cast<const float*>(cameraBytes + offsets.kCCameraFovOffset),
                std::memory_order_relaxed);
            g_projectionRatio.store(
                *reinterpret_cast<const float*>(cameraBytes + offsets.kCCameraProjectionRatioOffset),
                std::memory_order_relaxed);
        }
    }

    // ---- TEMPORARY culling probe ----------------------------------------
    //
    // The occlusion check culls an object by transforming its world AABB with a
    // 4x4 held on the cull object and testing the result against the coverage
    // buffer. Everything downstream of the render pass provably uses the
    // head-tracked camera, so if head-visible objects are being culled, that
    // 4x4 encodes some OTHER view. This says which.
    //
    // Addresses are the 2026-04-01 Steam build's and are deliberately NOT in the
    // build profile - the probe comes out again once it has answered.
    namespace probe
    {
        constexpr std::uint32_t kObjManagerGlobalRva = 0x02F97AD8;  // Cry3DEngineBase::m_pObjManager
        constexpr std::uint32_t kCullObjectOffset    = 0x00000480;  // its embedded cull object
        constexpr std::uint32_t kCullMatrixOffset    = 0x00000970;  // the 4x4 the AABB test uses
        constexpr std::uint32_t kCullParamsOffset    = 0x00001200;  // C3DEngine -> cull param block
        constexpr std::uint32_t kCullParamsCamera    = 0x00000150;  // its CCamera, written from the pass

        std::atomic<const std::uint8_t*> g_3dEngine{nullptr};

        Vec3f Forward(const Matrix34f& m) { return m.Column(1); }

        Vec3f Along(const Matrix34f& camera, const Vec3f& direction)
        {
            const Vec3f p = camera.Translation();
            return { p.x + direction.x * 100.0f,
                     p.y + direction.y * 100.0f,
                     p.z + direction.z * 100.0f };
        }

        // Row-vector convention, read off the AABB transform at RVA 0x0029870C:
        // it forms x*row0 + y*row1 + z*row2 + row3 with the rows 16 bytes apart.
        void Project(const float* m, const Vec3f& p, float& outX, float& outY)
        {
            const float x = p.x * m[0] + p.y * m[4] + p.z * m[8]  + m[12];
            const float y = p.x * m[1] + p.y * m[5] + p.z * m[9]  + m[13];
            const float w = p.x * m[3] + p.y * m[7] + p.z * m[11] + m[15];
            outX = w != 0.0f ? x / w : 0.0f;
            outY = w != 0.0f ? y / w : 0.0f;
        }
    }

    // ---- TEMPORARY write-watchpoint on the occlusion matrix ---------------
    //
    // Everything reachable statically turned out to be the head-tracked camera,
    // and the one GetViewCamera call site that inverts a camera rotation was the
    // wrong one. So stop looking for the writer and let the CPU report it: a
    // debug-register write watch on the first eight bytes of the matrix names
    // the instruction that fills it.
    namespace watch
    {
        std::uintptr_t g_textBegin = 0;
        std::uintptr_t g_textEnd = 0;
        std::uintptr_t g_address = 0;
        std::atomic<int> g_left{4};
        std::atomic<bool> g_armed{false};
        void* g_handler = nullptr;

        void Disarm(CONTEXT& context)
        {
            context.Dr0 = 0;
            context.Dr7 = 0;
            g_armed.store(false, std::memory_order_relaxed);
        }

        LONG CALLBACK OnException(EXCEPTION_POINTERS* info)
        {
            if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
                return EXCEPTION_CONTINUE_SEARCH;
            CONTEXT& context = *info->ContextRecord;
            if ((context.Dr6 & 0x1) == 0) return EXCEPTION_CONTINUE_SEARCH;
            context.Dr6 = 0;

            // The writer is often a memcpy, so the instruction alone is not
            // enough - the return addresses on the stack are what name the
            // engine function actually responsible.
            char frames[256];
            int used = 0;
            frames[0] = 0;
            const auto* stack = reinterpret_cast<const std::uintptr_t*>(context.Rsp);
            for (int i = 0, found = 0; i < 96 && found < 6; ++i)
            {
                const std::uintptr_t value = stack[i];
                if (value < g_textBegin || value >= g_textEnd) continue;
                ++found;
                const int wrote = wsprintfA(frames + used, " %08X",
                                            static_cast<unsigned>(value - g_textBegin));
                if (wrote <= 0) break;
                used += wrote;
            }

            Log::Line("cullwatch WRITE from RVA %08X  callers:%s",
                      static_cast<unsigned>(context.Rip - g_textBegin), frames);

            if (g_left.fetch_sub(1, std::memory_order_relaxed) <= 1)
                Disarm(context);
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        // Debug registers are per thread, and the matrix may be filled on the
        // render thread or on a job thread, so every thread gets the watch. A
        // thread cannot suspend itself to set its own, which is why this runs on
        // a helper thread of its own: every GAME thread is then somebody else.
        DWORD WINAPI ArmEveryThread(LPVOID)
        {
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snapshot == INVALID_HANDLE_VALUE) return 0;
            THREADENTRY32 entry{};
            entry.dwSize = sizeof(entry);
            const DWORD self = GetCurrentThreadId();
            const DWORD process = GetCurrentProcessId();
            int armed = 0;
            if (Thread32First(snapshot, &entry))
            {
                do
                {
                    if (entry.th32OwnerProcessID != process) continue;
                    if (entry.th32ThreadID == self) continue;
                    HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT
                                                   | THREAD_SUSPEND_RESUME,
                                               FALSE, entry.th32ThreadID);
                    if (thread == nullptr) continue;
                    if (SuspendThread(thread) != static_cast<DWORD>(-1))
                    {
                        CONTEXT context{};
                        context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                        if (GetThreadContext(thread, &context))
                        {
                            context.Dr0 = g_address;
                            // DR0 enabled locally, break on write, eight bytes.
                            context.Dr7 = (context.Dr7 & ~0x000F0003ull) | 0x000D0001ull;
                            if (SetThreadContext(thread, &context)) ++armed;
                        }
                        ResumeThread(thread);
                    }
                    CloseHandle(thread);
                } while (Thread32Next(snapshot, &entry));
            }
            CloseHandle(snapshot);
            Log::Line("cullwatch armed on %d threads, watching the occlusion matrix at %p.",
                      armed, reinterpret_cast<void*>(g_address));
            return 0;
        }
    }

    void WatchCullMatrix(std::uintptr_t moduleBase)
    {
        if (watch::g_armed.exchange(true)) return;

        const auto* objManager = *reinterpret_cast<const std::uint8_t* const*>(
            moduleBase + probe::kObjManagerGlobalRva);
        if (objManager == nullptr) { watch::g_armed.store(false); return; }

        const auto* headers = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
            moduleBase + headers->e_lfanew);
        watch::g_textBegin = moduleBase;
        watch::g_textEnd = moduleBase + nt->OptionalHeader.SizeOfImage;
        watch::g_address = reinterpret_cast<std::uintptr_t>(
            objManager + probe::kCullObjectOffset + probe::kCullMatrixOffset);
        watch::g_handler = AddVectoredExceptionHandler(1, &watch::OnException);
        if (HANDLE arming = CreateThread(nullptr, 0, &watch::ArmEveryThread, nullptr, 0, nullptr))
            CloseHandle(arming);
    }

    void NoteSystemRender(const void* system)
    {
        if (system == nullptr) return;
        const auto* env = *reinterpret_cast<const std::uint8_t* const*>(
            reinterpret_cast<const std::uint8_t*>(system) + 0x20);
        if (env == nullptr) return;
        probe::g_3dEngine.store(*reinterpret_cast<const std::uint8_t* const*>(env + 8),
                                std::memory_order_relaxed);
    }

    void ProbeCulling(std::uintptr_t moduleBase, const Matrix34f& clean, const Matrix34f& tracked)
    {
        static std::atomic<int> left{6};
        static std::atomic<std::uint64_t> lastTick{0};
        const std::uint64_t now = GetTickCount64();
        if (left.load(std::memory_order_relaxed) <= 0) return;
        if (now - lastTick.load(std::memory_order_relaxed) < 2000) return;
        lastTick.store(now, std::memory_order_relaxed);

        const auto* objManager = *reinterpret_cast<const std::uint8_t* const*>(
            moduleBase + probe::kObjManagerGlobalRva);
        if (objManager == nullptr) return;
        const auto* matrix = reinterpret_cast<const float*>(
            objManager + probe::kCullObjectOffset + probe::kCullMatrixOffset);

        const Vec3f cleanForward = probe::Forward(clean);
        const Vec3f trackedForward = probe::Forward(tracked);
        float cleanX = 0, cleanY = 0, trackedX = 0, trackedY = 0;
        probe::Project(matrix, probe::Along(clean, cleanForward), cleanX, cleanY);
        probe::Project(matrix, probe::Along(clean, trackedForward), trackedX, trackedY);

        // The camera the pass handed the cull param block. If this is already the
        // clean one the mismatch is upstream of the coverage buffer entirely.
        Vec3f paramsForward{};
        bool haveParams = false;
        if (const auto* engine = probe::g_3dEngine.load(std::memory_order_relaxed))
        {
            if (const auto* params = *reinterpret_cast<const std::uint8_t* const*>(
                    engine + probe::kCullParamsOffset))
            {
                paramsForward = probe::Forward(
                    *reinterpret_cast<const Matrix34f*>(params + probe::kCullParamsCamera));
                haveParams = true;
            }
        }

        if (left.fetch_sub(1, std::memory_order_relaxed) == 1)
            WatchCullMatrix(moduleBase);
        Log::Line("cullprobe cleanFwd=(%.4f %.4f %.4f) trackedFwd=(%.4f %.4f %.4f) "
                  "paramsFwd=%s(%.4f %.4f %.4f) | cullMatrix projects cleanAim to (%.1f %.1f), "
                  "trackedAim to (%.1f %.1f)",
                  static_cast<double>(cleanForward.x), static_cast<double>(cleanForward.y),
                  static_cast<double>(cleanForward.z),
                  static_cast<double>(trackedForward.x), static_cast<double>(trackedForward.y),
                  static_cast<double>(trackedForward.z),
                  haveParams ? "" : "UNAVAILABLE",
                  static_cast<double>(paramsForward.x), static_cast<double>(paramsForward.y),
                  static_cast<double>(paramsForward.z),
                  static_cast<double>(cleanX), static_cast<double>(cleanY),
                  static_cast<double>(trackedX), static_cast<double>(trackedY));
    }

    void Bind(const cameraunlock::UdpReceiver& receiver, const Session& session)
    {
        g_receiver = &receiver;
        g_session = &session;
    }

    void NoteActiveViewUpdate(const void* view)
    {
        g_viewUpdates.fetch_add(1, std::memory_order_relaxed);
        CaptureFrustum(view);
        LogHeartbeat();
    }
}
