extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Transform3D.hpp"
#include <cmath>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{

    constexpr float PI = 3.14159265358979f;

    kuge::Transform3D at(float x, float y, float z, float angle = 0.0f)
    {
        kuge::Transform3D t;

        t.translation = {x, y, z};
        t.rotation = kuge::axisAngle({0.0f, 1.0f, 0.0f}, angle);
        return t;
    }

}

Test(transform3d, default_is_identity)
{
    const kuge::Transform3D t;

    Assert(t == at(0.0f, 0.0f, 0.0f), "no move, no turn");
    AssertEq(t.scale.x + t.scale.y + t.scale.z, 3.0f, "scale 1");
    AssertEq(t.rotation.w, 1.0f, "unit quaternion");
}

Test(transform3d, axis_angle_is_a_unit)
{
    const auto q = kuge::axisAngle({0.0f, 2.0f, 0.0f}, PI / 2.0f);

    AssertEq(q.y, std::sin(PI / 4.0f), "any length of axis");
    AssertEq(q.w, std::cos(PI / 4.0f), "half the angle");
    AssertEq(kuge::axisAngle({0.0f, 0.0f, 0.0f}, 1.0f).w, 1.0f, "no axis: no turn");
}

Test(transform3d, lerp_halfway)
{
    const auto half = kuge::lerp(at(0.0f, 0.0f, 0.0f), at(2.0f, 4.0f, -6.0f, PI / 2.0f), 0.5f);

    AssertEq(half.translation.x, 1.0f, "x");
    AssertEq(half.translation.y, 2.0f, "y");
    AssertEq(half.translation.z, -3.0f, "z");
    Assert(std::fabs(half.rotation.y - std::sin(PI / 8.0f)) < 1e-5f, "a quarter turn, halfway");
    Assert(kuge::lerp(at(1.0f, 0.0f, 0.0f), at(5.0f, 0.0f, 0.0f), 0.0f) == at(1.0f, 0.0f, 0.0f), "t = 0");
}

Test(transform3d, lerp_short_way)
{
    // 350 and 10 degrees around y: through 0, not through 180
    const auto mid = kuge::lerp(at(0.0f, 0.0f, 0.0f, 350.0f * PI / 180.0f), at(0.0f, 0.0f, 0.0f, 10.0f * PI / 180.0f), 0.5f);

    Assert(std::fabs(mid.rotation.y) < 1e-5f, "no turn");
    Assert(std::fabs(std::fabs(mid.rotation.w) - 1.0f) < 1e-5f, "identity (or its opposite)");
}
