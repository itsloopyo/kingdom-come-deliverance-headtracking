#include "config.h"

#include <cmath>
#include <cstdint>
#include <windows.h>

#include <cameraunlock/config/ini_reader.h>
#include <cameraunlock/math/finite_utils.h>
#include <cameraunlock/protocol/port_utils.h>

#include "logging.h"

namespace kcd_ht
{
    namespace
    {
        constexpr char kIniName[] = "HeadTracking.ini";
        constexpr char kTracking[] = "HeadTracking";
        constexpr char kHotkeys[] = "Hotkeys";
        constexpr char kPosition[] = "Position";
        constexpr char kCamera[] = "Camera";

        // Degrees of VERTICAL field of view. The game's own slider covers 60 to
        // 75; the band here is wider on both sides because overriding that slider
        // is the point, and it is what stops a typo from reaching the frustum.
        constexpr float kMinFieldOfView = 40.0f;
        constexpr float kMaxFieldOfView = 120.0f;

        // Metres. Deliberately far wider than anything a player would choose - the
        // bound exists to stop a typo reaching the maths, not to second-guess a
        // setting.
        constexpr float kMinPositionLimit = 0.01f;
        constexpr float kMaxPositionLimit = 5.0f;

        // Nothing downstream of the INI rejects a bad float. strtod accepts "nan"
        // and "inf" and overflows a literal like 1e400 to +inf; a NaN limit then
        // poisons the smoothing state for the rest of the session and presents as
        // the view simply being gone, so the substitution is logged with the key
        // that caused it instead of being applied quietly.
        float ReadFloatChecked(const cameraunlock::IniReader& reader, const char* section,
                               const char* key, float fallback, float lo, float hi)
        {
            const float raw = reader.ReadFloat(section, key, fallback);
            const float value = cameraunlock::math::SanitizeFinite(raw, fallback, lo, hi);
            if (value != raw)
                Log::Line("WARNING: config [%s] %s = %g is not a number in [%g, %g] - using %g.",
                          section, key, static_cast<double>(raw), static_cast<double>(lo),
                          static_cast<double>(hi), static_cast<double>(value));
            return value;
        }

        float ReadPositionLimit(const cameraunlock::IniReader& reader, const char* key,
                                float fallback)
        {
            return ReadFloatChecked(reader, kPosition, key, fallback,
                                    kMinPositionLimit, kMaxPositionLimit);
        }

        // Zero is not a field of view, it is the off switch, so it cannot go
        // through ReadFloatChecked's clamp. Anything else that is not a field of
        // view a person could play at leaves the game's own setting alone rather
        // than being clamped into the band: a mistyped value should not silently
        // change what the player sees, and the game already has a setting of its
        // own for this.
        float ReadFieldOfView(const cameraunlock::IniReader& reader, float fallback)
        {
            const float raw = reader.ReadFloat(kCamera, "FieldOfView", fallback);
            if (raw == 0.0f) return 0.0f;
            if (!std::isfinite(raw) || raw < kMinFieldOfView || raw > kMaxFieldOfView)
            {
                Log::Line("WARNING: config [%s] FieldOfView = %g is not a field of view in "
                          "[%g, %g] - leaving the game's own setting alone.",
                          kCamera, static_cast<double>(raw), static_cast<double>(kMinFieldOfView),
                          static_cast<double>(kMaxFieldOfView));
                return 0.0f;
            }
            return raw;
        }

        // GetAsyncKeyState takes a virtual-key code in 1..254, and the hotkey
        // poller treats 0 as "unbound" and fires nothing. The mouse buttons are
        // excluded on top of that: GetAsyncKeyState reports them like any other
        // key, so ToggleKey=0x01 turns every left-click in normal play into a
        // tracking toggle, which presents as the mod switching itself off at
        // random rather than as a config error. ReadHex has no range of its own -
        // it hands back whatever strtol made of the text, including 0 for a typo
        // and -1 for an overflowed 0xFFFFFFFF - so the range is checked here.
        constexpr int kMinVirtualKey = 0x07;
        constexpr int kMaxVirtualKey = 0xFE;

