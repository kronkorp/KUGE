extern "C" {
    #include "kronklab/kronklab.h"
}
#include "FixedTimestep.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    bool near(double a, double b)
    {
        return std::fabs(a - b) < 1e-9;
    }
}

Test(timestep, one_second_is_tickrate)
{
    kuge::FixedTimestep step(60, 1000);

    AssertEq(step.advance(1.0), 60, "1 s at 60 Hz is 60 ticks");
    AssertEq(step.dropped(), 0, "nothing dropped");
    Assert(near(step.alpha(), 0.0), "nothing left over, alpha is %f", step.alpha());
}

Test(timestep, small_frames_add_up)
{
    kuge::FixedTimestep step(60, 1000);
    unsigned total = 0;

    // 240 frames of 1/240 s: the rounding of the additions must not lose a tick
    for (int i = 0; i < 240; ++i) {
        total += step.advance(1.0 / 240.0);
    }
    AssertEq(total, 60, "1 s in small frames is 60 ticks, got %u", total);
}

Test(timestep, alpha_is_what_is_left)
{
    kuge::FixedTimestep step(60, 10);

    AssertEq(step.advance(1.5 / 60.0), 1, "1 whole tick in 1.5");
    Assert(near(step.alpha(), 0.5), "half a tick left, alpha is %f", step.alpha());
    Assert(near(step.untilNextTick(), 0.5 / 60.0), "half a tick to wait");
}

Test(timestep, catch_up_is_capped)
{
    kuge::FixedTimestep step(60, 5);

    AssertEq(step.advance(1.0), 5, "at most 5 ticks in one loop");
    AssertEq(step.dropped(), 55, "the 55 others are dropped, got %lu", (unsigned long)step.dropped());
    AssertEq(step.advance(0.0), 0, "and not owed for later");
}

Test(timestep, bad_frame_times_count_zero)
{
    kuge::FixedTimestep step(60, 5);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    AssertEq(step.advance(-1.0), 0, "negative");
    AssertEq(step.advance(nan), 0, "NaN");
    AssertEq(step.advance(inf), 0, "infinite");
    AssertEq(step.advance(1.0 / 60.0), 1, "and it still works afterwards");
}

Test(timestep, invalid_arguments_throw)
{
    bool rate = false;
    bool cap = false;

    try { kuge::FixedTimestep step(0, 5); } catch (const std::invalid_argument&) { rate = true; }
    try { kuge::FixedTimestep step(60, 0); } catch (const std::invalid_argument&) { cap = true; }
    Assert(rate, "tickRate 0 must throw");
    Assert(cap, "maxCatchUp 0 must throw");
}
