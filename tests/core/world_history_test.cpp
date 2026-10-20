extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Transform3D.hpp"
#include "WorldHistory.hpp"
#include <stdexcept>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    struct Health { int points = 100; };

    // An entity at x = 10 * seconds, one tick being 1/60 of a second (it "runs" at 10 m/s)
    struct Course
    {
        kw::World         world;
        kuge::WorldHistory history;
        kw::Entity        runner;
        kw::Entity        still;

        explicit Course(kuge::WorldHistory::Config config = {}) : history(std::move(config))
        {
            history.keep<kuge::Transform3D>([](const kuge::Transform3D& a, const kuge::Transform3D& b, float t) { return kuge::lerp(a, b, t); });
            runner = world.create();
            still = world.create();
            world.add<kuge::Transform3D>(runner, kuge::Transform3D{});
            world.add<kuge::Transform3D>(still, kuge::Transform3D{});
            world.get<kuge::Transform3D>(still).translation = {5.0f, 0.0f, 0.0f};
        }

        void tick(std::uint32_t number)
        {
            world.get<kuge::Transform3D>(runner).translation.x = 10.0f * static_cast<float>(number) / 60.0f;
            history.record(world, number);
        }

        float x(kw::Entity entity) { return world.get<kuge::Transform3D>(entity).translation.x; }
    };
}

Test(history, the_world_as_it_was)
{
    Course course;

    for (std::uint32_t t = 1; t <= 12; ++t) {
        course.tick(t);
    }
    Assert(std::fabs(course.x(course.runner) - 2.0f) < 1e-4f, "now at 2 m");
    float seen = -1.0f, other = -1.0f;
    const bool ok = course.history.rewound(course.world, 6.0, [&](const kuge::WorldHistory::Rewound&) {
        seen = course.x(course.runner);
        other = course.x(course.still);
    });

    Assert(ok, "a tick of the window can be looked at");
    Assert(std::fabs(seen - 1.0f) < 1e-4f, "6 ticks ago it was at 1 m: %f", seen);
    Assert(other == 5.0f, "what did not move is where it was");
    Assert(std::fabs(course.x(course.runner) - 2.0f) < 1e-4f, "and everything is back after the call");
}

Test(history, between_two_ticks)
{
    Course course;

    for (std::uint32_t t = 1; t <= 12; ++t) {
        course.tick(t);
    }
    float seen = -1.0f;

    course.history.rewound(course.world, 6.5, [&](const auto&) { seen = course.x(course.runner); });
    Assert(std::fabs(seen - 10.0f * 6.5f / 60.0f) < 1e-4f, "half a tick: blended with the lerp: %f", seen);
    kuge::WorldHistory plain;
    float steps = -1.0f;

    plain.keep<Health>();   // no lerp: the nearest one is used
    kw::World world;
    const kw::Entity e = world.create();

    world.add<Health>(e, Health{10});
    plain.record(world, 1);
    world.get<Health>(e).points = 20;
    plain.record(world, 2);
    world.get<Health>(e).points = 99;
    plain.rewound(world, 1.2, [&](const auto&) { steps = static_cast<float>(world.get<Health>(e).points); });
    AssertEq(steps, 10.0f, "without a lerp: the nearest tick (1.2 is closer to 1)");
    plain.rewound(world, 1.8, [&](const auto&) { steps = static_cast<float>(world.get<Health>(e).points); });
    AssertEq(steps, 20.0f, "1.8 is closer to 2");
    AssertEq(world.get<Health>(e).points, 99, "and it is back");
}

Test(history, the_window_is_bounded)
{
    Course course({.windowTicks = 5});

    for (std::uint32_t t = 1; t <= 20; ++t) {
        course.tick(t);
    }
    AssertEq(course.history.size(), 5u, "5 ticks kept");
    AssertEq(*course.history.oldest(), 16u, "the oldest is 16");
    bool ran = false;

    Assert(!course.history.rewound(course.world, 10.0, [&](const auto&) { ran = true; }) && !ran, "older than the window: fn does not run");
    Assert(!course.history.rewound(course.world, 21.0, [&](const auto&) { ran = true; }) && !ran, "nor a tick that is not recorded yet");
    Assert(course.history.rewound(course.world, 16.0, [&](const auto&) { ran = true; }) && ran, "the oldest one is fine");
}

