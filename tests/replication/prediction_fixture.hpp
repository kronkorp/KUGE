#pragma once

#include "CommonComponents.hpp"
#include "Endpoint.hpp"
#include "Input.hpp"
#include "Loopback.hpp"
#include "Physics2D.hpp"
#include "Prediction.hpp"
#include "ReplicationClient.hpp"
#include "ReplicationServer.hpp"
#include <cmath>
#include <memory>
#include <random>

// A game to predict: a square that moves in the four directions among walls. Its physics is deterministic,
// and the room and the client run the same functions on it.

struct Steer
{
    KUGE_MESSAGE(Steer, dx, dy)
    std::int8_t dx = 0;
    std::int8_t dy = 0;
    bool operator==(const Steer&) const = default;
};

constexpr float SPEED = 120.0f;                    // pixels per second: 2 per tick
constexpr float STEP = SPEED / 60.0f;
constexpr kuge::replication::EntityType PLAYER = 1;
constexpr kuge::replication::EntityType MOVER = 2;

inline kuge::replication::ReplicationRegistry makePredictionRegistry(void)
{
    using namespace kuge::replication;
    ReplicationRegistry registry;

    registerTransform2D(registry, Replicate::Interpolated, true);     // predicted
    registerBody(registry, Replicate::OnChange, true);
    return registry;
}

// The level, the same for everyone: two walls, and a player if asked
inline void buildLevel(kw::World& world)
{
    world.addResource<kuge::Physics2D>(kuge::PhysicsConfig{});
    const auto wall = [&world](float x, float y, float w, float h) {
        const kw::Entity e = world.create();

        world.add<kuge::Transform2D>(e, kuge::Transform2D{{x, y}});
        world.add<kuge::Collider>(e, kuge::Collider::box(w, h));
    };

    wall(300.0f, 0.0f, 20.0f, 2000.0f);      // a wall at x = 300
    wall(0.0f, 300.0f, 2000.0f, 20.0f);      // and one at y = 300
}

inline kw::Entity buildPlayer(kw::World& world)
{
    const kw::Entity e = world.create();
    kuge::Body body;

    body.type = kuge::Body::Type::Kinematic;
    world.add<kuge::Transform2D>(e, kuge::Transform2D{{50.0f, 50.0f}});
    world.add<kuge::Body>(e, body);
    world.add<kuge::Collider>(e, kuge::Collider::box(10.0f, 10.0f));
    return e;
}

inline void steerPlayer(kw::World& world, kw::Entity e, const Steer& steer)
{
    world.get<kuge::Body>(e).velocity = {steer.dx * SPEED, steer.dy * SPEED};
}

inline void stepPhysics(kw::World& world, double dt)
{
    world.getResource<kuge::Physics2D>().step(world, static_cast<float>(dt));
}

// The server world, the client, and the network. Time is made by the test.
struct PredSim
{
    static constexpr double DT = 1.0 / 60.0;

    double                                       now = 0.0;
    std::uint32_t                                tick = 0;
    kuge::net::LoopbackNetwork                   network;
    kuge::replication::ReplicationRegistry       registry = makePredictionRegistry();
    kw::World                                    serverWorld;
    kw::World                                    clientWorld;
    kw::Entity                                   serverPlayer{};
    kw::Entity                                   mover{};
    bool                                         moverActive = false;
    float                                        moverX = 100.0f;
    std::unique_ptr<kuge::net::Endpoint>         serverEndpoint;
    std::unique_ptr<kuge::net::Endpoint>         clientEndpoint;
    std::unique_ptr<kuge::replication::ReplicationServer> server;
    std::unique_ptr<kuge::replication::ReplicationClient> client;
    std::unique_ptr<kuge::replication::InputServer<Steer>> inputs;
    std::unique_ptr<kuge::replication::Prediction<Steer>>   prediction;
    kuge::net::ConnectionId                      clientId = 0;
    std::vector<kuge::Vec2>                      shown;          //!< Where the client drew the player after each tick
    std::vector<kuge::Vec2>                      real;           //!< Where the server had it

