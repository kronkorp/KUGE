extern "C" {
    #include "kronklab/kronklab.h"
}
#include "sim3.hpp"

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using namespace sim3;

    constexpr float STEP = 0.1f;   // meters a tick

    struct Wish { float x = 0.0f; float z = 0.0f; };

    kw::Entity buildPlayer3(kw::World& world)
    {
        const kw::Entity e = world.create();

        world.add<kuge::Transform3D>(e, kuge::Transform3D{});
        world.add<Wish>(e, Wish{});
        return e;
    }

    void steer3(kw::World& world, kw::Entity e, const Steer3& steer)
    {
        world.get<Wish>(e) = Wish{steer.dx * STEP, steer.dz * STEP};
    }

    void step3(kw::World& world, double)
    {
        for (kw::Entity e : world.view<kuge::Transform3D, Wish>()) {
            world.get<kuge::Transform3D>(e).translation.x += world.get<Wish>(e).x;
            world.get<kuge::Transform3D>(e).translation.z += world.get<Wish>(e).z;
        }
    }


    sim3::Game3 simpleGame(void)
    {
        return sim3::Game3{&buildPlayer3, &steer3, &step3, false};
    }
}

Test(replication3d, a_transform_arrives)
{
    Sim3 sim({}, simpleGame());
    const kw::Entity prop = sim.addProp({1.0f, 2.0f, 3.0f});
    auto& t = sim.serverWorld.get<kuge::Transform3D>(prop);

    t.rotation = kuge::axisAngle({0.0f, 1.0f, 0.0f}, 1.0f);
    t.scale = {2.0f, 3.0f, 4.0f};
    sim.run(30);
    const kuge::Transform3D& got = sim.clientWorld.get<kuge::Transform3D>(sim.onClient(prop));

    Assert(got == t, "position, rotation and scale, as the server has them");
}

Test(replication3d, motion_is_smooth)
{
    Sim3 sim(kuge::net::LoopbackNetwork::Conditions{.latency = 0.03, .seed = 2}, simpleGame());
    const kw::Entity prop = sim.addProp({0.0f, 0.0f, 0.0f});
    float last = 0.0f;
    float longest = 0.0f;

    sim.run(30);
    for (int t = 0; t < 120; ++t) {
        sim.serverWorld.get<kuge::Transform3D>(prop).translation.x += 0.05f;
        sim.step();
        const float x = sim.clientWorld.get<kuge::Transform3D>(sim.onClient(prop)).translation.x;

        Assert(x >= last - 1e-5f, "never goes back: %f after %f", x, last);
        longest = std::max(longest, x - last);
        last = x;
    }
    Assert(last > 4.0f && last <= sim.serverWorld.get<kuge::Transform3D>(prop).translation.x, "it follows, a little behind: %f", last);
    Assert(longest < 0.2f, "in small steps: the longest is %f", longest);
    sim.run(60);
    Assert(std::fabs(sim.clientWorld.get<kuge::Transform3D>(sim.onClient(prop)).translation.x - 6.0f) < 1e-3f, "and gets there");
}

Test(replication3d, a_clip_arrives)
{
    Sim3 sim({}, simpleGame());
    const kw::Entity prop = sim.addProp({0.0f, 0.0f, 0.0f});

    sim.serverWorld.add<kuge::AnimationState3D>(prop, kuge::AnimationState3D{"run", true, 1});
    sim.run(20);
    Assert(sim.clientWorld.get<kuge::AnimationState3D>(sim.onClient(prop)) == (kuge::AnimationState3D{"run", true, 1}), "the clip");
    sim.serverWorld.get<kuge::AnimationState3D>(prop) = kuge::AnimationState3D{"attack", false, 2};
    sim.run(20);
    Assert(sim.clientWorld.get<kuge::AnimationState3D>(sim.onClient(prop)) == (kuge::AnimationState3D{"attack", false, 2}), "and the next one");
}

Test(prediction3d, answers_at_once)
{
    Sim3 sim(kuge::net::LoopbackNetwork::Conditions{.latency = 0.05, .seed = 1}, simpleGame());

    sim.run(30);
    Assert(sim.prediction->ready(), "the server has shown the player");
    const float before = sim.clientPosition().x;

    sim.step(Steer3{1, 0});
    Assert(std::fabs(sim.clientPosition().x - before - STEP) < 1e-4f, "one tick: the player moved 0.1 m, not waiting for the server");
    sim.run(30, Steer3{1, 0});
    Assert(sim.clientPosition().x > before + 3.0f, "and goes on");
    sim.run(60);
    Assert(distance(sim.clientPosition(), sim.serverPosition()) < 1e-3f, "in the end the client and the server agree");
    Assert(sim.prediction->stats().corrections * 20 < sim.prediction->stats().reconciliations,
        "the prediction is right nearly always: %llu corrections in %llu snapshots",
        static_cast<unsigned long long>(sim.prediction->stats().corrections),
        static_cast<unsigned long long>(sim.prediction->stats().reconciliations));
}

Test(prediction3d, a_bad_network)
{
    for (std::uint64_t seed = 1; seed <= 3; ++seed) {
        Sim3 sim(kuge::net::LoopbackNetwork::Conditions{.loss = 0.1, .duplicate = 0.05, .latency = 0.05, .jitter = 0.02, .seed = seed}, simpleGame());

        sim.run(30);
        for (int t = 0; t < 400; ++t) {
            sim.step(Steer3{static_cast<std::int8_t>(t % 100 < 50 ? 1 : -1), static_cast<std::int8_t>(t % 60 < 30 ? 0 : 1)});
        }
        sim.run(120);
        Assert(distance(sim.clientPosition(), sim.serverPosition()) < 1e-3f, "seed %llu: it ends where the server has it", static_cast<unsigned long long>(seed));
        AssertEq(sim.prediction->stats().snaps, 0u, "never put back by force");
    }
}

Test(prediction3d, corrections_are_spread)
{
    Sim3 sim(kuge::net::LoopbackNetwork::Conditions{.latency = 0.03, .seed = 4}, simpleGame());

    sim.run(30);
    sim.serverWorld.get<kuge::Transform3D>(sim.serverPlayer).translation.y += 0.5f;   // something the client cannot know
    const std::size_t from = sim.shown.size();

    sim.run(90);
    float longest = 0.0f;

    for (std::size_t t = from + 1; t < sim.shown.size(); ++t) {
        longest = std::max(longest, distance(sim.shown[t], sim.shown[t - 1]));
    }
    Assert(sim.prediction->stats().corrections > 0, "the client was corrected");
    Assert(longest < 0.2f, "without a jump: the longest step is %f", longest);
    AssertEq(sim.prediction->stats().snaps, 0u, "not a teleport");
    Assert(distance(sim.clientPosition(), sim.serverPosition()) < 1e-3f, "and in the end it agrees");
}

Test(prediction3d, too_far_is_a_snap)
{
    Sim3 sim(kuge::net::LoopbackNetwork::Conditions{.latency = 0.03, .seed = 4}, simpleGame());

    sim.run(30);
    sim.serverWorld.get<kuge::Transform3D>(sim.serverPlayer).translation.z += 100.0f;   // a respawn, far away
    sim.run(60);
    AssertEq(sim.prediction->stats().snaps, 1u, "put there at once");
    Assert(distance(sim.clientPosition(), sim.serverPosition()) < 1e-3f, "and it agrees");
}
