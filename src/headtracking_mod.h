#pragma once

#include <windows.h>

namespace kcd_ht
{
    // Called from DllMain. Spawns the bootstrap thread and returns immediately -
    // nothing that waits, hooks or loads a library may run under the loader lock.
    //
    // There is no matching shutdown. The module pins itself, so the only detach
    // is process exit: the kernel has already killed the other threads without
    // unwinding, and the detours cannot be taken back safely in any case (MinHook
    // rewinds a thread sitting in a trampoline, but not one sitting in our own
    // detour body).
    void Initialize(HMODULE module);
}
