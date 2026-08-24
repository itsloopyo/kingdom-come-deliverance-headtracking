#pragma once

// The slice of CryEngine's math types this mod touches, laid out to match the
// memory the engine actually writes. Nothing here is engine source; the layout
// is a measurement, recorded so the mod can read and write the same twelve
// floats the camera occupies.
//
// CryEngine is right-handed and Z-up. A camera's Matrix34 holds its basis in the
// COLUMNS: column 0 is right (+X), column 1 is FORWARD (+Y), column 2 is up
// (+Z), column 3 is the world position. That is the one fact the whole injection
// rests on, and it is why yaw rotates about Z and roll about Y here rather than
// the Y/Z a Y-up engine would use.

namespace kcd_ht
{
    struct Vec3f
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // Row-major 3x4, stored exactly as CryEngine stores it: m[row][col], with
    // the translation in column 3. CView::Update writes these twelve floats at
    // CView+0xE8 one at a time, in this order.
    struct Matrix34f
    {
        float m[3][4]{};

        Vec3f Column(int c) const { return { m[0][c], m[1][c], m[2][c] }; }

        void SetColumn(int c, const Vec3f& v)
        {
            m[0][c] = v.x;
            m[1][c] = v.y;
            m[2][c] = v.z;
        }

        Vec3f Translation() const { return Column(3); }
        void SetTranslation(const Vec3f& v) { SetColumn(3, v); }
    };

    // Identity-initialised 3x3 rotation, used only as the intermediate for
    // composing the head rotation onto a camera basis.
    struct Matrix33f
    {
        float m[3][3]{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    };
}
