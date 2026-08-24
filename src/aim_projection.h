#pragma once

#include "cryengine_types.h"

// Where the player is actually aiming, expressed as a screen offset from the
// centre of the head-tracked view. Pure functions over the same two matrices the
// injection produces, so it is testable without a game process and cannot drift
// from the camera composition the way a re-derived Euler formula would.

namespace kcd_ht
{
    struct AimProjection
    {
        // Tangent of the angle between the tracked view's centre and the aim
        // direction. Positive right is screen-right, positive up is screen-up.
        float tanRight = 0.0f;
        float tanUp = 0.0f;
        // False when the aim direction has passed behind the tracked view plane
        // (a head turn past 90 degrees). The reticle must not be drawn at centre
        // then - centre is the one position that reads as "your shot goes here".
        // Kingdom Come's crosshair is the game's own element and this mod can
        // only move it, so cursor_hook holds it at the edge it was last pushed
        // to; a mod that draws its own reticle should hide it outright.
        bool inFront = false;
    };

    // @p clean is the camera matrix the engine composed from m_viewParams - the
    // direction the game will fire, raycast and swing along. @p tracked is what
    // ApplyHeadPose returned and what the player actually sees.
    //
    // Only the aim DIRECTION is projected, not a point at some assumed distance.
    // A distance would only matter for the 6DOF lean parallax, and there is no
    // hit distance to raycast for here: assuming one puts the error where it
    // hurts most. At a 3 m guess a 0.3 m lean throws a 30 m bow shot off by more
    // than 5 degrees, where ignoring the parallax costs about half a degree at
    // that range and nothing at all for pure rotation, which is the common case.
    AimProjection ProjectAim(const Matrix34f& clean, const Matrix34f& tracked);

    struct ScreenPoint
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    // Maps a projection onto the back buffer. @p fovRadians is CCamera's VERTICAL
    // field of view and @p projectionRatio its width-to-height ratio, both read
    // straight off the live camera so an FOV slider or an ultrawide aspect needs
    // no calibration constant.
    ScreenPoint ToScreen(const AimProjection& aim, float screenWidth, float screenHeight,
                         float fovRadians, float projectionRatio);

    struct ScreenOffset
    {
        float dx = 0.0f;
        float dy = 0.0f;
    };

    // The same mapping expressed as a pixel offset from the centre of the frame,
    // which is the form the HUD cursor helper takes, and kept inside the frame by
    // @p edgeMarginPixels.
    //
    // The clamp is not cosmetic: the projection runs away as the aim approaches
    // the tracked view plane - an 89.4 degree head turn puts it 80,000 px off
    // screen - and a cursor thrown that far is a cursor the HUD has to deal with
    // for no benefit. At the edge it still points the right way out of frame.
    ScreenOffset ClampedCentreOffset(const AimProjection& aim,
                                     float screenWidth, float screenHeight,
                                     float fovRadians, float projectionRatio,
                                     float edgeMarginPixels);
}
