#pragma once

#include <string>
#include <vector>

#include <cameraunlock/config/legacy_import.h>

// The config reader as the pre-canonical builds ran it, frozen. It reads a
// HeadTracking.ini that an older build wrote and nothing else, and it never
// changes: a player can update from any older build, and their file has to be
// read exactly as that build read it. tests/config_differential holds it to the
// published build's reader.

namespace kcd_ht::legacy
{
    // kcd_ht::Config and its defaults as they stood when the reader was frozen,
    // spelled as literals so a later default in the mod or in core changes what a
    // new file holds, never what an old file without the key meant.
    struct Config
    {
        int udp_port = 4242;
        bool enable_on_startup = true;
        bool world_space_yaw = true;

        int toggle_key = 0x23;    // End
        int position_key = 0x21;  // Page Up
        int yaw_mode_key = 0x22;  // Page Down

        float local_smoothing = 0.0f;
        float remote_smoothing = 0.15f;
        float max_extrapolation_fraction = 0.5f;

        float field_of_view = 0.0f;

        bool position_enabled = true;
        float limit_x = 0.30f;
        float limit_y = 0.20f;
        float limit_y_down = 0.20f;
        float limit_z = 0.40f;
        float limit_z_back = 0.10f;
    };

    // Reads <exeDir>\HeadTracking.ini through GetPrivateProfileStringA into
    // @p out, starting from the values @p out holds. Writes nothing.
    void LoadConfig(const std::string& exeDir, Config& out);

    // Every section and key LoadConfig reads.
    const std::vector<cameraunlock::config::LegacyKey>& Keys();
}
