#pragma once

#include <atomic>

#include <cameraunlock/protocol/udp_receiver.h>
#include <cameraunlock/tracking/head_tracking_session.h>

namespace kcd_ht
{
    using Session = cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver>;

    // The session picks between LocalSmoothing and RemoteSmoothing from the
    // receiver's source-address check. That wiring is compile-time detected, so
    // a receiver without IsRemoteConnection() would silently pin every session
    // to the local value instead of failing to build.
    static_assert(Session::kHasRemoteConnection,
                  "receiver must expose IsRemoteConnection() for per-connection smoothing");

    // Written by the hotkey thread and read by the hook on every frame, so every
    // member is atomic. Seeded from the INI at bootstrap.
    struct RuntimeState
    {
        std::atomic<bool> trackingEnabled{true};
        // true = yaw about the world up-axis (horizon-locked); false = yaw about
        // the camera's own up-axis.
        std::atomic<bool> worldSpaceYaw{true};
    };

    inline RuntimeState& Runtime()
    {
        static RuntimeState state;
        return state;
    }
}