Test(history, fn_may_throw)
{
    Course course;

    for (std::uint32_t t = 1; t <= 12; ++t) {
        course.tick(t);
    }
    bool threw = false;

    try {
        course.history.rewound(course.world, 3.0, [&](const auto&) {
            if (std::fabs(course.x(course.runner) - 0.5f) < 1e-4f) {
                throw std::runtime_error("the callback fails");
            }
        });
    } catch (const std::runtime_error&) {
        threw = true;
    }
    Assert(threw, "the exception goes on");
    Assert(std::fabs(course.x(course.runner) - 2.0f) < 1e-4f, "but the entities are back where they are now: %f", course.x(course.runner));
}

Test(history, born_since_and_gone)
{
    Course course({.windowTicks = 30});

    for (std::uint32_t t = 1; t <= 6; ++t) {
        course.tick(t);
    }
    const kw::Entity latecomer = course.world.create();

    course.world.add<kuge::Transform3D>(latecomer, kuge::Transform3D{});
    for (std::uint32_t t = 7; t <= 12; ++t) {
        course.tick(t);
    }
    bool before = false, after = false;

    course.history.rewound(course.world, 3.0, [&](const kuge::WorldHistory::Rewound& past) {
        before = past.existed(course.runner);
        after = past.existed(latecomer);
    });
    Assert(before && !after, "an entity made since is not there");
    course.world.remove(course.still);   // gone since
    bool ran = course.history.rewound(course.world, 3.0, [&](const auto&) {});

    Assert(ran, "an entity that is gone does not stop the others from being rewound");
}

Test(history, a_name_that_is_not_reused)
{
    // The World gives the number of a removed entity to the next one; the game names entities by something else
    struct Name { std::uint64_t id; };
    kuge::WorldHistory history({.windowTicks = 10, .identity = [](kw::World& w, kw::Entity e) { return w.get<Name>(e).id; }});
    kw::World world;

    history.keep<kuge::Transform3D>();
    const kw::Entity first = world.create();

    world.add<Name>(first, Name{1});
    world.add<kuge::Transform3D>(first, kuge::Transform3D{});
    world.get<kuge::Transform3D>(first).translation.x = 7.0f;
    history.record(world, 1);
    history.record(world, 2);
    world.remove(first);
    const kw::Entity second = world.create();   // probably the same number

    world.add<Name>(second, Name{2});
    world.add<kuge::Transform3D>(second, kuge::Transform3D{});
    world.get<kuge::Transform3D>(second).translation.x = 100.0f;
    float seen = 0.0f;
    bool present = true;

    history.rewound(world, 1.0, [&](const kuge::WorldHistory::Rewound& past) {
        seen = world.get<kuge::Transform3D>(second).translation.x;
        present = past.existed(second);
    });
    Assert(seen == 100.0f && !present, "the new entity does not get the old one's place: %f", seen);
}

Test(history, how_far_back_is_believable)
{
    kuge::WorldHistory history({.windowTicks = 30});
    kw::World world;

    history.keep<kuge::Transform3D>();
    for (std::uint32_t t = 1; t <= 60; ++t) {
        history.record(world, t);
    }
    // 60 ticks a second, 100 ms of interpolation, 50 ms of round trip: about 9 ticks, and a tick of slack
    const auto honest = history.limit(50.0, 60, 0.05, 60, 0.1);
    const auto liar = history.limit(35.0, 60, 0.05, 60, 0.1);
    const auto old = history.limit(10.0, 60, 0.05, 60, 0.1);
    const auto future = history.limit(80.0, 60, 0.05, 60, 0.1);

    Assert(honest.verdict == kuge::WorldHistory::Limit::Verdict::Ok && honest.tick == 50.0, "50 is believable");
    Assert(liar.verdict == kuge::WorldHistory::Limit::Verdict::Clamped && liar.tick > 49.0 && liar.tick < 51.0, "35 is too far for that round trip: brought to %f", liar.tick);
    Assert(old.verdict == kuge::WorldHistory::Limit::Verdict::TooOld, "older than the window is refused");
    Assert(future.verdict == kuge::WorldHistory::Limit::Verdict::Future, "the future is refused");
    const auto laggy = history.limit(35.0, 60, 0.4, 60, 0.1);

    Assert(laggy.verdict == kuge::WorldHistory::Limit::Verdict::Ok, "a player with 400 ms of round trip may look further back (the window bounds it)");
}
