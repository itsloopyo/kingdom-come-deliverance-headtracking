// Behaviour lock for the hand-off between the view update, which decides the
// frame's pose, and the render hook, which draws through it. Every rule here has
// a symptom a screenshot would not explain: a stale pose keeps moving the picture
// through a loading screen, a pose that survives its view update draws one
// frame's passes from two different views, and a channel that reports a pose it
// never received points the camera somewhere the player never looked.

#include "test_support.h"

#include "pose_channel.h"

using kcd_ht::HeadPose;
using kcd_ht::PoseChannel;
using kcd_ht::PoseSnapshot;
using kcd_tests::Check;
using kcd_tests::NearEqual;

namespace
{
    constexpr std::uint64_t kNow = 1'000'000;

    HeadPose Pose(float yaw, float pitch, float roll)
    {
        HeadPose pose;
        pose.yaw = yaw;
        pose.pitch = pitch;
        pose.roll = roll;
        return pose;
    }
}

int RunPoseChannelTests()
{
    int failures = 0;
    std::cout << "Pose channel\n";

    {
        PoseChannel channel;
        PoseSnapshot snapshot;
        Check(failures, !channel.TryTake(snapshot, kNow),
              "a channel nothing has published to hands out no pose");
    }

    {
        PoseChannel channel;
        channel.Publish(Pose(10.0f, -5.0f, 2.0f), true, kNow);

        PoseSnapshot snapshot;
        const bool took = channel.TryTake(snapshot, kNow);
        Check(failures, took && NearEqual(snapshot.pose.yaw, 10.0)
                     && NearEqual(snapshot.pose.pitch, -5.0)
                     && NearEqual(snapshot.pose.roll, 2.0)
                     && snapshot.positionActive,
              "a published pose comes back whole, position flag and all");
    }

    {
        // The freshness window is what stops a view that has stopped updating -
        // a load screen, a video - from drawing through the pose it last had.
        PoseChannel channel;
        channel.Publish(Pose(10.0f, 0.0f, 0.0f), false, kNow);

        PoseSnapshot snapshot;
        Check(failures, channel.TryTake(snapshot, kNow + PoseChannel::kFreshnessMs - 1),
              "a pose one millisecond inside the window is still drawable");
        Check(failures, !channel.TryTake(snapshot, kNow + PoseChannel::kFreshnessMs),
              "a pose exactly as old as the window is no longer drawable");
        Check(failures, !channel.TryTake(snapshot, kNow + 10 * PoseChannel::kFreshnessMs),
              "a long-stale pose is never drawable");
    }

    {
        // The view update clears the previous frame's pose rather than the render
        // hook consuming it, because several passes are built from the player's
        // camera in one frame and they all have to be drawn from the same view.
        PoseChannel channel;
        channel.Publish(Pose(10.0f, 0.0f, 0.0f), true, kNow);

        PoseSnapshot snapshot;
        Check(failures, channel.TryTake(snapshot, kNow) && channel.TryTake(snapshot, kNow),
              "taking a pose does not consume it - a frame can draw several passes");

        channel.Invalidate();
        Check(failures, !channel.TryTake(snapshot, kNow),
              "an invalidated pose is dropped even while it is still fresh");

        channel.Publish(Pose(20.0f, 0.0f, 0.0f), false, kNow);
        Check(failures, channel.TryTake(snapshot, kNow) && NearEqual(snapshot.pose.yaw, 20.0),
              "publishing after an invalidate arms the channel again");
    }

    {
        // More frames than the ring holds. The reader always wants the newest
        // slot, and wrapping must not hand it an older one.
        PoseChannel channel;
        PoseSnapshot snapshot;
        bool newestEveryTime = true;
        for (int i = 1; i <= 21; ++i)
        {
            channel.Publish(Pose(static_cast<float>(i), 0.0f, 0.0f), false, kNow);
            newestEveryTime = newestEveryTime && channel.TryTake(snapshot, kNow)
                           && NearEqual(snapshot.pose.yaw, static_cast<double>(i));
        }
        Check(failures, newestEveryTime, "the ring always hands out the newest pose");
    }

    return kcd_tests::Report("Pose channel", failures);
}
