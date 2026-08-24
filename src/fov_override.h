#pragma once

#include <cstdint>

// Kingdom Come renders through whatever vertical field of view its `cl_fov`
// console variable holds. The game's own video options expose that value, but
// only across 60 to 75 degrees, and head tracking is one of the things that
// wants more than 75: the wider the picture, the less of a head turn it takes
// to see something that is already on screen.
//
// So the option here writes the same console variable the game's own slider
// writes, rather than widening the frustum behind the game's back. Everything
// that derives from the camera - culling, the HUD, world-anchored markers, the
// interaction pick - stays consistent with the picture, and this mod's own
// reticle projection reads the field of view back off the live camera every
// frame, so it follows with no calibration of its own.

namespace kcd_ht::fov
{
    // @p degrees is a VERTICAL field of view, the same number the game's slider
    // uses. Returns false only while the console has not been created yet, so a
    // caller on a per-frame path can simply try again; every other outcome is
    // final and has been logged.
    bool Apply(std::uintptr_t moduleBase, float degrees);
}
