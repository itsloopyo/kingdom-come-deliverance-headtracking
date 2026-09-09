#include "native_tracking.h"

#include "builds/build_registry.h"
#include "logging.h"

namespace kcd_ht::native_tracking
{
    namespace
    {
        using GetCVar = void*(__fastcall*)(void*, const char*);
        using GetFloat = float(__fastcall*)(void*);
        using SetFloat = void(__fastcall*)(void*, float);

        struct Setting
        {
            const char* name;
            void* cvar = nullptr;
            float saved = 0.0f;
        };

        Setting g_settings[] = {
            {"tobii_enabled"},
            {"tobii_ext_view_on"},
            {"tobii_interact_on"},
            {"tobii_lock_target_on"},
            {"tobii_cleanui_on"},
        };
        bool g_suppressed = false;
        bool g_failed = false;
    }

    bool Update(std::uintptr_t moduleBase, bool suppress)
    {
        if (g_failed) return false;
        if (!suppress && !g_suppressed) return true;

        const auto& offsets = builds::Offsets();
        auto* console = *reinterpret_cast<void**>(moduleBase + offsets.kConsoleGlobalRva);
        if (console == nullptr) return false;
        auto** consoleVtable = *reinterpret_cast<void***>(console);
        const auto getCVar = reinterpret_cast<GetCVar>(
            consoleVtable[offsets.kConsoleGetCVarSlot / sizeof(void*)]);

        for (auto& setting : g_settings)
        {
            if (setting.cvar != nullptr) continue;
            setting.cvar = getCVar(console, setting.name);
            if (setting.cvar == nullptr)
            {
                Log::Line("Native Tobii control failed: missing %s. Mod tracking suppressed.",
                          setting.name);
                g_failed = true;
                return false;
            }
        }

        for (auto& setting : g_settings)
        {
            auto** vtable = *reinterpret_cast<void***>(setting.cvar);
            const auto get = reinterpret_cast<GetFloat>(
                vtable[offsets.kCVarGetFValSlot / sizeof(void*)]);
            const auto set = reinterpret_cast<SetFloat>(
                vtable[offsets.kCVarSetFloatSlot / sizeof(void*)]);
            const float current = get(setting.cvar);
            if (!g_suppressed) setting.saved = current;
            const float desired = suppress ? 0.0f : setting.saved;
            if (current == desired) continue;

            set(setting.cvar, desired);
            const float actual = get(setting.cvar);
            Log::Line("Native Tobii: %s %.0f -> %.0f (%s).", setting.name,
                      current, actual, suppress ? "mod enabled" : "restored");
            if (actual != desired)
            {
                Log::Line("Native Tobii control failed: %s rejected %.0f. Mod tracking suppressed.",
                          setting.name, desired);
                g_failed = true;
                return false;
            }
        }

        if (g_suppressed != suppress)
            Log::Line("Native Tobii integration %s.", suppress ? "disabled while mod tracking is enabled"
                                                              : "restored to its previous settings");
        g_suppressed = suppress;
        return true;
    }
}
