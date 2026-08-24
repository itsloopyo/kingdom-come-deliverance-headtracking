#pragma once

#include <cstdint>

#include <cameraunlock/protocol/udp_receiver.h>

#include "runtime_state.h"

// Everything the log says about a running session, in one place, so the hooks
// carry the injection and nothing else.
//
// The heartbeat is the mod's only channel to a player whose head tracking is not
// working: it has to name which half is at fault - the tracker, the port, the
// gameplay gate or the camera - without them running anything.

namespace kcd_ht
{
    struct Matrix34f;
}

namespace kcd_ht::diagnostics
{
    // Names what the heartbeat reports on. Must run before the view hooks go
    // live; both references have to outlive the process, which they do because
    // the mod pins itself and never unloads.
    void Bind(const cameraunlock::UdpReceiver& receiver, const Session& session);

    // Per-frame diagnostics for the ACTIVE view: counts the frame, captures the
    // frustum fields off the view's own camera and emits the heartbeat when one
    // is due. @p view is the CView the engine has just updated.
    void NoteActiveViewUpdate(const void* view);

    // TEMPORARY culling probe. Records which CCamera the engine's asynchronous
    // occlusion check is testing object bounding boxes against, because that
    // test - and not frustum culling - is what removes buildings and NPCs once
    // the head turns. Remove once the answer is in.
    void NoteSystemRender(const void* system);
    void ProbeCulling(std::uintptr_t moduleBase, const Matrix34f& clean, const Matrix34f& tracked);

    // TEMPORARY. Arms a hardware write-watchpoint on the matrix the occlusion
    // test uses, so the code that fills it names itself instead of being hunted
    // for. Logs the writing instruction and a stack trace, then disarms.
    void WatchCullMatrix(std::uintptr_t moduleBase);
}
