#include "test_support.h"

#include "view_injection.h"

#include <cmath>
#include <limits>

using kcd_ht::ApplyHeadPose;
using kcd_ht::HeadPose;
using kcd_ht::IsFinitePose;
using kcd_ht::Matrix34f;
using kcd_ht::Vec3f;
using kcd_tests::Check;
using kcd_tests::NearEqual;

namespace
{
    // CryEngine camera basis: column 0 right (+X), column 1 forward (+Y),
    // column 2 up (+Z), column 3 world position.
    Matrix34f IdentityAt(float x, float y, float z)
    {
        Matrix34f m;
        m.m[0][0] = 1.0f; m.m[1][1] = 1.0f; m.m[2][2] = 1.0f;
        m.m[0][3] = x; m.m[1][3] = y; m.m[2][3] = z;
        return m;
    }

    Vec3f Forward(const Matrix34f& m) { return m.Column(1); }
    Vec3f Right(const Matrix34f& m) { return m.Column(0); }
    Vec3f Up(const Matrix34f& m) { return m.Column(2); }

    bool IsOrthonormal(const Matrix34f& m)
    {
        const Vec3f cols[3] = { m.Column(0), m.Column(1), m.Column(2) };
        for (int i = 0; i < 3; ++i)
        {
            const double len = std::sqrt(static_cast<double>(
                cols[i].x * cols[i].x + cols[i].y * cols[i].y + cols[i].z * cols[i].z));
            if (!NearEqual(len, 1.0, 1e-4)) return false;
            for (int j = i + 1; j < 3; ++j)
            {
                const double dot = static_cast<double>(
                    cols[i].x * cols[j].x + cols[i].y * cols[j].y + cols[i].z * cols[j].z);
                if (!NearEqual(dot, 0.0, 1e-4)) return false;
            }
        }
        return true;
    }
}

