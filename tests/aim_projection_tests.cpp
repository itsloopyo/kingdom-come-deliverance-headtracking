// The reticle litmus tests. Every one of these is a shipped-and-fixed bug
// somewhere in the fleet: a projection that agrees with the camera on a single
// axis and then drifts the moment two are combined looks right in every quick
// check and wrong in the only situation that matters.
//
// The projection here is derived from the injected matrices rather than from
// Euler angles, so these tests are really asserting that the reticle and the
// camera cannot disagree - and they run against ApplyHeadPose itself, not
// against a reconstruction of it.

#include "test_support.h"

#include "aim_projection.h"
#include "view_injection.h"

#include <cmath>

using kcd_ht::AimProjection;
using kcd_ht::ApplyHeadPose;
using kcd_ht::HeadPose;
using kcd_ht::Matrix34f;
using kcd_ht::ClampedCentreOffset;
using kcd_ht::ProjectAim;
using kcd_ht::ScreenOffset;
using kcd_ht::ScreenPoint;
using kcd_ht::ToScreen;
using kcd_tests::Check;
using kcd_tests::NearEqual;

namespace
{
    constexpr double kDegToRad = 0.01745329251994329577;

    Matrix34f Identity()
    {
        Matrix34f m;
        m.m[0][0] = 1.0f; m.m[1][1] = 1.0f; m.m[2][2] = 1.0f;
        return m;
    }

    // A camera pitched down by @p degrees, so world-yaw mode has something to
    // conjugate through. Pitch is about the camera's right axis (+X).
    Matrix34f PitchedDown(float degrees)
    {
        const float r = static_cast<float>(degrees * kDegToRad);
        const float s = std::sin(-r), c = std::cos(-r);
        Matrix34f m;
        m.m[0][0] = 1.0f;
        m.m[1][1] = c; m.m[1][2] = -s;
        m.m[2][1] = s; m.m[2][2] = c;
        return m;
    }

    AimProjection Project(const Matrix34f& clean, const HeadPose& pose, bool worldYaw)
    {
        return ProjectAim(clean, ApplyHeadPose(clean, pose, worldYaw, false));
    }
}

