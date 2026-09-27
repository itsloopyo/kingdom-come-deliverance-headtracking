#include "hotkey_bindings.h"

#include <stdexcept>
#include <string>

namespace kcd_ht
{
    namespace
    {
        std::vector<cameraunlock::input::KeyBinding> Parse(const char* key, const std::string& list)
        {
            const cameraunlock::input::KeyBindingsParseResult parsed =
                cameraunlock::input::ParseKeyBindings(list);
            if (!parsed.ok())
                throw std::logic_error(std::string(key) + "=" + list + " does not parse: " + parsed.error);
            return parsed.bindings;
        }
    }

    HotkeyBindings BindingsFor(const Config& config)
    {
        HotkeyBindings bindings;
        bindings.toggle = Parse("ToggleKey", config.toggle_key);
        bindings.cycle_tracking_mode = Parse("CycleTrackingModeKey", config.cycle_tracking_mode_key);
        bindings.yaw_mode = Parse("YawModeKey", config.yaw_mode_key);
        return bindings;
    }
}
