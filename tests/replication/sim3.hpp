#pragma once

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
#include <functional>
#include <memory>

// A game in 3D to predict: a player that walks. The room and the client run the same functions on it
// (a Game3), and the test makes the time.

namespace sim3
{
    using namespace kuge::replication;
    using Vec3 = k3::Math::Vector3f;

    struct Steer3
    {
        KUGE_MESSAGE(Steer3, dx, dz)
        std::int8_t dx = 0;
        std::int8_t dz = 0;
        bool operator==(const Steer3&) const = default;
    };


    constexpr EntityType PLAYER = 1;
    constexpr EntityType PROP = 2;

    //! What the game is: how to make its world and the player, how an input steers the player, one tick of its physics
    struct Game3
    {
        std::function<kw::Entity(kw::World&)>                      build;
        std::function<void(kw::World&, kw::Entity, const Steer3&)> apply;
        std::function<void(kw::World&, double)>                    step;
        bool                                                       replicateBody = false;   //!< Body3D goes to the client too
    };

    inline ReplicationRegistry makeRegistry3(bool body)
    {
        ReplicationRegistry registry;

        registerTransform3D(registry, Replicate::Interpolated, true);     // predicted
        registerAnimationState3D(registry);
        if (body) {
            registerBody3D(registry, Replicate::OnChange, true);
        }
        return registry;
    }

    inline float distance(Vec3 a, Vec3 b)
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
        Game3                                        game;
        ReplicationRegistry                          registry;
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

        explicit Sim3(kuge::net::LoopbackNetwork::Conditions conditions, Game3 gameToPlay, PredictionConfig<Steer3> tune = {})
            : network(conditions, [this] { return now; }), game(std::move(gameToPlay)), registry(makeRegistry3(game.replicateBody))
        {
            kuge::net::EndpointConfig config;

            config.clock = [this] { return now; };
            serverPlayer = game.build(serverWorld);
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

            pc.build = game.build;
            pc.apply = game.apply;
            pc.step = game.step;
            prediction = std::make_unique<Prediction<Steer3, kuge::Transform3D>>(pc, clientWorld, registry, *client, *clientEndpoint);
        }

        void step(Steer3 steer = {})
        {
            now += DT;
            ++tick;
            serverEndpoint->poll();
            for (const auto& applied : inputs->collect()) {
                game.apply(serverWorld, serverPlayer, applied.input);
                server->setInputAck(applied.connection, applied.sequence);
            }
            game.step(serverWorld, DT);
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
