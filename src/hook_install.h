#pragma once

#include <cameraunlock/hooks/hook_manager.h>

namespace kcd_ht
{
    // MinHook's create-then-enable pair. Every hook site here wants the same
    // thing - a trampoline that is live by the time the call returns - and the
    // first failing step decides the outcome, so one status covers both.
    // HookManager::Initialize() must already have run; the bootstrap does it
    // once for the whole mod.
    inline cameraunlock::hooks::HookStatus CreateAndEnableHook(void* target, void* detour,
                                                               void** original)
    {
        auto& manager = cameraunlock::hooks::HookManager::Instance();
        const cameraunlock::hooks::HookStatus created =
            manager.CreateHook(target, detour, original);
        if (created != cameraunlock::hooks::HookStatus::Ok) return created;
        return manager.EnableHook(target);
    }
}
