#pragma once

#include <string>

#include <cameraunlock/config/config_owner.h>
#include <cameraunlock/config/config_table.h>
#include <cameraunlock/config/defaults_file.h>
#include <cameraunlock/config/legacy_import.h>
#include <cameraunlock/data/position_settings.h>
#include <cameraunlock/math/smoothing_utils.h>
#include <cameraunlock/tracking/tracking_mode.h>

namespace kcd_ht
{
    // CameraUnlock.ini beside KingdomCome.exe, in cameraunlock-core's canonical
    // format. ConfigOwner is its one reader and writer; ConfigTableFor() lists
    // its rows.
    struct Config
    {
        int udp_port = 4242;
        bool enable_on_startup = true;

        // true = yaw about the world up-axis (horizon-locked); false = yaw about
        // the camera's own up-axis, which leans on pitched turns.
        bool world_space_yaw = true;

        // The tracking mode at startup, as the pair the mode hotkey saves.
        bool rotation_enabled = true;
        bool position_enabled = true;

        // Two smoothing parameters, picked per connection from the packet source
        // address. Both cover rotation and position. There is no third knob and
        // no hidden floor.
        float local_smoothing = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
        float remote_smoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

        // How far past the newest sample the interpolators may continue the last
        // velocity, as a fraction of the estimated sample interval. 0 interpolates
        // only between known samples.
        float max_extrapolation_fraction = 0.5f;

        // Vertical field of view in degrees, the same number Kingdom Come's own
        // FOV slider carries. 0 leaves the game's setting alone; anything else is
        // written to the engine's cl_fov once the console is up, which is how the
        // game's slider sets it too - so the frustum, the HUD and this mod's
        // reticle all move together.
        float field_of_view = 0.0f;

        float limit_x = cameraunlock::PositionSettings{}.limit_x;
        float limit_y = cameraunlock::PositionSettings{}.limit_y;
        float limit_y_down = cameraunlock::PositionSettings{}.limit_y_down;
        float limit_z = cameraunlock::PositionSettings{}.limit_z;
        float limit_z_back = cameraunlock::PositionSettings{}.limit_z_back;

        // Hotkey lists as the canonical format writes them.
        std::string toggle_key = "End, Ctrl+Shift+Y";
        std::string cycle_tracking_mode_key = "PageUp, Ctrl+Shift+G";
        std::string yaw_mode_key = "PageDown, Ctrl+Shift+H";
    };

    // There is deliberately NO per-axis sensitivity or inversion here. The tracker
    // owns pose shaping, and a backwards axis is a boundary-conversion bug to fix
    // in view_injection.cpp, not a knob to hand the player.

    // The rows of CameraUnlock.ini. The mode pair and WorldSpaceYaw are Writable:
    // their hotkeys save them. EnableOnStartup is not, so End never reaches the
    // file.
    cameraunlock::config::ConfigTable<Config> ConfigTableFor();

    // The display name the file's header names the game by, as data/games.json
    // spells it.
    extern const char* const kGameDisplayName;

    // The frozen reader in legacy_config/ as the owner's import: it reads the
    // HeadTracking.ini an older build read and maps it into Config.
    cameraunlock::config::LegacyImport<Config> LegacyImportFor();

    // The owner of CameraUnlock.ini in @p directory, a full path, with the
    // HeadTracking.ini every earlier build read beside it as the legacy file,
    // which it imports once while CameraUnlock.ini is absent and never writes.
    // @p defaults is where Defaults.ini is: the player's own in the mod, a
    // scratch file in a test.
    cameraunlock::config::ConfigOwnerOptions<Config> OwnerOptions(
        const std::wstring& directory, cameraunlock::config::DefaultsFile defaults);

    // The tracking mode the session starts in.
    cameraunlock::TrackingMode StartupMode(const Config& config);
}
