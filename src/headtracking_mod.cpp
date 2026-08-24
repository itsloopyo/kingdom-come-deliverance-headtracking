#include "headtracking_mod.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

#include <cameraunlock/diagnostics/crash_handler.h>
#include <cameraunlock/hooks/hook_manager.h>

#include "builds/build_registry.h"
#include "config.h"
#include "cursor_hook.h"
#include "diagnostics.h"
#include "exe_paths.h"
#include "hotkeys.h"
#include "logging.h"
#include "runtime_state.h"
#include "view_hooks.h"

// Bootstrap only: bring the log, the config, the tracker and the hooks up in the
// one order that works, then say in a single line what is live. The injection
// itself is in view_hooks.cpp and everything the log reports is in
// diagnostics.cpp.

namespace kcd_ht
{
    namespace
    {
        namespace hooks = cameraunlock::hooks;
        using cameraunlock::TrackingMode;

        // The engine and the whole game live in this one module; KingdomCome.exe
        // is a launcher stub with no camera code in it at all.
        constexpr wchar_t kGameModule[] = L"WHGame.dll";

        // The ASI loads while WHGame.dll's imports are being resolved, so the
        // module is mapped but its entry point has not run. Poll rather than
        // assume: a hook installed before the loader finished with the module
        // would be written over.
        constexpr int kModuleWaitMs = 20000;
        constexpr int kModulePollMs = 50;

        // The tracker owns pose shaping, so the mod consumes the
        // pose 1:1 and offers no sensitivity of its own.
        constexpr float kUnitSensitivity = 1.0f;

        // These outlive the bootstrap thread: the hooks read the session on every
        // frame, and the pollers stop the moment their owner goes away.
        std::unique_ptr<cameraunlock::UdpReceiver> g_receiver;
        std::unique_ptr<Session> g_session;
        std::unique_ptr<cameraunlock::input::HotkeyPoller> g_hotkeys;

        // Open truncates, so every launch starts from scratch, and it rotates
        // the outgoing log to HeadTracking.prev.log first. That second
        // generation is kept because the crash handler asks the player to send
        // this log and they relaunch the game before going to look for it. Two
        // bounded files, never an accumulating set.
        void OpenSessionLog()
        {
            Log::Open(ExeDirectory() + L"\\HeadTracking.log");
            Log::Line("=== Kingdom Come: Deliverance Head Tracking (CryEngine) ===");
        }

        std::string DescribeFieldOfView(float degrees)
        {
            if (degrees <= 0.0f) return "game's own";
            char text[32];
            std::snprintf(text, sizeof(text), "%.1fdeg", static_cast<double>(degrees));
            return text;
        }

        void LogConfig(const Config& config)
        {
            Log::Line("config: port=%d enabled=%s worldYaw=%s local=%.2f remote=%.2f pos=%s "
                      "limits=(x %.2f, y +%.2f/-%.2f, z %.2f fwd/%.2f back) fov=%s",
                      config.udp_port,
                      config.enable_on_startup ? "yes" : "no",
                      config.world_space_yaw ? "yes" : "no",
                      static_cast<double>(config.local_smoothing),
                      static_cast<double>(config.remote_smoothing),
                      config.position_enabled ? "on" : "off",
                      static_cast<double>(config.limit_x),
                      static_cast<double>(config.limit_y),
                      static_cast<double>(config.limit_y_down),
                      static_cast<double>(config.limit_z),
                      static_cast<double>(config.limit_z_back),
                      DescribeFieldOfView(config.field_of_view).c_str());
        }

        HMODULE WaitForGameModule()
        {
            for (int waited = 0; waited < kModuleWaitMs; waited += kModulePollMs)
            {
                if (HMODULE module = GetModuleHandleW(kGameModule)) return module;
                Sleep(kModulePollMs);
            }
            Log::Line("%ls never appeared after %d ms - staying dormant.",
                      kGameModule, kModuleWaitMs);
            return nullptr;
        }

        // Returns whether the port was free right away. False is not a failure:
        // the receiver's supervisor keeps retrying the bind on its own and goes
        // live the moment the port frees, so a player who launched this game with
        // the previous one still running gets tracking within a second of closing
        // it, without touching anything.
        bool StartTracking(const Config& config)
        {
            g_receiver = std::make_unique<cameraunlock::UdpReceiver>();
            g_receiver->SetLog([](const std::string& line) { Log::Line("%s", line.c_str()); });
            const bool bound = g_receiver->Start(static_cast<std::uint16_t>(config.udp_port));

            g_session = std::make_unique<Session>(*g_receiver);
            g_session->SetLocalSmoothing(config.local_smoothing);
            g_session->SetRemoteSmoothing(config.remote_smoothing);
            g_session->SetMaxExtrapolationFraction(config.max_extrapolation_fraction);
            g_session->SetPositionSettings(cameraunlock::PositionSettings(
                kUnitSensitivity, kUnitSensitivity, kUnitSensitivity,
                config.limit_x, config.limit_y, config.limit_y_down,
                config.limit_z, config.limit_z_back,
                config.local_smoothing, config.remote_smoothing));
            g_session->SetMode(config.position_enabled ? TrackingMode::RotationAndPosition
                                                       : TrackingMode::RotationOnly);

            Runtime().trackingEnabled.store(config.enable_on_startup);
            Runtime().worldSpaceYaw.store(config.world_space_yaw);
            return bound;
        }

