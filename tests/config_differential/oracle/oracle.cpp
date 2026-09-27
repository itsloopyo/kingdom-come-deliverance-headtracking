// Compiled into kcd_config_oracle only, with `cameraunlock` and `kcd_ht` renamed
// by the preprocessor (tests/config_differential/CMakeLists.txt), so the published
// reader and the core sources it built against sit beside the current ones in one
// test binary without a symbol in common.

#include "oracle.h"

#include "config.h"

namespace kcd_config_oracle
{
    namespace
    {
        // c91277b:src/hotkeys.cpp kVkY, kVkG, kVkH and the chord guard.
        constexpr unsigned kChord = 1u | 2u;
        constexpr int kVkY = 0x59;
        constexpr int kVkG = 0x47;
        constexpr int kVkH = 0x48;
    }

    Result Startup(const std::string& exeDir)
    {
        // c91277b:src/headtracking_mod.cpp BootstrapThread.
        kcd_ht::WriteDefaultConfigIfMissing(exeDir);
        kcd_ht::Config config;
        kcd_ht::LoadConfig(exeDir, config);

        Result r;
        r.udp_port = config.udp_port;
        r.enable_on_startup = config.enable_on_startup;
        r.world_space_yaw = config.world_space_yaw;
        r.toggle_key = config.toggle_key;
        r.position_key = config.position_key;
        r.yaw_mode_key = config.yaw_mode_key;
        r.local_smoothing = config.local_smoothing;
        r.remote_smoothing = config.remote_smoothing;
        r.max_extrapolation_fraction = config.max_extrapolation_fraction;
        r.field_of_view = config.field_of_view;
        r.position_enabled = config.position_enabled;
        r.limit_x = config.limit_x;
        r.limit_y = config.limit_y;
        r.limit_y_down = config.limit_y_down;
        r.limit_z = config.limit_z;
        r.limit_z_back = config.limit_z_back;

        // c91277b:src/headtracking_mod.cpp StartTracking: the session mode from
        // position_enabled (0 RotationAndPosition, 1 RotationOnly), the master
        // toggle and the yaw mode straight from the file.
        r.tracking_enabled = config.enable_on_startup;
        r.tracking_mode = config.position_enabled ? 0 : 1;
        r.world_yaw = config.world_space_yaw;

        // c91277b:src/hotkeys.cpp StartHotkeys: each action's nav key, unguarded
        // but for the chord, then its Ctrl+Shift letter.
        r.toggle = {Binding{0, config.toggle_key}, Binding{kChord, kVkY}};
        r.cycle_tracking_mode = {Binding{0, config.position_key}, Binding{kChord, kVkG}};
        r.yaw_mode = {Binding{0, config.yaw_mode_key}, Binding{kChord, kVkH}};
        return r;
    }
}
