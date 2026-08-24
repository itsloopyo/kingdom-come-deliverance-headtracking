#include "headtracking_mod.h"

#include <windows.h>

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID lpReserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(module);
        kcd_ht::Initialize(module);
        break;
    case DLL_PROCESS_DETACH:
        // The module pins itself in Initialize, so lpReserved is always non-null
        // here: this is process exit, where the kernel has already killed the
        // other threads without unwinding and joining anything would deadlock.
        // Let the OS reclaim it.
        (void)lpReserved;
        break;
    }
    return TRUE;
}
