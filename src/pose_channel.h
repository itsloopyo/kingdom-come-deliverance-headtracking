#pragma once

#include <atomic>
#include <cstdint>

#include "view_injection.h"

// Hands the pose the view update decided on to the render hook that applies it.
//
// The two live on different call paths and, in CryEngine, potentially on
// different threads: the pass-info builder the pose is read from has 14 call
// sites, at least one outside CSystem::Render, and render passes are filled from
// workers. A plain six-float struct written in place could therefore be read
// half-written, which is a camera snapping to a pose that never existed. So the
// pose is published into a ring through a sequence counter and read whole.

namespace kcd_ht
{
    struct PoseSnapshot
    {
        HeadPose pose;
        bool positionActive = false;
        std::uint64_t stampMs = 0;
    };

    class PoseChannel
    {
    public:
        // Two frames at 30 fps. Long enough that a slow frame still draws
        // through the pose the view decided on, short enough that a view which
        // has stopped updating - a load screen, a video - stops moving the
        // picture.
        static constexpr std::uint64_t kFreshnessMs = 66;

        // Single writer: only the active view's update publishes.
        void Publish(const HeadPose& pose, bool positionActive, std::uint64_t nowMs);

        // Drops the pose until the next Publish. The view update clears the
        // previous frame's pose this way rather than the render hook consuming
        // it, because more than one pass can be built from the player's camera
        // in a frame and they all have to be drawn from the same view.
        void Invalidate();

        // Fills @p out and returns whether it is fresh enough to draw through.
        // @p out is written either way; a stale read must be ignored, not used.
        bool TryTake(PoseSnapshot& out, std::uint64_t nowMs) const;

    private:
        static constexpr unsigned kSlots = 4;

        PoseSnapshot m_slots[kSlots];
        std::atomic<unsigned> m_sequence{0};
        std::atomic<bool> m_valid{false};
    };
}