    explicit PredSim(kuge::net::LoopbackNetwork::Conditions conditions = {}, std::size_t jitter = 0,
        kuge::replication::PredictionConfig<Steer> tune = {})
        : network(conditions, [this] { return now; })
    {
        kuge::net::EndpointConfig config;

        config.clock = [this] { return now; };
        buildLevel(serverWorld);
        serverPlayer = buildPlayer(serverWorld);
        serverEndpoint = std::make_unique<kuge::net::Endpoint>(network.listen("game"), kuge::net::Role::Server, config);
        clientEndpoint = std::make_unique<kuge::net::Endpoint>(network.connect("game"), kuge::net::Role::Client, config);
        server = std::make_unique<kuge::replication::ReplicationServer>(serverWorld, registry, *serverEndpoint);
        client = std::make_unique<kuge::replication::ReplicationClient>(clientWorld, registry);
        inputs = std::make_unique<kuge::replication::InputServer<Steer>>(*serverEndpoint, kuge::replication::InputServerConfig{.jitter = jitter});
        client->attach(*clientEndpoint);
        client->setLocalPlayer(1);
        client->predictType(PLAYER);
        for (int i = 0; i < 300 && !(clientEndpoint->connected() && serverEndpoint->connected()); ++i) {
            now += DT;
            serverEndpoint->poll();
            clientEndpoint->poll();
        }
        clientId = serverEndpoint->connections().front();
        server->addClient(clientId);
        inputs->addClient(clientId);
        server->track(serverPlayer, PLAYER, 1);

        kuge::replication::PredictionConfig<Steer> pc = tune;

        pc.build = [](kw::World& w) { buildLevel(w); return buildPlayer(w); };
        pc.apply = &steerPlayer;
        pc.step = &stepPhysics;
        prediction = std::make_unique<kuge::replication::Prediction<Steer>>(pc, clientWorld, registry, *client, *clientEndpoint);
    }

    // A wall that only the server has, and that moves: the client cannot predict what it does to the player
    void addMover(void)
    {
        mover = serverWorld.create();
        serverWorld.add<kuge::Transform2D>(mover, kuge::Transform2D{{moverX, 60.0f}});
        serverWorld.add<kuge::Collider>(mover, kuge::Collider::box(20.0f, 60.0f));
        server->track(mover, MOVER, 0);
        moverActive = true;
    }

    void step(Steer steer)
    {
        now += DT;
        ++tick;
        serverEndpoint->poll();
        for (const auto& applied : inputs->collect()) {
            steerPlayer(serverWorld, serverPlayer, applied.input);
            server->setInputAck(applied.connection, applied.sequence);
        }
        if (moverActive) {
            moverX = 100.0f + 80.0f * std::sin(static_cast<float>(tick) * 0.04f);      // (a wall going to and fro)
            serverWorld.get<kuge::Transform2D>(mover).position.x = moverX;
        }
        stepPhysics(serverWorld, DT);
        server->update(tick);
        clientEndpoint->poll();
        prediction->tick(steer);
        client->update(DT);
        if (const auto e = prediction->entity()) {
            shown.push_back(clientWorld.get<kuge::Transform2D>(*e).position);
        } else {
            shown.push_back({});
        }
        real.push_back(serverWorld.get<kuge::Transform2D>(serverPlayer).position);
    }

    // A walk that changes direction now and then (always the same for a seed)
    static std::vector<Steer> script(std::size_t ticks, unsigned seed)
    {
        std::mt19937 rng(seed);
        std::vector<Steer> steers;
        Steer current;

        for (std::size_t t = 0; t < ticks; ++t) {
            if (t % 25 == 0) {
                current.dx = static_cast<std::int8_t>(static_cast<int>(rng() % 3) - 1);
                current.dy = static_cast<std::int8_t>(static_cast<int>(rng() % 3) - 1);
            }
            steers.push_back(current);
        }
        return steers;
    }

    void play(const std::vector<Steer>& steers)
    {
        for (const Steer& steer : steers) {
            step(steer);
        }
    }

    void rest(int ticks) { for (int i = 0; i < ticks; ++i) { step(Steer{}); } }

    kuge::Vec2 serverPosition(void) { return serverWorld.get<kuge::Transform2D>(serverPlayer).position; }
    kuge::Vec2 clientPosition(void) { return clientWorld.get<kuge::Transform2D>(*prediction->entity()).position; }
};

inline float distanceBetween(kuge::Vec2 a, kuge::Vec2 b)
{
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

// The longest that the drawn player moved in one tick, from the tick `from` on
inline float longestStep(const std::vector<kuge::Vec2>& shown, std::size_t from)
{
    float longest = 0.0f;

    for (std::size_t t = from + 1; t < shown.size(); ++t) {
        longest = std::max(longest, distanceBetween(shown[t], shown[t - 1]));
    }
    return longest;
}
