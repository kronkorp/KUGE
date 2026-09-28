#pragma once

#include "CommonComponents.hpp"
#include "Endpoint.hpp"
#include "Loopback.hpp"
#include "ReplicationClient.hpp"
#include "ReplicationServer.hpp"
#include <memory>
#include <random>

// Components of the games of the tests
struct Health { int points = 0; bool operator==(const Health&) const = default; };
struct Team   { std::uint8_t id = 0; bool operator==(const Team&) const = default; };

constexpr kuge::replication::EntityType SHIP = 1;
constexpr kuge::replication::EntityType BULLET = 2;

// The same list on both sides, as a game would (one function that both call)
inline kuge::replication::ReplicationRegistry makeRegistry(void)
{
    using namespace kuge::replication;
    ReplicationRegistry registry;

    registerTransform2D(registry, Replicate::Interpolated);
    registry.component<Health>("Health", Replicate::OnChange,
        [](kuge::ByteWriter& out, const Health& h) { out.write<std::int32_t>(h.points); },
        [](kuge::ByteReader& in) { return Health{in.read<std::int32_t>()}; });
    registry.component<Team>("Team", Replicate::OnSpawn,
        [](kuge::ByteWriter& out, const Team& t) { out.write(t.id); },
        [](kuge::ByteReader& in) { return Team{in.read<std::uint8_t>()}; });
    return registry;
}

// A server world, a client world, and a network between them, with a time of their own
struct RepSim
{
    static constexpr double DT = 1.0 / 60.0;

    double                                       now = 0.0;
    std::uint32_t                                tick = 0;
    kuge::net::LoopbackNetwork                   network;
    kuge::replication::ReplicationRegistry       registry = makeRegistry();
    kw::World                                    serverWorld;
    kw::World                                    clientWorld;
    std::unique_ptr<kuge::net::Endpoint>         serverEndpoint;
    std::unique_ptr<kuge::net::Endpoint>         clientEndpoint;
    std::unique_ptr<kuge::replication::ReplicationServer> server;
    std::unique_ptr<kuge::replication::ReplicationClient> client;
    kuge::net::ConnectionId                      clientId = 0;

    explicit RepSim(kuge::net::LoopbackNetwork::Conditions conditions = {},
        kuge::replication::ReplicationServerConfig serverConfig = {},
        kuge::replication::ReplicationClientConfig clientConfig = {})
        : network(conditions, [this] { return now; })
    {
        kuge::net::EndpointConfig config;

        config.clock = [this] { return now; };
        serverEndpoint = std::make_unique<kuge::net::Endpoint>(network.listen("rep"), kuge::net::Role::Server, config);
        clientEndpoint = std::make_unique<kuge::net::Endpoint>(network.connect("rep"), kuge::net::Role::Client, config);
        server = std::make_unique<kuge::replication::ReplicationServer>(serverWorld, registry, *serverEndpoint, serverConfig);
        client = std::make_unique<kuge::replication::ReplicationClient>(clientWorld, registry, clientConfig);
        client->attach(*clientEndpoint);
        for (int i = 0; i < 300 && !(clientEndpoint->connected() && serverEndpoint->connected()); ++i) {
            now += DT;
            serverEndpoint->poll();
            clientEndpoint->poll();
        }
        clientId = serverEndpoint->connections().empty() ? 0 : serverEndpoint->connections().front();
        server->addClient(clientId);
    }

    // One tick: the server updates, the network runs, the client applies and draws
    void step(void)
    {
        now += DT;
        ++tick;
        serverEndpoint->poll();
        server->update(tick);
        clientEndpoint->poll();
        client->update(DT);
    }

    void run(int ticks) { for (int i = 0; i < ticks; ++i) { step(); } }

    kw::Entity spawn(kuge::replication::EntityType type, float x, float y, int health = 100, std::uint8_t team = 1, kuge::replication::NetworkId owner = 0)
    {
        const kw::Entity entity = serverWorld.create();

        serverWorld.add<kuge::Transform2D>(entity, kuge::Transform2D{{x, y}});
        serverWorld.add<Health>(entity, Health{health});
        serverWorld.add<Team>(entity, Team{team});
        server->track(entity, type, owner);
        return entity;
    }

    std::size_t clientCount(void)
    {
        std::size_t count = 0;
        auto view = clientWorld.view<kuge::replication::Replicated>();

        for ([[maybe_unused]] kw::Entity e : view) { ++count; }
        return count;
    }
};
