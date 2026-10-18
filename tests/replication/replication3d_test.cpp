extern "C" {
    #include "kronklab/kronklab.h"
}
#include "AnimationState3D.hpp"
#include "CommonComponents.hpp"
#include "Endpoint.hpp"
#include "Input.hpp"
#include "Loopback.hpp"
#include "Prediction.hpp"
#include "ReplicationClient.hpp"
#include "ReplicationServer.hpp"
#include "Transform3D.hpp"
#include <algorithm>
#include <cmath>
#include <memory>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using namespace kuge::replication;
    using Vec3 = k3::Math::Vector3f;

    // A game in 3D: a player that walks on the ground plane, at 0.1 m a tick. The room and the client
    // run the same functions on it.
    struct Steer3
    {
        KUGE_MESSAGE(Steer3, dx, dz)
        std::int8_t dx = 0;
        std::int8_t dz = 0;
        bool operator==(const Steer3&) const = default;
    };

    struct Wish { float x = 0.0f; float z = 0.0f; };

    constexpr float STEP = 0.1f;
    constexpr EntityType PLAYER = 1;
    constexpr EntityType PROP = 2;

    ReplicationRegistry makeRegistry3(void)
    {
        ReplicationRegistry registry;

        registerTransform3D(registry, Replicate::Interpolated, true);     // predicted
        registerAnimationState3D(registry);
        return registry;
    }

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

    float distance(Vec3 a, Vec3 b)
    {
        return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
    }

    // A server world, a client, and a network between them. Time is made by the test.
    struct Sim3
    {
        static constexpr double DT = 1.0 / 60.0;

        double                                       now = 0.0;
        std::uint32_t                                tick = 0;
        kuge::net::LoopbackNetwork                   network;
        ReplicationRegistry                          registry = makeRegistry3();
        kw::World                                    serverWorld;
        kw::World                                    clientWorld;
        kw::Entity                                   serverPlayer{};
        std::unique_ptr<kuge::net::Endpoint>         serverEndpoint;
        std::unique_ptr<kuge::net::Endpoint>         clientEndpoint;
        std::unique_ptr<ReplicationServer>           server;
        std::unique_ptr<ReplicationClient>           client;
        std::unique_ptr<InputServer<Steer3>>         inputs;
        std::unique_ptr<Prediction<Steer3, kuge::Transform3D>> prediction;
        std::vector<Vec3>                            shown;   //!< Where the client drew the player after each tick

        explicit Sim3(kuge::net::LoopbackNetwork::Conditions conditions = {}, PredictionConfig<Steer3> tune = {})
            : network(conditions, [this] { return now; })
        {
            kuge::net::EndpointConfig config;

            config.clock = [this] { return now; };
            serverPlayer = buildPlayer3(serverWorld);
            serverEndpoint = std::make_unique<kuge::net::Endpoint>(network.listen("game3"), kuge::net::Role::Server, config);
            clientEndpoint = std::make_unique<kuge::net::Endpoint>(network.connect("game3"), kuge::net::Role::Client, config);
            server = std::make_unique<ReplicationServer>(serverWorld, registry, *serverEndpoint);
            client = std::make_unique<ReplicationClient>(clientWorld, registry);
            inputs = std::make_unique<InputServer<Steer3>>(*serverEndpoint);
            client->attach(*clientEndpoint);
            client->setLocalPlayer(1);
            client->predictType(PLAYER);
            for (int i = 0; i < 300 && !(clientEndpoint->connected() && serverEndpoint->connected()); ++i) {
                now += DT;
                serverEndpoint->poll();
                clientEndpoint->poll();
            }
            const auto clientId = serverEndpoint->connections().front();

            server->addClient(clientId);
            inputs->addClient(clientId);
            server->track(serverPlayer, PLAYER, 1);

            PredictionConfig<Steer3> pc = tune;

            pc.build = [](kw::World& w) { return buildPlayer3(w); };
            pc.apply = &steer3;
            pc.step = &step3;
            prediction = std::make_unique<Prediction<Steer3, kuge::Transform3D>>(pc, clientWorld, registry, *client, *clientEndpoint);
        }

        void step(Steer3 steer = {})
        {
            now += DT;
            ++tick;
            serverEndpoint->poll();
            for (const auto& applied : inputs->collect()) {
                steer3(serverWorld, serverPlayer, applied.input);
                server->setInputAck(applied.connection, applied.sequence);
            }
            step3(serverWorld, DT);
            server->update(tick);
            clientEndpoint->poll();
            prediction->tick(steer);
            client->update(DT);
            if (const auto e = prediction->entity()) {
                shown.push_back(clientWorld.get<kuge::Transform3D>(*e).translation);
            } else {
                shown.push_back({});
            }
        }

        void run(int ticks, Steer3 steer = {}) { for (int i = 0; i < ticks; ++i) { step(steer); } }

        Vec3 serverPosition(void) { return serverWorld.get<kuge::Transform3D>(serverPlayer).translation; }
        Vec3 clientPosition(void) { return clientWorld.get<kuge::Transform3D>(*prediction->entity()).translation; }

        // A prop that the server moves and nobody predicts
        kw::Entity addProp(Vec3 at)
        {
            const kw::Entity e = serverWorld.create();
            kuge::Transform3D t;

            t.translation = at;
            serverWorld.add<kuge::Transform3D>(e, t);
            server->track(e, PROP, 0);
            return e;
        }

        kw::Entity onClient(kw::Entity serverEntity)
        {
            return *client->entity(serverWorld.get<Replicated>(serverEntity).id);
        }
    };
}

Test(replication3d, a_transform_arrives)
{
    Sim3 sim;
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
    Sim3 sim(kuge::net::LoopbackNetwork::Conditions{.latency = 0.03, .seed = 2});
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
    Sim3 sim;
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
    Sim3 sim(kuge::net::LoopbackNetwork::Conditions{.latency = 0.05, .seed = 1});

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
        Sim3 sim(kuge::net::LoopbackNetwork::Conditions{.loss = 0.1, .duplicate = 0.05, .latency = 0.05, .jitter = 0.02, .seed = seed});

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
    Sim3 sim(kuge::net::LoopbackNetwork::Conditions{.latency = 0.03, .seed = 4});

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
    Sim3 sim(kuge::net::LoopbackNetwork::Conditions{.latency = 0.03, .seed = 4});

    sim.run(30);
    sim.serverWorld.get<kuge::Transform3D>(sim.serverPlayer).translation.z += 100.0f;   // a respawn, far away
    sim.run(60);
    AssertEq(sim.prediction->stats().snaps, 1u, "put there at once");
    Assert(distance(sim.clientPosition(), sim.serverPosition()) < 1e-3f, "and it agrees");
}