        // Separate from the hook installs because BOTH of them need it, and the
        // order they run in has already changed once: when the cursor hook moved
        // ahead of the view hooks it started failing with "Not initialized",
        // which cost the crosshair compensation entirely and showed up only as
        // one line in the log.
        bool InitHookEngine()
        {
            const hooks::HookStatus status = hooks::HookManager::Instance().Initialize();
            if (status == hooks::HookStatus::Ok) return true;
            Log::Line("MinHook init failed: %s - staying dormant.",
                      hooks::HookStatusToString(status));
            return false;
        }

        std::string DescribeUdpPort(int port, bool bound)
        {
            const std::string number = std::to_string(port);
            if (bound) return "Listening for a tracker on UDP " + number + ".";
            return "UDP " + number + " is held by another app - another game left running, or "
                   "OpenTrack. Close it and tracking starts within a second; nothing else "
                   "to do.";
        }

        DWORD WINAPI BootstrapThread(LPVOID)
        {
            OpenSessionLog();
            cameraunlock::diagnostics::InstallCrashHandler();

            const std::string exeDir = ExeDirectoryNarrow();
            WriteDefaultConfigIfMissing(exeDir);
            Config config;
            LoadConfig(exeDir, config);
            LogConfig(config);

            HMODULE module = WaitForGameModule();
            if (!module) return 0;
            const auto moduleBase = reinterpret_cast<std::uintptr_t>(module);

            if (builds::SelectProfile(module) != builds::SelectResult::Matched) return 0;
            Log::Line("build profile %s active (WHGame.dll at 0x%p).",
                      builds::ActiveProfile().Name, module);

            // Before StartTracking, not after. A failure here leaves the mod
            // dormant for the session, and the receiver binds the OpenTrack port
            // the moment it starts - so binding first meant a dormant mod sat on
            // UDP 4242 until the game closed, which is this mod's own named
            // cause of "no head tracking" for whatever the player launches next.
            if (!InitHookEngine()) return 0;

            const bool bound = StartTracking(config);
            diagnostics::Bind(*g_receiver, *g_session);

            // Before the view hooks, not after: the heartbeat inside the very
            // first CView::Update asks the cursor hook for the HUD's pixel size,
            // and until Install has run it has no module base to read it through.
            // Not fatal if the hook itself fails - a mod that moves the view but
            // leaves the crosshair at screen centre is still worth having, and
            // cursor::Install says so in the log.
            const bool crosshair = cursor::Install(moduleBase);

            if (!InstallViewHooks(moduleBase, *g_session, config.field_of_view)) return 0;

            g_hotkeys = StartHotkeys(*g_session, config);

            // One line saying what is actually live. The crosshair hook once
            // failed to install for an hour of testing because its own error
            // line sat above three successful ones and nobody read up.
            Log::Line("init complete. view=on render=on crosshair=%s. End = toggle tracking, "
                      "Page Up = cycle tracking mode, Page Down = yaw mode (chords "
                      "Ctrl+Shift+Y/G/H). %s Centre in your tracker app - this mod keeps no "
                      "centre of its own.",
                      crosshair ? "on" : "OFF (crosshair stays at screen centre)",
                      DescribeUdpPort(config.udp_port, bound).c_str());
            return 0;
        }
    }

    void Initialize(HMODULE module)
    {
        // Pinned so nothing can FreeLibrary this mod. Detours cannot be taken
        // back safely: MinHook rewinds a thread sitting in a trampoline but not
        // one sitting in OUR detour body, so an unload frees the pages that
        // thread is about to return into. Pinning makes that unrepresentable
        // rather than merely unlikely, and it costs a module reference in a
        // process that never unloads its ASIs anyway.
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           reinterpret_cast<LPCWSTR>(module), &pinned);

        // Nothing joins the bootstrap thread, so the handle is closed here. The
        // thread keeps running; closing a thread handle only drops the reference
        // used to wait on it.
        if (HANDLE thread = CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr))
            CloseHandle(thread);
    }
}
