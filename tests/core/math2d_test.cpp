extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Math2D.hpp"

// NOTE: kronklab test names are limited to 31 characters.

Test(math2d, vector_arithmetic)
{
    const kuge::Vec2 a(1.0f, 2.0f);
    const kuge::Vec2 b(3.0f, -4.0f);

    Assert(a + b == kuge::Vec2(4.0f, -2.0f), "add");
    Assert(a - b == kuge::Vec2(-2.0f, 6.0f), "subtract");
    Assert(a * 2.0f == kuge::Vec2(2.0f, 4.0f), "scale");
    Assert(2.0f * a == kuge::Vec2(2.0f, 4.0f), "scale from the left");
    Assert(b / 2.0f == kuge::Vec2(1.5f, -2.0f), "divide");
    Assert(-a == kuge::Vec2(-1.0f, -2.0f), "negate");
    AssertEq(a.dot(b), -5.0f, "dot");
    kuge::Vec2 c = a;
    c += b;
    c -= kuge::Vec2(1.0f, 1.0f);
    c *= 2.0f;
    Assert(c == kuge::Vec2(6.0f, -6.0f), "compound operators");
}

Test(math2d, length_and_direction)
{
    const kuge::Vec2 v(3.0f, 4.0f);

    AssertEq(v.length(), 5.0f, "3-4-5");
    AssertEq(v.lengthSquared(), 25.0f, "squared");
    Assert(v.normalized() == kuge::Vec2(0.6f, 0.8f), "direction");
    Assert(kuge::Vec2().normalized() == kuge::Vec2(), "the zero vector stays zero, not NaN");
}

Test(math2d, lerp_goes_from_a_to_b)
{
    const kuge::Vec2 a(0.0f, 10.0f);
    const kuge::Vec2 b(10.0f, 20.0f);

    Assert(kuge::Vec2::lerp(a, b, 0.0f) == a, "t = 0 is a");
    Assert(kuge::Vec2::lerp(a, b, 1.0f) == b, "t = 1 is b");
    Assert(kuge::Vec2::lerp(a, b, 0.5f) == kuge::Vec2(5.0f, 15.0f), "halfway");
}

Test(math2d, rect_contains_and_meets)
{
    const kuge::Rect r{10.0f, 20.0f, 30.0f, 40.0f};

    AssertEq(r.right(), 40.0f, "right");
    AssertEq(r.bottom(), 60.0f, "bottom");
    Assert(r.center() == kuge::Vec2(25.0f, 40.0f), "center");
    Assert(r.contains({10.0f, 20.0f}), "the top-left corner is inside");
    Assert(!r.contains({40.0f, 30.0f}), "the right edge is outside");
    Assert(!r.contains({25.0f, 60.0f}), "the bottom edge is outside");
    Assert(r.intersects({30.0f, 50.0f, 20.0f, 20.0f}), "overlapping");
    Assert(!r.intersects({40.0f, 20.0f, 5.0f, 5.0f}), "sharing an edge is not overlapping");
    Assert(!r.intersects({100.0f, 100.0f, 5.0f, 5.0f}), "far away");
    Assert(r.intersects({0.0f, 0.0f, 100.0f, 100.0f}), "one inside the other");
}
