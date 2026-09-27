#include "config.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <windows.h>

#include <cameraunlock/config/config_concepts.g.h>
#include <cameraunlock/config/value_codecs.h>
#include <cameraunlock/input/key_bindings.h>

#include "exe_paths.h"
#include "legacy_config/legacy_config.h"
#include "logging.h"

namespace kcd_ht
{
    namespace
    {
        namespace config = cameraunlock::config;
        using config::DroppedValue;
        using config::ImportResult;
        using cameraunlock::input::KeyBinding;
        using cameraunlock::input::KeyModifiers;

        // Degrees of VERTICAL field of view. The game's own slider covers 60 to
        // 75; the band here is wider on both sides because overriding that slider
        // is the point, and it is what stops a typo from reaching the frustum.
        constexpr float kMinFieldOfView = 40.0f;
        constexpr float kMaxFieldOfView = 120.0f;

        // 0, the game's own field of view, or a chosen one from kMinFieldOfView to
        // kMaxFieldOfView. Between 0 and kMinFieldOfView is no field of view a
        // person could play at, so those values are refused like any value outside
        // the range, and the row keeps 0.
        class FieldOfViewCodec
        {
        public:
            using Value = float;

            config::CodecParseResult<float> Parse(std::string_view text) const
            {
                config::CodecParseResult<float> read = inner_.Parse(text);
                if (!read.ok() || (read.value != 0.0f && read.value < kMinFieldOfView))
                    return {0.0f, "0, or a number from 40 to 120"};
                return read;
            }

            std::string Render(float value) const { return inner_.Render(value); }

            bool Equal(float a, float b) const { return inner_.Equal(a, b); }

        private:
            config::FloatCodec inner_{0.0f, kMaxFieldOfView};
        };

        // The Ctrl+Shift letters every pre-canonical build bound beside each nav
        // key, hard-coded, which the import folds into each action's list.
        constexpr KeyModifiers kChord = KeyModifiers::kCtrl | KeyModifiers::kShift;
        constexpr int kVkY = 0x59;
        constexpr int kVkG = 0x47;
        constexpr int kVkH = 0x48;

        // A legacy hotkey code with the action's hard-coded chord after it. The
        // code goes through core's N1 and N3 normalisations, so a Ctrl, Shift or
        // Alt key on its own is left unbound and logged, and the chord stays.
        std::string LegacyHotkey(int code, const char* key, int chordLetter,
                                 std::vector<DroppedValue>& dropped)
        {
            std::string list = config::LegacyVirtualKeyToBindings(code, "Hotkeys", key, dropped);
            const std::string chord =
                cameraunlock::input::FormatKeyBindings({KeyBinding{kChord, chordLetter}});
            return list.empty() ? chord : list + ", " + chord;
        }

        // input names the legacy file, HeadTracking.ini, which the frozen reader
        // finds by its folder, as the published build did.
        ImportResult RunLegacyImport(const config::LegacyInput& input, Config& out)
        {
            legacy::Config read;
            legacy::LoadConfig(DirectoryOf(input.ansi_path), read);

            std::vector<DroppedValue> dropped;
            out.udp_port = read.udp_port;
            out.enable_on_startup = read.enable_on_startup;
            out.world_space_yaw = read.world_space_yaw;
            // [Position] Enabled only chose the startup mode: 6DOF, or rotation
            // only. The mode hotkey always cycled all three.
            out.rotation_enabled = true;
            out.position_enabled = read.position_enabled;
            out.local_smoothing = read.local_smoothing;
            out.remote_smoothing = read.remote_smoothing;
            out.max_extrapolation_fraction = read.max_extrapolation_fraction;
            out.field_of_view = read.field_of_view;
            out.limit_x = read.limit_x;
            out.limit_y = read.limit_y;
            out.limit_y_down = read.limit_y_down;
            out.limit_z = read.limit_z;
            out.limit_z_back = read.limit_z_back;
            out.toggle_key = LegacyHotkey(read.toggle_key, "ToggleKey", kVkY, dropped);
            out.cycle_tracking_mode_key = LegacyHotkey(read.position_key, "PositionKey", kVkG, dropped);
            out.yaw_mode_key = LegacyHotkey(read.yaw_mode_key, "YawModeKey", kVkH, dropped);

            // A setting the player never changed from what the published build
            // shipped follows Defaults.ini. Each hotkey's chord was hard-coded, so
            // its code alone says whether the player changed it.
            using C = config::schema::Concept;
            const legacy::Config shipped;
            config::LegacyFollowsDefaultsIni follows;
            follows.Setting(C::UdpPort, read.udp_port, shipped.udp_port);
            follows.Setting(C::EnableOnStartup, read.enable_on_startup, shipped.enable_on_startup);
            follows.Setting(C::WorldSpaceYaw, read.world_space_yaw, shipped.world_space_yaw);
            follows.TrackingMode(read.position_enabled, shipped.position_enabled);
            follows.Setting(C::LocalSmoothing, read.local_smoothing, shipped.local_smoothing);
            follows.Setting(C::RemoteSmoothing, read.remote_smoothing, shipped.remote_smoothing);
            follows.Setting(C::PositionLimitX, read.limit_x, shipped.limit_x);
            follows.Setting(C::PositionLimitY, read.limit_y, shipped.limit_y);
            follows.Setting(C::PositionLimitYDown, read.limit_y_down, shipped.limit_y_down);
            follows.Setting(C::PositionLimitZ, read.limit_z, shipped.limit_z);
            follows.Setting(C::PositionLimitZBack, read.limit_z_back, shipped.limit_z_back);
            follows.Setting(C::ToggleKey, read.toggle_key, shipped.toggle_key);
            follows.Setting(C::CycleTrackingModeKey, read.position_key, shipped.position_key);
            follows.Setting(C::YawModeKey, read.yaw_mode_key, shipped.yaw_mode_key);

            // The frozen reader finds the file the way the published build did,
            // with GetFileAttributesA on the ANSI path.
            if (GetFileAttributesA(input.ansi_path.c_str()) == INVALID_FILE_ATTRIBUTES)
                return ImportResult::Absent(std::move(dropped), {}, follows.Concepts());
            return ImportResult::Imported(std::move(dropped), {}, follows.Concepts());
        }
    }

