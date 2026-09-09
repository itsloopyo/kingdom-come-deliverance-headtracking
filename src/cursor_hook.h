#pragma once

#include <cstdint>

#include "aim_projection.h"

// Moves the GAME's crosshair to where the shot actually goes, rather than
// hiding it and drawing our own. The stock cursor carries weapon, stamina and
// interaction state that a plain dot cannot, so shifting it is worth the extra
// pinned addresses.
//
// The HUD sets its cursor through one helper that takes a position in back
// buffer pixels, subtracts the screen centre and converts the remainder into
// Flash units for the `CursorCross` / `CombatCursorCross` elements. That
// conversion is linear, so adding the aim offset to the pixel position it is
// handed lands the cursor exactly where the reticle projection says it should
// be, with no knowledge of Flash needed.

namespace kcd_ht::cursor
{
    bool Install(std::uintptr_t moduleBase);

    // The pixel space the HUD positions cursors in, or false before the renderer
    // is up. Reported on the heartbeat so a renderer slot a patch has moved shows
    // up in a log rather than only as a crosshair thrown off screen.
    bool HudPixelSize(float& width, float& height);

    // Called from the render hook on every frame tracking is applied. Doubles as
    // the liveness signal: when these stop arriving the cursor goes back to
    // wherever the game wanted it, which is what makes menus and loading screens
    // look after themselves.
    //
    // @p fovRadians is the VERTICAL field of view of the camera the world is
    // drawn with. The aspect is NOT taken from a camera field: it is the HUD's
    // own pixel space, which is the back buffer and cannot disagree with what
    // the player is looking at.
    //
    // @p headYawDeg and @p headPitchDeg are the tracker's own angles. They are
    // carried only so the reticle probe can print the head angle beside the
    // offset it produced: the projection is geometric and never reads them.
    void SubmitAim(const AimProjection& aim, float fovRadians,
                   float headYawDeg, float headPitchDeg);
}
