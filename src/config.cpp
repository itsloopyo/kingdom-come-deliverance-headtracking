#include "config.h"

#include <cstdio>
#include <string>
#include <windows.h>

#include "legacy_config/legacy_config.h"
#include "logging.h"

namespace kcd_ht
{
    namespace
    {
        constexpr char kIniName[] = "HeadTracking.ini";

        std::string IniPath(const std::string& exeDir)
        {
            return exeDir + "\\" + kIniName;
        }

        // Comments for bool keys go on their OWN line: the INI reader is built on
        // GetPrivateProfileStringA, which does not treat ';' as an inline comment
        // introducer, so "Enabled=true ; note" matches no known bool spelling and
        // silently falls back to the default.
        //
        // LimitYDown is formatted from cameraunlock::PositionSettings{}.limit_y_down
        // rather than written as a literal, so a change to core's default cannot
        // silently disagree with the file this mod ships.
        std::string BuildDefaultIni()
        {
            char limitYDown[16] = {};
            std::snprintf(limitYDown, sizeof(limitYDown), "%.2f",
                          static_cast<double>(cameraunlock::PositionSettings{}.limit_y_down));

            std::string ini(
            "[HeadTracking]\r\n"
            "UdpPort=4242\r\n"
            "; Start with head tracking already on.\r\n"
            "EnableOnStartup=true\r\n"
            "; Yaw about the world up-axis so the horizon stays level. Off yaws about\r\n"
            "; the camera's own up-axis, which leans the view on pitched turns.\r\n"
            "WorldSpaceYaw=true\r\n"
            "; Smoothing for a tracker running on this machine (loopback). 0 = none.\r\n"
            "LocalSmoothing=0.0\r\n"
            "; Smoothing for a tracker reaching this machine over the network. A tracker\r\n"
            "; sending to this PC's LAN address instead of 127.0.0.1 counts as remote -\r\n"
            "; the classifier sees a transport, not a machine.\r\n"
            "RemoteSmoothing=0.15\r\n"
            "MaxExtrapolationFraction=0.5\r\n"
            "\r\n"
            "[Camera]\r\n"
            "; Vertical field of view in degrees - the same number the game's own\r\n"
            "; Vertical FOV setting carries, but not limited to its 60-75 range. 0 leaves\r\n"
            "; whatever the game is set to. A wider view means less head turning to see\r\n"
            "; the same thing; 65 is the game's default and 90 is a common choice.\r\n"
            "; Saving the game's graphics settings puts its own value back until the next\r\n"
            "; launch.\r\n"
            "FieldOfView=0\r\n"
            "\r\n"
            "[Position]\r\n"
            "; 6DOF lean. Limits are metres.\r\n"
            "Enabled=true\r\n"
            "LimitX=0.30\r\n"
            "LimitY=0.20\r\n"
            "LimitYDown=");
            ini += limitYDown;
            ini +=
            "\r\n"
            "LimitZ=0.40\r\n"
            "LimitZBack=0.10\r\n"
            "\r\n"
            "[Hotkeys]\r\n"
            "; Windows virtual-key codes. Ctrl+Shift+Y / G / H work as alternatives.\r\n"
            "ToggleKey=0x23\r\n"
            "PositionKey=0x21\r\n"
            "YawModeKey=0x22\r\n";
            return ini;
        }
    }

    void LoadConfig(const std::string& exeDir, Config& out)
    {
        legacy::Config read;
        legacy::LoadConfig(exeDir, read);
        out.udp_port = read.udp_port;
        out.enable_on_startup = read.enable_on_startup;
        out.world_space_yaw = read.world_space_yaw;
        out.toggle_key = read.toggle_key;
        out.position_key = read.position_key;
        out.yaw_mode_key = read.yaw_mode_key;
        out.local_smoothing = read.local_smoothing;
        out.remote_smoothing = read.remote_smoothing;
        out.max_extrapolation_fraction = read.max_extrapolation_fraction;
        out.field_of_view = read.field_of_view;
        out.position_enabled = read.position_enabled;
        out.limit_x = read.limit_x;
        out.limit_y = read.limit_y;
        out.limit_y_down = read.limit_y_down;
        out.limit_z = read.limit_z;
        out.limit_z_back = read.limit_z_back;
    }

    void WriteDefaultConfigIfMissing(const std::string& exeDir)
    {
        const std::string path = IniPath(exeDir);
        if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) return;

        HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            Log::Line("Could not create %s (error %lu) - running on defaults.",
                      kIniName, GetLastError());
            return;
        }
        const std::string defaultIni = BuildDefaultIni();
        const DWORD kLength = static_cast<DWORD>(defaultIni.size());
        DWORD written = 0;
        const BOOL wrote = WriteFile(file, defaultIni.data(), kLength, &written, nullptr);
        const DWORD error = GetLastError();
        CloseHandle(file);

        // A short write leaves a file that PARSES - every key past the cut is
        // simply absent - so the next launch reads defaults for half the
        // settings, finds the file present and never rewrites it. The player's
        // edits then apply to a file the mod has already truncated. Delete the
        // partial one so the next launch writes a whole file.
        if (!wrote || written != kLength)
        {
            DeleteFileA(path.c_str());
            Log::Line("Only %lu of %lu bytes of %s reached disk (error %lu) - removed the "
                      "partial file and stayed on defaults; it is written again next launch.",
                      written, kLength, kIniName, error);
            return;
        }
        Log::Line("Wrote default %s beside the game exe.", kIniName);
    }
}
