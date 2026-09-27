#pragma once

#include <string>
#include <vector>

// The published build's config reader and startup, as the differential test sees
// them. The oracle library compiles published/ with its namespaces renamed, so
// this header names nothing from it: every type here is plain.

namespace kcd_config_oracle
{
    // One registered hotkey: a virtual-key code and the modifiers its guard
    // requires, as cameraunlock::input::KeyModifiers numbers them (Ctrl 1,
    // Shift 2).
    struct Binding
    {
        unsigned modifiers = 0;
        int vk = 0;
    };

    struct Result
    {
        // kcd_ht::Config as the published LoadConfig left it.
        int udp_port = 0;
        bool enable_on_startup = false;
        bool world_space_yaw = false;
        int toggle_key = 0;
        int position_key = 0;
        int yaw_mode_key = 0;
        float local_smoothing = 0.0f;
        float remote_smoothing = 0.0f;
        float max_extrapolation_fraction = 0.0f;
        float field_of_view = 0.0f;
        bool position_enabled = false;
        float limit_x = 0.0f;
        float limit_y = 0.0f;
        float limit_y_down = 0.0f;
        float limit_z = 0.0f;
        float limit_z_back = 0.0f;

        // What the published startup made of it.
        bool tracking_enabled = false;
        // cameraunlock::TrackingMode's numbers.
        int tracking_mode = 0;
        bool world_yaw = false;
        std::vector<Binding> toggle;
        std::vector<Binding> cycle_tracking_mode;
        std::vector<Binding> yaw_mode;
    };

    // The published bootstrap on <exeDir>: WriteDefaultConfigIfMissing, then
    // LoadConfig into a default Config, then the startup state.
    Result Startup(const std::string& exeDir);
}