int RunViewInjectionTests()
{
    int failures = 0;
    std::cout << "View injection\n";

    const Matrix34f base = IdentityAt(10.0f, 20.0f, 30.0f);

    {
        HeadPose zero;
        const Matrix34f out = ApplyHeadPose(base, zero, true, true);
        bool same = true;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c)
                same = same && NearEqual(out.m[r][c], base.m[r][c]);
        Check(failures, same, "a zero pose leaves the camera matrix untouched");
    }

    {
        // Yaw is about the camera's up axis (+Z) and the boundary negates it, so
        // a +90 tracker yaw swings the forward axis from +Y towards +X.
        HeadPose pose; pose.yaw = 90.0f;
        const Matrix34f out = ApplyHeadPose(base, pose, false, false);
        const Vec3f f = Forward(out);
        Check(failures, NearEqual(f.x, 1.0) && NearEqual(f.y, 0.0) && NearEqual(f.z, 0.0),
              "yaw +90 turns the view right about the up axis");
        Check(failures, IsOrthonormal(out), "yaw keeps the basis orthonormal");
    }

    {
        // Pitch is about the camera's right axis (+X) and positive raises the
        // view, so forward tilts from +Y towards +Z.
        HeadPose pose; pose.pitch = 90.0f;
        const Matrix34f out = ApplyHeadPose(base, pose, false, false);
        const Vec3f f = Forward(out);
        Check(failures, NearEqual(f.x, 0.0) && NearEqual(f.y, 0.0) && NearEqual(f.z, 1.0),
              "pitch +90 raises the view about the right axis");
    }

    {
        // Roll is about the camera's forward axis (+Y) and the boundary negates
        // it: forward is unchanged and the up axis swings towards -X.
        HeadPose pose; pose.roll = 90.0f;
        const Matrix34f out = ApplyHeadPose(base, pose, false, false);
        const Vec3f f = Forward(out);
        const Vec3f u = Up(out);
        Check(failures, NearEqual(f.x, 0.0) && NearEqual(f.y, 1.0) && NearEqual(f.z, 0.0),
              "roll leaves the forward axis alone");
        Check(failures, NearEqual(u.x, -1.0) && NearEqual(u.z, 0.0),
              "roll +90 tips the up axis towards -X");
        Check(failures, IsOrthonormal(out), "roll keeps the basis orthonormal");
    }

    {
        // The whole point of the mod: the camera moves, the camera POSITION does
        // not, so nothing that reads the view origin sees a rotation.
        HeadPose pose; pose.yaw = 25.0f; pose.pitch = -12.0f; pose.roll = 7.0f;
        const Matrix34f out = ApplyHeadPose(base, pose, false, false);
        const Vec3f t = out.Translation();
        Check(failures, NearEqual(t.x, 10.0) && NearEqual(t.y, 20.0) && NearEqual(t.z, 30.0),
              "rotation alone never moves the camera position");
    }

    {
        // Position is disabled -> the offset is ignored entirely.
        HeadPose pose; pose.x = 0.1f; pose.y = 0.2f; pose.z = -0.3f;
        const Matrix34f out = ApplyHeadPose(base, pose, true, false);
        const Vec3f t = out.Translation();
        Check(failures, NearEqual(t.x, 10.0) && NearEqual(t.y, 20.0) && NearEqual(t.z, 30.0),
              "position offset is ignored when position tracking is off");
    }

    {
        // The core's z is NEGATIVE forward. A forward lean must move the camera
        // along its own forward axis (+Y here), not backwards - the classic
        // mirrored-Z bug.
        HeadPose pose; pose.z = -0.4f;
        const Matrix34f out = ApplyHeadPose(base, pose, true, true);
        const Vec3f t = out.Translation();
        Check(failures, NearEqual(t.y, 20.4), "a forward lean moves the camera forward");
        Check(failures, NearEqual(t.x, 10.0) && NearEqual(t.z, 30.0),
              "a forward lean does not move the other axes");
    }

    {
        // x is negated at the boundary alongside z, so a +x tracker offset moves
        // the camera along its own -right axis (-X for this identity basis).
        HeadPose pose; pose.x = 0.3f; pose.y = 0.2f;
        const Matrix34f out = ApplyHeadPose(base, pose, true, true);
        const Vec3f t = out.Translation();
        Check(failures, NearEqual(t.x, 9.7), "+x leans the camera left");
        Check(failures, NearEqual(t.z, 30.2), "+y raises the camera");
    }

    {
        // The offset goes through the ORIGINAL basis, so a camera facing +X gets
        // its forward lean along +X.
        Matrix34f turned;
        turned.SetColumn(0, Vec3f{ 0.0f, -1.0f, 0.0f });  // right
        turned.SetColumn(1, Vec3f{ 1.0f, 0.0f, 0.0f });   // forward = world +X
        turned.SetColumn(2, Vec3f{ 0.0f, 0.0f, 1.0f });   // up
        turned.SetTranslation(Vec3f{ 0.0f, 0.0f, 0.0f });

        HeadPose pose; pose.z = -0.5f;
        const Matrix34f out = ApplyHeadPose(turned, pose, true, true);
        const Vec3f t = out.Translation();
        Check(failures, NearEqual(t.x, 0.5) && NearEqual(t.y, 0.0),
              "the lean follows where the camera is facing, not world axes");
    }

    {
        // The lean goes through the ORIGINAL basis, so it must not pick up the
        // head rotation. Without that, a yawed head sends the lean off along the
        // wrong world axis - and every other position test here uses a zero
        // rotation, so nothing else would notice.
        HeadPose pose;
        pose.yaw = 90.0f;
        pose.x = 0.30f;
        const Matrix34f out = ApplyHeadPose(base, pose, false, true);
        const Vec3f t = out.Translation();
        Check(failures, NearEqual(t.x, 9.7) && NearEqual(t.y, 20.0) && NearEqual(t.z, 30.0),
              "a lean under a yawed head still follows the body, not the head");
    }

    {
        // World-space yaw on a pitched camera must keep the horizon level: the
        // resulting right axis stays in the world XY plane.
        Matrix34f pitched;
        const float s = 0.70710678f;
        pitched.SetColumn(0, Vec3f{ 1.0f, 0.0f, 0.0f });
        pitched.SetColumn(1, Vec3f{ 0.0f, s, s });
        pitched.SetColumn(2, Vec3f{ 0.0f, -s, s });
        pitched.SetTranslation(Vec3f{ 0.0f, 0.0f, 0.0f });

        HeadPose pose; pose.yaw = 30.0f;
        const Matrix34f world = ApplyHeadPose(pitched, pose, true, false);
        Check(failures, NearEqual(Right(world).z, 0.0),
              "world-space yaw keeps the horizon level on a pitched camera");
        Check(failures, IsOrthonormal(world), "world-space yaw keeps the basis orthonormal");

        const Matrix34f local = ApplyHeadPose(pitched, pose, false, false);
        Check(failures, !NearEqual(Right(local).z, 0.0),
              "camera-local yaw tips the horizon on a pitched camera");
    }

    {
        // The pose is derived from a UDP socket bound to every interface, and
        // one non-finite component turns the whole basis into NaN through
        // sin/asin. The hook drops a pose that fails this rather than handing
        // CCamera::UpdateFrustum a NaN matrix, so the predicate has to reject
        // every component independently and accept an ordinary pose untouched.
        HeadPose ordinary;
        ordinary.yaw = 25.0f; ordinary.pitch = -10.0f; ordinary.roll = 4.0f;
        ordinary.x = 0.05f; ordinary.y = -0.02f; ordinary.z = 0.10f;
        Check(failures, IsFinitePose(ordinary), "an ordinary tracker pose is finite");

        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        float HeadPose::* const components[] = {
            &HeadPose::yaw, &HeadPose::pitch, &HeadPose::roll,
            &HeadPose::x, &HeadPose::y, &HeadPose::z,
        };

        bool everyComponentRejected = true;
        for (float HeadPose::* const component : components)
        {
            HeadPose notANumber = ordinary;
            notANumber.*component = nan;
            HeadPose infinite = ordinary;
            infinite.*component = inf;
            everyComponentRejected = everyComponentRejected
                                  && !IsFinitePose(notANumber) && !IsFinitePose(infinite);
        }
        Check(failures, everyComponentRejected,
              "a NaN or infinity in any single component rejects the whole pose");

        // What the overflow actually produces: two samples near the float
        // maximum make the interpolator's (to - from) an infinity, and pitch is
        // the axis that never gets wrapped on the way through. volatile so the
        // sum happens at run time the way it does in the interpolator; folded at
        // compile time it is C4756, which /WX turns into a build failure.
        volatile float largest = std::numeric_limits<float>::max();
        HeadPose overflowed = ordinary;
        overflowed.pitch = largest + largest;
        Check(failures, !IsFinitePose(overflowed),
              "a pitch that overflowed the interpolator is rejected");
    }

    return kcd_tests::Report("View injection", failures);
}