int RunAimProjectionTests()
{
    int failures = 0;
    std::cout << "Aim projection tests\n";

    const Matrix34f level = Identity();

    {
        HeadPose pose;
        const AimProjection aim = Project(level, pose, false);
        Check(failures, aim.inFront && NearEqual(aim.tanRight, 0.0) && NearEqual(aim.tanUp, 0.0),
              "a still head leaves the reticle at screen centre");
    }

    {
        // Litmus 1: pure roll must not move the reticle. Roll turns about the
        // view axis, so the aim point it turns around is the centre itself.
        HeadPose pose; pose.roll = 25.0f;
        const AimProjection aim = Project(level, pose, false);
        Check(failures, aim.inFront && NearEqual(aim.tanRight, 0.0) && NearEqual(aim.tanUp, 0.0),
              "pure roll keeps the reticle at centre");
    }

    {
        // Litmus 2: pure pitch moves it vertically and only vertically. Looking
        // up puts the aim point lower in the picture.
        HeadPose pose; pose.pitch = 20.0f;
        const AimProjection aim = Project(level, pose, false);
        Check(failures, aim.inFront && NearEqual(aim.tanRight, 0.0),
              "pure pitch does not move the reticle sideways");
        Check(failures, aim.tanUp < 0.0f,
              "pitching the head up drops the reticle below centre");
        Check(failures, NearEqual(aim.tanUp, -std::tan(20.0 * kDegToRad)),
              "the vertical offset is the tangent of the head pitch");
    }

    {
        // Pure yaw is the mirror of the above. The boundary negates tracker yaw,
        // so a positive yaw turns the view right and the aim point falls to the
        // left of centre.
        HeadPose pose; pose.yaw = 20.0f;
        const AimProjection aim = Project(level, pose, false);
        Check(failures, aim.inFront && NearEqual(aim.tanUp, 0.0),
              "pure yaw does not move the reticle vertically");
        Check(failures, aim.tanRight < 0.0f,
              "turning the head right leaves the reticle left of centre");
    }

    {
        // Litmus 3: pitch combined with roll. The camera rolls the screen frame
        // around the view axis, so the offset must turn with it and keep its
        // length - a horizontal wander that grows with roll is the classic
        // symptom of a projection derived from per-axis tangents instead.
        HeadPose pitchOnly; pitchOnly.pitch = 20.0f;
        const AimProjection a = Project(level, pitchOnly, false);

        HeadPose pitchAndRoll = pitchOnly; pitchAndRoll.roll = 35.0f;
        const AimProjection b = Project(level, pitchAndRoll, false);

        const double lenA = std::sqrt(static_cast<double>(a.tanRight * a.tanRight + a.tanUp * a.tanUp));
        const double lenB = std::sqrt(static_cast<double>(b.tanRight * b.tanRight + b.tanUp * b.tanUp));
        Check(failures, NearEqual(lenA, lenB),
              "roll turns the pitch offset about centre without changing its distance");

        // Turned by exactly the angle the picture rolled, which is the NEGATED
        // tracker roll - the engine boundary in ApplyHeadPose flips it. Asserting
        // the raw tracker sign here would put the reticle 180 degrees out of
        // phase with the camera, which is the failure this test exists to catch.
        const double roll = -35.0 * kDegToRad;
        const double expectedRight = a.tanRight * std::cos(roll) - a.tanUp * std::sin(roll);
        const double expectedUp = a.tanRight * std::sin(roll) + a.tanUp * std::cos(roll);
        Check(failures, NearEqual(b.tanRight, expectedRight) && NearEqual(b.tanUp, expectedUp),
              "the combined pose turns the offset by exactly the roll angle");
    }

    {
        // Litmus 4: world-space yaw while the camera looks straight down. Head
        // yaw is then a pure spin about the view axis, the world turns, and the
        // reticle must sit dead centre. A projection that treats head yaw as
        // camera-local sweeps it through an arc instead.
        const Matrix34f down = PitchedDown(90.0f);
        HeadPose pose; pose.yaw = 40.0f;
        const AimProjection aim = Project(down, pose, true);
        Check(failures, aim.inFront && NearEqual(aim.tanRight, 0.0) && NearEqual(aim.tanUp, 0.0),
              "world-space yaw looking straight down spins the world, not the reticle");
    }

    {
        // World yaw at the horizon has to behave like local yaw, or the mode
        // switch itself would move the reticle.
        HeadPose pose; pose.yaw = 20.0f;
        const AimProjection world = Project(level, pose, true);
        const AimProjection local = Project(level, pose, false);
        Check(failures, NearEqual(world.tanRight, local.tanRight)
                     && NearEqual(world.tanUp, local.tanUp),
              "at the horizon the two yaw modes project identically");
    }

    {
        // Past a right angle the aim direction is behind the picture. Hiding the
        // reticle is the only honest answer; clamping it to an edge points the
        // player away from where the shot goes.
        HeadPose pose; pose.yaw = 100.0f;
        const AimProjection aim = Project(level, pose, false);
        Check(failures, !aim.inFront, "an aim point behind the view is reported as not drawable");
    }

    {
        // Screen mapping. A vertical FOV of 90 degrees makes tan(half) exactly
        // 1, so an offset of 1 lands on the top edge and the horizontal offset
        // is divided by the projection ratio.
        AimProjection aim; aim.inFront = true; aim.tanRight = 0.0f; aim.tanUp = 1.0f;
        const ScreenPoint top = ToScreen(aim, 1920.0f, 1080.0f, 1.57079632679f, 16.0f / 9.0f);
        Check(failures, NearEqual(top.x, 960.0) && NearEqual(top.y, 0.0),
              "a full up-offset maps to the top edge, horizontally centred");

        aim.tanUp = 0.0f;
        aim.tanRight = 16.0f / 9.0f;
        const ScreenPoint rightEdge = ToScreen(aim, 1920.0f, 1080.0f, 1.57079632679f, 16.0f / 9.0f);
        Check(failures, NearEqual(rightEdge.x, 1920.0) && NearEqual(rightEdge.y, 540.0),
              "the projection ratio widens the horizontal mapping, not the vertical");

        // Same head pose on an ultrawide: the reticle sits closer to centre
        // horizontally because the same angle covers fewer degrees of the
        // wider picture. Read from the live camera, so no calibration constant.
        aim.tanRight = 0.5f;
        const ScreenPoint wide = ToScreen(aim, 3840.0f, 1080.0f, 1.57079632679f, 32.0f / 9.0f);
        const ScreenPoint normal = ToScreen(aim, 1920.0f, 1080.0f, 1.57079632679f, 16.0f / 9.0f);
        const double wideFraction = (wide.x - 1920.0) / 1920.0;
        const double normalFraction = (normal.x - 960.0) / 960.0;
        Check(failures, NearEqual(wideFraction, normalFraction * 0.5),
              "doubling the aspect halves the reticle's fraction of the half-width");
    }

    {
        // The field of view the mod can now override is read off the live camera
        // for exactly this reason: the same aim direction sits further from
        // centre in a narrower picture. A reticle projected through a fixed 65
        // degrees while the game renders 90 would sit 40% too far out.
        AimProjection aim; aim.inFront = true; aim.tanRight = 0.2f; aim.tanUp = 0.0f;
        const float ratio = 16.0f / 9.0f;
        const ScreenPoint narrow =
            ToScreen(aim, 1920.0f, 1080.0f, static_cast<float>(65.0 * kDegToRad), ratio);
        const ScreenPoint wide =
            ToScreen(aim, 1920.0f, 1080.0f, static_cast<float>(90.0 * kDegToRad), ratio);

        const double expectedNarrow =
            960.0 + (0.2 / (std::tan(65.0 * kDegToRad * 0.5) * (16.0 / 9.0))) * 960.0;
        Check(failures, NearEqual(narrow.x, expectedNarrow),
              "the reticle offset is the aim tangent over the live half-FOV tangent");
        Check(failures, narrow.x - 960.0 > (wide.x - 960.0) * 1.35,
              "widening the field of view from 65 to 90 pulls the reticle back towards centre");
    }

    {
        // Yaw combined with pitch. Every other case here is a single axis or
        // pitch with roll, and swapping the yaw and pitch factors in the camera
        // composition passes all of those while putting the reticle 58 px out at
        // 25 degrees of each. The expected offset comes from the composition
        // itself: aim in the tracked frame is (Yaw*Pitch)^T applied to forward,
        // which for engine yaw y and pitch p is (sin y, cos y cos p, -cos y sin p),
        // so the offsets are tan(y)/cos(p) across and -tan(p) up. The asymmetry
        // is the point: the yaw offset grows with pitch, the pitch offset does
        // not grow with yaw, and a composition with those two swapped passes
        // every other test in this file.
        HeadPose pose;
        pose.yaw = 25.0f;
        pose.pitch = 25.0f;
        const AimProjection aim = Project(level, pose, false);

        const double y = -25.0 * kDegToRad;   // the boundary negates yaw
        const double p = 25.0 * kDegToRad;
        const double wantRight = std::tan(y) / std::cos(p);
        const double wantUp = -std::tan(p);

        Check(failures, aim.inFront, "a combined yaw and pitch keeps the aim in front");
        Check(failures, NearEqual(aim.tanRight, wantRight, 1e-4),
              "combined yaw and pitch: horizontal offset matches the composition");
        Check(failures, NearEqual(aim.tanUp, wantUp, 1e-4),
              "combined yaw and pitch: vertical offset matches the composition");
    }

    {
        // The offset form the HUD cursor helper takes, and the clamp that keeps
        // a runaway projection inside the frame. Without it an 89.4 degree head
        // turn hands the HUD a cursor 80,000 px off screen.
        AimProjection aim; aim.inFront = true; aim.tanRight = 0.0f; aim.tanUp = 0.0f;
        const ScreenOffset centred =
            ClampedCentreOffset(aim, 1920.0f, 1080.0f, 1.57079632679f, 16.0f / 9.0f, 8.0f);
        Check(failures, NearEqual(centred.dx, 0.0) && NearEqual(centred.dy, 0.0),
              "an aim at the view centre asks for no cursor movement at all");

        aim.tanRight = 0.5f;
        const ScreenOffset moved =
            ClampedCentreOffset(aim, 1920.0f, 1080.0f, 1.57079632679f, 16.0f / 9.0f, 8.0f);
        const ScreenPoint point =
            ToScreen(aim, 1920.0f, 1080.0f, 1.57079632679f, 16.0f / 9.0f);
        Check(failures, NearEqual(moved.dx, point.x - 960.0) && NearEqual(moved.dy, point.y - 540.0),
              "an in-frame offset is exactly the projected point minus the centre");

        aim.tanRight = 60.0f;
        aim.tanUp = -60.0f;
        const ScreenOffset runaway =
            ClampedCentreOffset(aim, 1920.0f, 1080.0f, 1.57079632679f, 16.0f / 9.0f, 8.0f);
        Check(failures, NearEqual(runaway.dx, 952.0) && NearEqual(runaway.dy, 532.0),
              "a runaway projection is held one margin inside the frame, still pointing out of it");

        aim.tanRight = -60.0f;
        aim.tanUp = 60.0f;
        const ScreenOffset opposite =
            ClampedCentreOffset(aim, 1920.0f, 1080.0f, 1.57079632679f, 16.0f / 9.0f, 8.0f);
        Check(failures, NearEqual(opposite.dx, -952.0) && NearEqual(opposite.dy, -532.0),
              "the clamp is symmetric about the frame centre");
    }

    return kcd_tests::Report("Aim projection", failures);
}
