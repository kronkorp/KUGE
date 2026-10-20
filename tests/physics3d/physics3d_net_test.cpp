extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Physics3D.hpp"
#include "sim3.hpp"

// NOTE: kronklab test names are limited to 31 characters.

// Jolt, in a game with a server and a predicting client: the room and the client simulate the same bodies

namespace
{
    using namespace sim3;

    constexpr float WALK = 6.0f;   // meters per second: 0.1 per tick

    kw::Entity buildLevel(kw::World& world)
    {
        world.addResource<kuge::Physics3D>();
        const kw::Entity ground = world.create();
        const kw::Entity player = world.create();
        kuge::Transform3D transform;
        kuge::Body3D body;

        transform.translation = {0.0f, -0.5f, 0.0f};
        world.add<kuge::Transform3D>(ground, transform);
        world.add<kuge::Collider3D>(ground, kuge::Collider3D::box(100.0f, 1.0f, 100.0f));
        transform.translation = {0.0f, 1.0f, 0.0f};
        body.lockRotation = true;
        world.add<kuge::Transform3D>(player, transform);
        world.add<kuge::Collider3D>(player, kuge::Collider3D::capsule(0.4f, 1.8f));
        world.add<kuge::Body3D>(player, body);
        return player;
    }

    void walk(kw::World& world, kw::Entity player, const Steer3& steer)
    {
        auto& velocity = world.get<kuge::Body3D>(player).velocity;     // (y is the gravity's)

        velocity.x = steer.dx * WALK;
        velocity.z = steer.dz * WALK;
    }

    void tick(kw::World& world, double dt)
    {
        world.getResource<kuge::Physics3D>().step(world, static_cast<float>(dt));
    }

    Game3 joltGame(void) { return Game3{&buildLevel, &walk, &tick, true}; }

    // A wall that only the server has, and that moves across the path of the player
    void addWall(Sim3& sim)
    {
        const kw::Entity wall = sim.serverWorld.create();
        kuge::Transform3D transform;
        kuge::Body3D body;

        transform.translation = {4.0f, 1.0f, -2.5f};
        body.type = kuge::Body3D::Type::Kinematic;
        body.velocity = {0.0f, 0.0f, 1.5f};
        sim.serverWorld.add<kuge::Transform3D>(wall, transform);
        sim.serverWorld.add<kuge::Collider3D>(wall, kuge::Collider3D::box(1.0f, 2.0f, 3.0f));
        sim.serverWorld.add<kuge::Body3D>(wall, body);
        sim.server->track(wall, PROP, 0);
    }
}

Test(physics3d_net, answers_at_once)
{
    Sim3 sim(kuge::net::LoopbackNetwork::Conditions{.latency = 0.05, .seed = 1}, joltGame());

    sim.run(90);
    Assert(sim.prediction->ready(), "the server has shown the player");
    const float before = sim.clientPosition().x;

    sim.step(Steer3{1, 0});
    sim.step(Steer3{1, 0});
    Assert(sim.clientPosition().x - before > 0.15f, "two ticks: it already moved, not waiting for the server: %f", sim.clientPosition().x - before);
    sim.run(60, Steer3{1, 0});
    Assert(sim.clientPosition().x > before + 5.0f, "and goes on: %f", sim.clientPosition().x - before);
    sim.run(90);
    Assert(distance(sim.clientPosition(), sim.serverPosition()) < 0.02f, "in the end the client and the server agree: %f", distance(sim.clientPosition(), sim.serverPosition()));
    Assert(sim.prediction->stats().corrections * 20 < sim.prediction->stats().reconciliations,
        "the prediction is right nearly always: %llu corrections in %llu snapshots",
        static_cast<unsigned long long>(sim.prediction->stats().corrections),
        static_cast<unsigned long long>(sim.prediction->stats().reconciliations));
}

Test(physics3d_net, a_bad_network)
{
    for (std::uint64_t seed = 1; seed <= 3; ++seed) {
        Sim3 sim(kuge::net::LoopbackNetwork::Conditions{.loss = 0.1, .duplicate = 0.05, .latency = 0.05, .jitter = 0.02, .seed = seed}, joltGame());

        sim.run(60);
        for (int t = 0; t < 400; ++t) {
            sim.step(Steer3{static_cast<std::int8_t>(t % 100 < 50 ? 1 : -1), static_cast<std::int8_t>(t % 60 < 30 ? 0 : 1)});
        }
        sim.run(120);
        Assert(distance(sim.clientPosition(), sim.serverPosition()) < 0.02f, "seed %llu: it ends where the server has it: %f",
            static_cast<unsigned long long>(seed), distance(sim.clientPosition(), sim.serverPosition()));
        AssertEq(sim.prediction->stats().snaps, 0u, "never put back by force");
    }
}

Test(physics3d_net, a_wall_it_cannot_see)
{
    Sim3 sim(kuge::net::LoopbackNetwork::Conditions{.latency = 0.05, .jitter = 0.01, .seed = 3}, joltGame());

    addWall(sim);
    sim.run(60);
    float longest = 0.0f;
    const std::size_t from = sim.shown.size();

    for (int t = 0; t < 400; ++t) {
        sim.step(Steer3{static_cast<std::int8_t>(t % 200 < 100 ? 1 : -1), 0});     // to and fro across the wall's path
    }
    sim.run(120);
    for (std::size_t t = from + 1; t < sim.shown.size(); ++t) {
        longest = std::max(longest, distance(sim.shown[t], sim.shown[t - 1]));
    }
    Assert(sim.prediction->stats().corrections > 0, "the server did contradict the client: %llu corrections",
        static_cast<unsigned long long>(sim.prediction->stats().corrections));
    Assert(distance(sim.clientPosition(), sim.serverPosition()) < 0.05f, "and in the end they agree: %f", distance(sim.clientPosition(), sim.serverPosition()));
    Assert(longest < 0.6f, "each correction is spread over several ticks: the longest step drawn is %f", longest);
}
