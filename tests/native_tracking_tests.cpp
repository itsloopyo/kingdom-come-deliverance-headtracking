#include "test_support.h"

#include "builds/build_registry.h"
#include "native_tracking.h"

#include <cstring>

namespace
{
    struct CVar
    {
        void** vtable;
        const char* name;
        float value;
        int writes = 0;
        bool reject = false;
    };

    float __fastcall GetValue(void* self)
    {
        return static_cast<CVar*>(self)->value;
    }

    void __fastcall SetValue(void* self, float value)
    {
        auto& cvar = *static_cast<CVar*>(self);
        ++cvar.writes;
        if (!cvar.reject) cvar.value = value;
    }

    void* g_cvarVtable[] = {reinterpret_cast<void*>(&GetValue), reinterpret_cast<void*>(&SetValue)};
    CVar g_cvars[] = {
        {g_cvarVtable, "tobii_enabled", 1.0f},
        {g_cvarVtable, "tobii_ext_view_on", 1.0f},
        {g_cvarVtable, "tobii_interact_on", 0.0f},
        {g_cvarVtable, "tobii_lock_target_on", 1.0f},
        {g_cvarVtable, "tobii_cleanui_on", 0.0f},
    };

    void* __fastcall GetCVar(void*, const char* name)
    {
        for (auto& cvar : g_cvars)
            if (std::strcmp(cvar.name, name) == 0) return &cvar;
        return nullptr;
    }
}

namespace kcd_ht::builds
{
    const OffsetTable& Offsets()
    {
        static const OffsetTable offsets = [] {
            OffsetTable table{};
            table.kCVarSetFloatSlot = sizeof(void*);
            return table;
        }();
        return offsets;
    }
}

int main()
{
    using kcd_ht::native_tracking::Update;
    using kcd_tests::Check;
    int failures = 0;
    void* console = nullptr;
    const auto module = reinterpret_cast<std::uintptr_t>(&console);
    Check(failures, Update(module, false), "disabled mod needs no console");
    Check(failures, !Update(module, true), "tracking waits until the console exists");

    void* consoleVtable[] = {reinterpret_cast<void*>(&GetCVar)};
    void** consoleObject = consoleVtable;
    console = &consoleObject;

    Check(failures, Update(module, true), "native settings can be suppressed");
    for (const auto& cvar : g_cvars)
        Check(failures, cvar.value == 0.0f, std::string(cvar.name) + " disabled");
    const int writes = g_cvars[0].writes;
    Check(failures, Update(module, true) && g_cvars[0].writes == writes,
          "unchanged settings are not rewritten every frame");

    g_cvars[0].value = 1.0f;
    g_cvars[2].value = 1.0f;
    Check(failures, Update(module, true) && g_cvars[0].value == 0.0f && g_cvars[2].value == 0.0f,
          "game settings cannot reactivate Tobii under the mod");
    Check(failures, Update(module, false) && g_cvars[0].value == 1.0f && g_cvars[2].value == 0.0f,
          "toggle off restores the original mixture of enabled and disabled settings");

    g_cvars[0].value = 0.0f;
    g_cvars[2].value = 1.0f;
    Check(failures, Update(module, false) && g_cvars[2].value == 1.0f,
          "disabled mod leaves native settings editable");
    Check(failures, Update(module, true) && Update(module, false)
                   && g_cvars[0].value == 0.0f && g_cvars[2].value == 1.0f,
          "each activation restores the settings from that activation");

    g_cvars[2].reject = true;
    Check(failures, !Update(module, true), "a rejected native setting prevents mod tracking");
    Check(failures, !Update(module, true), "a rejected write is not silently retried");
    return failures == 0 ? 0 : 1;
}