    const char* const kGameDisplayName = "Kingdom Come: Deliverance";

    config::ConfigTable<Config> ConfigTableFor()
    {
        using C = config::schema::Concept;
        config::ConfigTable<Config> table{Config{}};
        table.Concept<C::UdpPort>(&Config::udp_port)
            .Concept<C::EnableOnStartup>(&Config::enable_on_startup)
            .Concept<C::WorldSpaceYaw>(&Config::world_space_yaw).Writable()
            .Concept<C::RotationEnabled>(&Config::rotation_enabled).Writable()
            .Concept<C::LocalSmoothing>(&Config::local_smoothing)
            .Concept<C::RemoteSmoothing>(&Config::remote_smoothing)
            .Local("Smoothing", "MaxExtrapolationFraction", &Config::max_extrapolation_fraction,
                   config::FloatCodec(0.0f, 1.0f),
                   "How far past the newest tracker sample the view may carry on moving,\n"
                   "as a fraction of the time between samples. 0 only moves between samples.")
            .Local("Camera", "FieldOfView", &Config::field_of_view, FieldOfViewCodec(),
                   "Vertical field of view in degrees, the same number the game's own Vertical\n"
                   "FOV setting carries, but not limited to its 60 to 75. 0 leaves whatever the\n"
                   "game is set to; 40 to 120 can be set. Saving the game's graphics settings\n"
                   "puts its own value back until the next launch.")
            .Concept<C::PositionEnabled>(&Config::position_enabled).Writable()
            .Concept<C::PositionLimitX>(&Config::limit_x)
            .Concept<C::PositionLimitY>(&Config::limit_y)
            .Concept<C::PositionLimitYDown>(&Config::limit_y_down)
            .Concept<C::PositionLimitZ>(&Config::limit_z)
            .Concept<C::PositionLimitZBack>(&Config::limit_z_back)
            .Concept<C::ToggleKey>(&Config::toggle_key)
            .Concept<C::CycleTrackingModeKey>(&Config::cycle_tracking_mode_key)
            .Concept<C::YawModeKey>(&Config::yaw_mode_key);
        return table;
    }

    config::LegacyImport<Config> LegacyImportFor()
    {
        config::LegacyImport<Config> import;
        import.run = &RunLegacyImport;
        import.keys = legacy::Keys();
        return import;
    }

    config::ConfigOwnerOptions<Config> OwnerOptions(const std::wstring& directory,
                                                    config::DefaultsFile defaults)
    {
        config::ConfigOwnerOptions<Config> options;
        options.path = directory + L"\\CameraUnlock.ini";
        options.table = ConfigTableFor();
        options.import = LegacyImportFor();
        options.legacy_path = directory + L"\\HeadTracking.ini";
        options.header.display_name = kGameDisplayName;
        options.defaults = std::move(defaults);
        // The mod draws no text of its own, so the player's message goes to the log.
        options.status_sink = [](const std::string& message) { Log::Line("%s", message.c_str()); };
        return options;
    }

    cameraunlock::TrackingMode StartupMode(const Config& config)
    {
        return cameraunlock::DecodeTrackingMode(config.rotation_enabled, config.position_enabled).value();
    }
}
