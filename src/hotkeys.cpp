#include "hotkeys.h"

#include <functional>
#include <string>

#include <cameraunlock/input/key_binding_registration.h>

#include "hotkey_bindings.h"
#include "logging.h"

namespace kcd_ht
{
    namespace
    {
        using cameraunlock::TrackingMode;
        using cameraunlock::config::ConfigOwner;
        using cameraunlock::config::ConfigSaveResult;
        using cameraunlock::input::HotkeyPoller;
        using cameraunlock::input::RegisterKeyBindings;

        constexpr int kPollIntervalMs = 16;

        // Runs on the poller thread, after the new state is already live: a save
        // that fails leaves the session on it, and the owner's status sink has
        // put the reason in the log.
        void Persist(ConfigOwner<Config>& owner, const std::function<void(Config&)>& change)
        {
            const ConfigSaveResult saved = owner.Save(change);
            for (const std::string& line : saved.log) Log::Line("%s", line.c_str());
        }

        // Session only: EnableOnStartup decides the next start, so End never
        // touches the file.
        void ToggleTracking()
        {
            const bool enabled = !Runtime().trackingEnabled.load();
            Runtime().trackingEnabled.store(enabled);
            Log::Line("hotkey: tracking %s", enabled ? "ON" : "OFF");
        }

        // Three states, not a toggle: a toggle can only ever reach two of the
        // three modes the session has, and position-only is the one a player
        // wants when the tracker's rotation is fighting the mouse.
        void CycleTrackingMode(Session& session, ConfigOwner<Config>& owner)
        {
            const TrackingMode mode = session.CycleMode();
            const char* name = "unknown";
            switch (mode)
            {
            case TrackingMode::RotationAndPosition: name = "6DOF (rotation + lean)"; break;
            case TrackingMode::RotationOnly:        name = "rotation only"; break;
            case TrackingMode::PositionOnly:        name = "positional lean only"; break;
            }
            Log::Line("hotkey: tracking mode %s", name);

            const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(mode);
            Persist(owner, [channels](Config& c) {
                c.rotation_enabled = channels.rotation_enabled;
                c.position_enabled = channels.position_enabled;
            });
        }

        void ToggleYawMode(ConfigOwner<Config>& owner)
        {
            const bool worldSpace = !Runtime().worldSpaceYaw.load();
            Runtime().worldSpaceYaw.store(worldSpace);
            Log::Line("hotkey: yaw mode %s", worldSpace ? "world" : "local");
            Persist(owner, [worldSpace](Config& c) { c.world_space_yaw = worldSpace; });
        }
    }

    std::unique_ptr<HotkeyPoller> StartHotkeys(Session& session, const Config& config,
                                               ConfigOwner<Config>& owner)
    {
        auto poller = std::make_unique<HotkeyPoller>();
        const HotkeyBindings bindings = BindingsFor(config);
        RegisterKeyBindings(*poller, bindings.toggle, [] { ToggleTracking(); });
        RegisterKeyBindings(*poller, bindings.cycle_tracking_mode,
                            [&session, &owner] { CycleTrackingMode(session, owner); });
        RegisterKeyBindings(*poller, bindings.yaw_mode, [&owner] { ToggleYawMode(owner); });

        poller->Start(kPollIntervalMs);
        return poller;
    }
}
