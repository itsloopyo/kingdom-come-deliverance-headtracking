#pragma once

#include <cstdint>

#include "runtime_state.h"

// The engine side of the injection: the three detours that decide the frame's
// pose and hand the renderer a camera with it applied.
//
// Nothing here writes to the game's own camera. CView::Update decides the pose
// and writes nothing; the render pass builder copies the camera it is handed,
// applies the pose to the copy and passes that on. The game reads its view
// camera back from 141 call sites for interaction focus, aim and raycasts, and
// every one of them sees the rotation the game itself computed.

namespace kcd_ht
{
    // @p fieldOfViewDegrees is the vertical FOV override from the INI, or 0 to
    // leave the game's own setting alone. It is applied once, on the first
    // active view update, because the game writes cl_fov itself during startup.
    //
    // @p session must outlive the process. It does: the mod pins itself and the
    // hooks are never taken back, so a session that went away would leave a
    // detour reading freed memory.
    //
    // Returns false with the reason logged if any hook could not be installed,
    // in which case the mod stays dormant. HookManager::Initialize() must
    // already have run.
    bool InstallViewHooks(std::uintptr_t moduleBase, Session& session, float fieldOfViewDegrees);
}
