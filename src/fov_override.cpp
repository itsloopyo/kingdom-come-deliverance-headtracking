#include "fov_override.h"

#include "builds/build_registry.h"
#include "logging.h"

namespace kcd_ht::fov
{
    namespace
    {
        using GetCVar_t = void*(__fastcall*)(void* console, const char* name);
        using GetFVal_t = float(__fastcall*)(void* cvar);
        using SetFloat_t = void(__fastcall*)(void* cvar, float value);

        constexpr char kCVarName[] = "cl_fov";
    }

    bool Apply(std::uintptr_t moduleBase, float degrees)
    {
        const auto& offsets = builds::Offsets();

        auto* console = *reinterpret_cast<void**>(moduleBase + offsets.kConsoleGlobalRva);
        if (console == nullptr) return false;

        auto** consoleVtable = *reinterpret_cast<void***>(console);
        void* cvar = reinterpret_cast<GetCVar_t>(
            consoleVtable[offsets.kConsoleGetCVarSlot / sizeof(void*)])(console, kCVarName);
        if (cvar == nullptr)
        {
            Log::Line("The console has no %s variable, so the field of view is left as the game "
                      "set it. This build is not the one the profile was derived from.",
                      kCVarName);
            return true;
        }

        auto** cvarVtable = *reinterpret_cast<void***>(cvar);
        const auto getFVal =
            reinterpret_cast<GetFVal_t>(cvarVtable[offsets.kCVarGetFValSlot / sizeof(void*)]);
        const float before = getFVal(cvar);

        reinterpret_cast<SetFloat_t>(
            cvarVtable[offsets.kCVarSetFloatSlot / sizeof(void*)])(cvar, degrees);

        // Read back rather than announce what was asked for. The two vtable
        // slots are pinned per build, and a slot a patch has moved would take
        // the write somewhere else entirely - which a log line saying "set to
        // 100" would still claim had worked.
        Log::Line("field of view: %s was %.1f degrees, now %.1f (asked for %.1f). Saving the "
                  "game's own graphics settings puts its Vertical FOV back; relaunch to get this "
                  "one again.",
                  kCVarName, static_cast<double>(before), static_cast<double>(getFVal(cvar)),
                  static_cast<double>(degrees));
        return true;
    }
}
