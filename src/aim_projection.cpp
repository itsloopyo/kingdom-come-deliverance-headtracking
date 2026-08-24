#include "aim_projection.h"

#include <cmath>

#include <cameraunlock/math/angle_utils.h>

namespace kcd_ht
{
    namespace
    {
        using cameraunlock::math::Clamp;

        float Dot(const Vec3f& a, const Vec3f& b)
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        // Below this the aim direction is within a degree or so of the tracked
        // view plane and the perspective divide runs away.
        constexpr float kMinDepth = 0.01f;
    }

    AimProjection ProjectAim(const Matrix34f& clean, const Matrix34f& tracked)
    {
        // CryEngine keeps the camera basis in the columns: 0 right, 1 forward,
        // 2 up.
        const Vec3f aim = clean.Column(1);
        const Vec3f right = tracked.Column(0);
        const Vec3f forward = tracked.Column(1);
        const Vec3f up = tracked.Column(2);

        AimProjection out;
        const float depth = Dot(aim, forward);
        if (!(depth > kMinDepth)) return out;

        out.tanRight = Dot(aim, right) / depth;
        out.tanUp = Dot(aim, up) / depth;
        out.inFront = true;
        return out;
    }

    ScreenPoint ToScreen(const AimProjection& aim, float screenWidth, float screenHeight,
                         float fovRadians, float projectionRatio)
    {
        const float halfW = screenWidth * 0.5f;
        const float halfH = screenHeight * 0.5f;

        const float tanHalfV = std::tan(fovRadians * 0.5f);
        const float tanHalfH = tanHalfV * projectionRatio;

        ScreenPoint out;
        out.x = halfW + (aim.tanRight / tanHalfH) * halfW;
        out.y = halfH - (aim.tanUp / tanHalfV) * halfH;
        return out;
    }

    ScreenOffset ClampedCentreOffset(const AimProjection& aim,
                                     float screenWidth, float screenHeight,
                                     float fovRadians, float projectionRatio,
                                     float edgeMarginPixels)
    {
        const ScreenPoint point =
            ToScreen(aim, screenWidth, screenHeight, fovRadians, projectionRatio);

        const float halfW = screenWidth * 0.5f;
        const float halfH = screenHeight * 0.5f;
        const float limitX = halfW - edgeMarginPixels;
        const float limitY = halfH - edgeMarginPixels;

        ScreenOffset out;
        out.dx = Clamp(point.x - halfW, -limitX, limitX);
        out.dy = Clamp(point.y - halfH, -limitY, limitY);
        return out;
    }
}