        int ReadHotkeyChecked(const cameraunlock::IniReader& reader, const char* key, int fallback)
        {
            const int raw = reader.ReadHex(kHotkeys, key, fallback);
            if (raw >= kMinVirtualKey && raw <= kMaxVirtualKey) return raw;
            Log::Line("WARNING: config [%s] %s = 0x%X is not a bindable virtual-key code in "
                      "[0x%02X, 0x%02X] - using 0x%X.",
                      kHotkeys, key, static_cast<unsigned>(raw),
                      static_cast<unsigned>(kMinVirtualKey), static_cast<unsigned>(kMaxVirtualKey),
                      static_cast<unsigned>(fallback));
            return fallback;
        }

        std::string IniPath(const std::string& exeDir)
        {
            return exeDir + "\\" + kIniName;
        }

        // Comments for bool keys go on their OWN line: the INI reader is built on
        // GetPrivateProfileStringA, which does not treat ';' as an inline comment
        // introducer, so "Enabled=true ; note" matches no known bool spelling and
        // silently falls back to the default.
        constexpr char kDefaultIni[] =
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
            "LimitYDown=0.20\r\n"
            "LimitZ=0.40\r\n"
            "LimitZBack=0.10\r\n"
            "\r\n"
            "[Hotkeys]\r\n"
            "; Windows virtual-key codes. Ctrl+Shift+Y / G / H work as alternatives.\r\n"
            "ToggleKey=0x23\r\n"
            "PositionKey=0x21\r\n"
            "YawModeKey=0x22\r\n";
    }

    void LoadConfig(const std::string& exeDir, Config& out)
    {
        const std::string path = IniPath(exeDir);

        cameraunlock::IniReader reader;
        if (!reader.Open(path))
        {
            Log::Line("No %s beside the game exe - using defaults.", kIniName);
            return;
        }

        bool portValid = true;
        const int rawPort = reader.ReadInt(kTracking, "UdpPort", out.udp_port);
        out.udp_port = cameraunlock::NormalizeUdpPort(rawPort,
                                                      static_cast<std::uint16_t>(out.udp_port),
                                                      portValid);
        if (!portValid)
            Log::Line("WARNING: config [%s] UdpPort = %d is out of range - using %d.",
                      kTracking, rawPort, out.udp_port);

        out.enable_on_startup = reader.ReadBool(kTracking, "EnableOnStartup", out.enable_on_startup);
        out.world_space_yaw = reader.ReadBool(kTracking, "WorldSpaceYaw", out.world_space_yaw);

        out.local_smoothing = ReadFloatChecked(reader, kTracking, "LocalSmoothing",
                                               out.local_smoothing, 0.0f, 1.0f);
        out.remote_smoothing = ReadFloatChecked(reader, kTracking, "RemoteSmoothing",
                                                out.remote_smoothing, 0.0f, 1.0f);
        out.max_extrapolation_fraction = ReadFloatChecked(reader, kTracking,
                                                          "MaxExtrapolationFraction",
                                                          out.max_extrapolation_fraction,
                                                          0.0f, 1.0f);

        out.field_of_view = ReadFieldOfView(reader, out.field_of_view);

        out.position_enabled = reader.ReadBool(kPosition, "Enabled", out.position_enabled);
        out.limit_x = ReadPositionLimit(reader, "LimitX", out.limit_x);
        out.limit_y = ReadPositionLimit(reader, "LimitY", out.limit_y);
        out.limit_y_down = ReadPositionLimit(reader, "LimitYDown", out.limit_y_down);
        out.limit_z = ReadPositionLimit(reader, "LimitZ", out.limit_z);
        out.limit_z_back = ReadPositionLimit(reader, "LimitZBack", out.limit_z_back);

        out.toggle_key = ReadHotkeyChecked(reader, "ToggleKey", out.toggle_key);
        out.position_key = ReadHotkeyChecked(reader, "PositionKey", out.position_key);
        out.yaw_mode_key = ReadHotkeyChecked(reader, "YawModeKey", out.yaw_mode_key);
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
        constexpr DWORD kLength = static_cast<DWORD>(sizeof(kDefaultIni) - 1);
        DWORD written = 0;
        const BOOL wrote = WriteFile(file, kDefaultIni, kLength, &written, nullptr);
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
