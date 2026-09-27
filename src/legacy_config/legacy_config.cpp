#include "legacy_config/legacy_config.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include <cameraunlock/config/ini_reader.h>

#include "logging.h"

namespace kcd_ht::legacy
{
    namespace
    {
        // cameraunlock::math::SanitizeFinite and cameraunlock::NormalizeUdpPort as
        // the reader called them at core f92be69, copied here because core does not
        // freeze them the way it freezes IniReader.
        float SanitizeFinite(float value, float fallback, float lo, float hi)
        {
            return std::clamp(std::isfinite(value) ? value : fallback, lo, hi);
        }

        std::uint16_t NormalizeUdpPort(int raw, std::uint16_t fallback, bool& valid)
        {
            valid = (raw >= 1024 && raw <= 65535);
            return valid ? static_cast<std::uint16_t>(raw) : fallback;
        }

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
            const float value = SanitizeFinite(raw, fallback, lo, hi);
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
        // than being clamped into the band.
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

        // The published build took 0x07..0xFE: GetAsyncKeyState's range without
        // the mouse buttons, since ToggleKey=0x01 made every left click a toggle.
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
    }

    void LoadConfig(const std::string& exeDir, Config& out)
    {
        const std::string path = exeDir + "\\" + kIniName;

        cameraunlock::IniReader reader;
        if (!reader.Open(path))
        {
            Log::Line("No %s beside the game exe - using defaults.", kIniName);
            return;
        }

        bool portValid = true;
        const int rawPort = reader.ReadInt(kTracking, "UdpPort", out.udp_port);
        out.udp_port = NormalizeUdpPort(rawPort, static_cast<std::uint16_t>(out.udp_port), portValid);
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
        // Falls back to whatever LimitY resolved to, not to the struct default.
        out.limit_y_down = ReadPositionLimit(reader, "LimitYDown", out.limit_y);
        out.limit_z = ReadPositionLimit(reader, "LimitZ", out.limit_z);
        out.limit_z_back = ReadPositionLimit(reader, "LimitZBack", out.limit_z_back);

        out.toggle_key = ReadHotkeyChecked(reader, "ToggleKey", out.toggle_key);
        out.position_key = ReadHotkeyChecked(reader, "PositionKey", out.position_key);
        out.yaw_mode_key = ReadHotkeyChecked(reader, "YawModeKey", out.yaw_mode_key);
    }

    const std::vector<cameraunlock::config::LegacyKey>& Keys()
    {
        static const std::vector<cameraunlock::config::LegacyKey> keys = {
            {kTracking, "UdpPort"},
            {kTracking, "EnableOnStartup"},
            {kTracking, "WorldSpaceYaw"},
            {kTracking, "LocalSmoothing"},
            {kTracking, "RemoteSmoothing"},
            {kTracking, "MaxExtrapolationFraction"},
            {kCamera, "FieldOfView"},
            {kPosition, "Enabled"},
            {kPosition, "LimitX"},
            {kPosition, "LimitY"},
            {kPosition, "LimitYDown"},
            {kPosition, "LimitZ"},
            {kPosition, "LimitZBack"},
            {kHotkeys, "ToggleKey"},
            {kHotkeys, "PositionKey"},
            {kHotkeys, "YawModeKey"},
        };
        return keys;
    }
}
