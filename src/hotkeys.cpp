#include "hotkeys.h"

#include <cameraunlock/input/chord_hotkeys.h>

#include "logging.h"

namespace kcd_ht
{
    namespace
    {
        using cameraunlock::TrackingMode;
        using cameraunlock::input::ChordGuarded;
        using cameraunlock::input::NavGuarded;

        // Ctrl+Shift+<letter> from the T/Y/U/G/H/J block, in the fixed order that
        // puts the same action on the same chord in every mod.
        // Ctrl+Shift+T is left free: it was the recenter chord before mods
        // stopped keeping a centre, so reusing it would fire on muscle memory.
        constexpr int kVkY = 0x59;
        constexpr int kVkG = 0x47;
        constexpr int kVkH = 0x48;

        constexpr int kPollIntervalMs = 16;

        void ToggleTracking()
        {
            const bool enabled = !Runtime().trackingEnabled.load();
            Runtime().trackingEnabled.store(enabled);
            Log::Line("hotkey: tracking %s", enabled ? "ON" : "OFF");
        }

        // Three states, not a toggle: a toggle can only ever reach two of the
        // three modes the session has, and position-only is the one a player
        // wants when the tracker's rotation is fighting the mouse.
        void CycleTrackingMode(Session& session)
        {
            const char* name = "unknown";
            switch (session.CycleMode())
            {
            case TrackingMode::RotationAndPosition: name = "6DOF (rotation + lean)"; break;
            case TrackingMode::RotationOnly:        name = "rotation only"; break;
            case TrackingMode::PositionOnly:        name = "positional lean only"; break;
            }
            Log::Line("hotkey: tracking mode %s", name);
        }

        void ToggleYawMode()
        {
            const bool worldSpace = !Runtime().worldSpaceYaw.load();
            Runtime().worldSpaceYaw.store(worldSpace);
            Log::Line("hotkey: yaw mode %s", worldSpace ? "world" : "local");
        }
    }

    std::unique_ptr<cameraunlock::input::HotkeyPoller> StartHotkeys(Session& session,
                                                                    const Config& config)
    {
        auto poller = std::make_unique<cameraunlock::input::HotkeyPoller>();

        // Nav-cluster defaults. Suppressed while Ctrl+Shift is held so the chord
        // path is the sole trigger for a Ctrl+Shift+<nav> press.
        poller->AddHotkey(config.toggle_key, NavGuarded([] { ToggleTracking(); }));
        poller->AddHotkey(config.position_key,
                          NavGuarded([&session] { CycleTrackingMode(session); }));
        poller->AddHotkey(config.yaw_mode_key, NavGuarded([] { ToggleYawMode(); }));

        poller->AddHotkey(kVkY, ChordGuarded([] { ToggleTracking(); }));
        poller->AddHotkey(kVkG, ChordGuarded([&session] { CycleTrackingMode(session); }));
        poller->AddHotkey(kVkH, ChordGuarded([] { ToggleYawMode(); }));

        poller->Start(kPollIntervalMs);
        return poller;
    }
}
