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

    // The field of view off the PLAYER'S VIEW camera, in radians, or 0 before
    // the first active view update.
    //
    // This is cl_fov - it reads 75 on a game set to 75 and 65 on one left at
    // the default, and KCD's own slider only offers 60 to 75. The camera handed
    // to the render pass carries a flat 40 for stretches of play, identical on
    // both installs and outside that range, so it is not this game's frustum
    // whatever else it is. The reticle scales by tan(fov/2), so picking the 40
    // up moves the crosshair 2.1x too far while the picture moves normally.
    float ViewFieldOfViewRadians();

    // Counts what actually reaches the picture. A pose is invalidated at the
    // top of every active view update and only republished if the tracking,
    // session and gameplay gates all pass, so a frame whose gate fails renders
    // CLEAN. That is invisible in a screenshot and unmistakable here: a picture
    // alternating between tracked and clean reads as a camera that barely
    // moves, while the crosshair - held for 250ms - still shows the full
    // deflection. Compare posesPublished against passesTracked on the
    // heartbeat; they should be within a frame or two of each other.
    void NotePosePublished();
    void NotePassTracked();
    void NotePassUntracked();

    // Names every pass-info build made from the player's camera, once per
    // distinct call site, with the frustum that pass carries and whether it is
    // the world pass the reticle is scaled by. The engine builds several from
    // the same CCamera and they do NOT share a field of view, so a log that
    // reports only one of them cannot show a reticle scaled by the wrong one.
    void NotePassFrustum(std::uintptr_t returnRva, float fovRadians, float projectionRatio);

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
