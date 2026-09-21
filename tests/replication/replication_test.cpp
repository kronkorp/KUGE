extern "C" {
    #include "kronklab/kronklab.h"
}
#include "rep_fixture.hpp"
#include <algorithm>
#include <cmath>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using namespace kuge::replication;

    std::map<NetworkId, int> healthOfServer(RepSim& sim)
    {
        std::map<NetworkId, int> healths;
        auto view = sim.serverWorld.view<Replicated>();

        for (kw::Entity e : view) {
            healths[sim.serverWorld.get<Replicated>(e).id] = sim.serverWorld.get<Health>(e).points;
        }
        return healths;
    }

    std::map<NetworkId, int> healthOfClient(RepSim& sim)
    {
        std::map<NetworkId, int> healths;
        auto view = sim.clientWorld.view<Replicated>();

        for (kw::Entity e : view) {
            healths[sim.clientWorld.get<Replicated>(e).id] = sim.clientWorld.get<Health>(e).points;
        }
        return healths;
    }
}

Test(replication, entities_appear)
{
    RepSim sim;
    std::vector<SpawnInfo> spawned;

    sim.client->onSpawn(SHIP, [&](kw::World& world, kw::Entity entity, const SpawnInfo& info) {
        spawned.push_back(info);
        Assert(world.has<Health>(entity) && world.has<Team>(entity) && world.has<kuge::Transform2D>(entity), "the components are there when the prefab is called");
    });
    const auto a = sim.spawn(SHIP, 10, 20, 100, 1, 7);
    const auto b = sim.spawn(SHIP, 30, 40, 80, 2);
    const auto c = sim.spawn(BULLET, 0, 0, 1, 0);

    (void)a; (void)b; (void)c;
    sim.run(10);
    AssertEq(sim.clientCount(), 3, "three entities on the client");
    AssertEq(spawned.size(), 2, "the prefab of SHIP was called for the two ships");
    AssertEq(spawned[0].owner, 7, "with the owner");
    AssertEq(spawned[0].type, SHIP, "and the type");
    const auto entity = sim.client->entity(spawned[0].id);

    Assert(entity.has_value(), "the table finds it by network id");
    AssertEq(sim.clientWorld.get<Health>(*entity).points, 100, "OnChange component");
    AssertEq(sim.clientWorld.get<Team>(*entity).id, 1, "OnSpawn component");
    Assert(sim.clientWorld.get<kuge::Transform2D>(*entity).position == kuge::Vec2(10, 20), "and the position");
    Assert(sim.clientWorld.has<Replicated>(*entity) && sim.clientWorld.get<Replicated>(*entity).id == spawned[0].id, "with its Replicated");
}

Test(replication, changes_arrive)
{
    RepSim sim;
    const auto ship = sim.spawn(SHIP, 0, 0, 100, 1);

    sim.run(10);
    sim.serverWorld.get<Health>(ship).points = 60;
    sim.serverWorld.get<Team>(ship).id = 9;             // OnSpawn: never sent again
    sim.run(10);
    const auto entity = *sim.client->entity(1);

    AssertEq(sim.clientWorld.get<Health>(entity).points, 60, "an OnChange component follows");
    AssertEq(sim.clientWorld.get<Team>(entity).id, 1, "an OnSpawn one stays as it was told");
}

Test(replication, entities_go)
{
    RepSim sim;
    std::vector<NetworkId> gone;

    sim.client->onDestroy(SHIP, [&](kw::World& world, kw::Entity entity) { gone.push_back(world.get<Replicated>(entity).id); });
    const auto a = sim.spawn(SHIP, 0, 0);
    const auto b = sim.spawn(SHIP, 0, 0);

    sim.run(10);
    AssertEq(sim.clientCount(), 2, "two");
    sim.serverWorld.remove(a);                            // destroyed on the server
    sim.run(10);
    AssertEq(sim.clientCount(), 1, "one left");
    Assert(gone.size() == 1 && gone[0] == 1, "the destroy hook was called before it went");
    Assert(!sim.client->entity(1).has_value() && sim.client->entity(2).has_value(), "the table follows");
    sim.server->untrack(b);                               // still there on the server, but not replicated
    sim.run(10);
    AssertEq(sim.clientCount(), 0, "untracked: it goes from the clients");
    Assert(sim.serverWorld.has<Health>(b), "and stays on the server");
}

Test(replication, a_component_removed)
{
    RepSim sim;
    const auto ship = sim.spawn(SHIP, 0, 0);

    sim.run(10);
    sim.serverWorld.remove<Health>(ship);
    sim.run(10);
    Assert(!sim.clientWorld.has<Health>(*sim.client->entity(1)), "a removed OnChange component is removed on the client");
}

Test(replication, deltas_are_small)
{
    RepSim sim;

    for (int i = 0; i < 20; ++i) {
        sim.spawn(SHIP, static_cast<float>(i), 0);
    }
    sim.run(20);
    const auto afterSpawn = sim.server->stats().bytesSent;

    Assert(afterSpawn > 20 * 20, "the entities were sent: %llu bytes", static_cast<unsigned long long>(afterSpawn));
    sim.run(60);
    AssertEq(sim.server->stats().bytesSent, afterSpawn, "nothing changed: no bytes of changes, only the snapshots that keep the time");
    sim.serverWorld.get<Health>(*sim.serverWorld.view<Health>().begin()).points = 1;
    sim.run(6);
    const auto oneChange = sim.server->stats().bytesSent - afterSpawn;

    Assert(oneChange > 0 && oneChange < 60, "one change is a few bytes: %llu", static_cast<unsigned long long>(oneChange));
}

Test(replication, a_late_client)
{
    RepSim sim;

    for (int i = 0; i < 5; ++i) {
        sim.spawn(SHIP, static_cast<float>(i), 0);
    }
    sim.run(30);
    // A second client comes when the world has lived for a while
    kw::World otherWorld;
    kuge::net::EndpointConfig config;

    config.clock = [&sim] { return sim.now; };
    kuge::net::Endpoint other(sim.network.connect("rep"), kuge::net::Role::Client, config);
    kuge::replication::ReplicationClient otherClient(otherWorld, sim.registry);

    otherClient.attach(other);
    for (int i = 0; i < 200 && !other.connected(); ++i) {
        sim.now += RepSim::DT;
        sim.serverEndpoint->poll();
        other.poll();
    }
    for (const auto id : sim.serverEndpoint->connections()) {
        if (!sim.server->hasClient(id)) {
            sim.server->addClient(id);
        }
    }
    for (int i = 0; i < 30; ++i) {
        sim.step();
        other.poll();
    }
    AssertEq(otherClient.entityCount(), 5, "it sees all five, from a full snapshot");
    Assert(sim.server->stats().fullSnapshots >= 2, "the server sent from nothing for each client: %llu", static_cast<unsigned long long>(sim.server->stats().fullSnapshots));
}

Test(replication, over_a_bad_network)
{
    // Loss, repeats and reordering, with entities coming and going all the time: in the end, both sides agree
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
        RepSim sim(kuge::net::LoopbackNetwork::Conditions{.loss = 0.3, .duplicate = 0.1, .latency = 0.03, .jitter = 0.05, .seed = seed});
        std::mt19937 rng(static_cast<unsigned>(seed));
        std::vector<kw::Entity> alive;

        for (int t = 0; t < 600; ++t) {
            const int roll = static_cast<int>(rng() % 10);

            if (roll < 3 || alive.empty()) {
                alive.push_back(sim.spawn(SHIP, static_cast<float>(rng() % 100), static_cast<float>(rng() % 100), static_cast<int>(rng() % 100)));
            } else if (roll < 5 && !alive.empty()) {
                const std::size_t i = rng() % alive.size();

                sim.serverWorld.remove(alive[i]);
                alive.erase(alive.begin() + static_cast<std::ptrdiff_t>(i));
            } else if (roll < 8 && !alive.empty()) {
                sim.serverWorld.get<Health>(alive[rng() % alive.size()]).points = static_cast<int>(rng() % 1000);
            }
            sim.step();
        }
        sim.run(200);     // no more changes: the client catches up
        Assert(healthOfClient(sim) == healthOfServer(sim), "seed %llu: the same entities, with the same health (%zu)", static_cast<unsigned long long>(seed), healthOfServer(sim).size());
        Assert(sim.client->stats().ignored > 0, "seed %llu: old and repeated packets were met", static_cast<unsigned long long>(seed));
        AssertEq(sim.client->stats().missingBaseline, 0, "seed %llu: every snapshot was built on one that the client had (the server only builds on what was acknowledged)", static_cast<unsigned long long>(seed));
    }
}

Test(replication, big_snapshots_in_parts)
{
    RepSim sim(kuge::net::LoopbackNetwork::Conditions{.loss = 0.15, .latency = 0.02, .jitter = 0.02, .seed = 4});

    for (int i = 0; i < 800; ++i) {
        sim.spawn(SHIP, static_cast<float>(i), static_cast<float>(i * 2), i);
    }
    sim.run(200);
    Assert(sim.server->stats().packetsSent > sim.server->stats().snapshotsSent + 10, "a snapshot went in several packets: %llu packets for %llu snapshots",
        static_cast<unsigned long long>(sim.server->stats().packetsSent), static_cast<unsigned long long>(sim.server->stats().snapshotsSent));
    Assert(healthOfClient(sim) == healthOfServer(sim), "and the client has all 800 entities");
}

Test(replication, a_client_that_cannot_follow)
{
    // Snapshots built on one that the client does not have: it says so and is sent everything
    RepSim sim;
    auto& endpoint = *sim.serverEndpoint;
    std::vector<std::uint32_t> acks;
    const NetworkId owner = 0;

    (void)owner;
    endpoint.on<SnapshotAck>([&acks](kuge::net::ConnectionId, const SnapshotAck& ack) { acks.push_back(ack.tick); });
    SnapshotPacket packet;

    packet.schema = sim.registry.schema();
    packet.tick = 500;
    packet.baseTick = 499;             // that one was never sent
    sim.serverEndpoint->send(sim.clientId, packet, kuge::net::Channel::Unreliable);
    sim.now += RepSim::DT;
    sim.serverEndpoint->poll();
    sim.clientEndpoint->poll();
    sim.now += RepSim::DT;
    sim.clientEndpoint->poll();
    sim.serverEndpoint->poll();
    AssertEq(sim.client->stats().missingBaseline, 1, "the client noticed");
    Assert(!acks.empty() && acks.back() == 0, "and asked for everything");
    AssertEq(sim.client->entityCount(), 0, "nothing was made of it");
}

Test(replication, other_lists_are_refused)
{
    RepSim sim;
    ReplicationRegistry other;

    registerTransform2D(other, Replicate::Interpolated);     // the client has only this one
    kw::World world;
    ReplicationClient client(world, other);

    client.attach(*sim.clientEndpoint);
    sim.spawn(SHIP, 1, 1);
    sim.run(20);
    Assert(client.stats().wrongSchema > 0, "the snapshots of a server with another list are seen as such");
    AssertEq(client.entityCount(), 0, "and not applied");
}

Test(replication, garbage_is_dropped)
{
    RepSim sim;
    SnapshotPacket packet;

    packet.schema = sim.registry.schema();
    packet.tick = 3;
    packet.ops = {9, 1, 2, 3};                    // no such kind of record
    sim.serverEndpoint->send(sim.clientId, packet, kuge::net::Channel::Unreliable);
    packet.tick = 4;
    packet.ops = {2, 1, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};   // an update of an entity that is not there
    sim.serverEndpoint->send(sim.clientId, packet, kuge::net::Channel::Unreliable);
    packet.tick = 5;
    packet.part = 9;                              // a part that does not exist
    packet.parts = 2;
    sim.serverEndpoint->send(sim.clientId, packet, kuge::net::Channel::Unreliable);
    sim.run(5);
    Assert(sim.client->stats().malformed >= 2 && sim.client->stats().ignored >= 1, "each was refused and counted");
    sim.spawn(SHIP, 5, 5);
    sim.run(20);
    AssertEq(sim.clientCount(), 1, "and the client is fine");
}

Test(replication, interest_filter)
{
    RepSim sim;

    sim.server->setFilter([](kuge::net::ConnectionId, NetworkId, const EntityRecord& record) { return record.type != BULLET; });
    sim.spawn(SHIP, 0, 0);
    sim.spawn(BULLET, 0, 0);
    sim.run(10);
    AssertEq(sim.clientCount(), 1, "the client is only told of what it may see");
}

// -- The diff, alone ----------------------------------------------------------------------------
Test(worldstate, diff_then_apply)
{
    const auto registry = makeRegistry();
    std::mt19937 rng(11);

    auto randomState = [&rng](const WorldState* like) {
        WorldState state;

        for (NetworkId id = 1; id <= 12; ++id) {
            if (rng() % 3 == 0) {
                continue;
            }
            EntityRecord record;
            const auto old = like ? like->find(id) : WorldState::const_iterator();

            record.type = static_cast<EntityType>(1 + rng() % 3);
            record.owner = static_cast<NetworkId>(rng() % 3);
            for (std::uint8_t index = 0; index < 3; ++index) {
                if (rng() % 4 != 0) {
                    record.components[index] = Bytes(1 + rng() % 5, static_cast<std::uint8_t>(rng()));
                }
            }
            // An entity that stays keeps its type, owner and OnSpawn component (index 2): they are told once
            if (like && old != like->end()) {
                record.type = old->second.type;
                record.owner = old->second.owner;
                const auto team = old->second.components.find(2);

                if (team != old->second.components.end()) {
                    record.components[2] = team->second;
                } else {
                    record.components.erase(2);
                }
            }
            state[id] = std::move(record);
        }
        return state;
    };
    for (int round = 0; round < 500; ++round) {
        const WorldState before = randomState(nullptr);
        const WorldState after = randomState(&before);
        const auto ops = diffOps(registry, &before, after);
        Bytes joined;

        for (const auto& op : ops) {
            joined.insert(joined.end(), op.begin(), op.end());
        }
        Assert(applyOps(registry, before, joined) == after, "round %d: the changes turn the old state into the new", round);
        Bytes full;

        for (const auto& op : diffOps(registry, nullptr, after)) {
            full.insert(full.end(), op.begin(), op.end());
        }
        Assert(applyOps(registry, WorldState{}, full) == after, "round %d: and from nothing too", round);
    }
}

Test(worldstate, same_world_same_bytes)
{
    RepSim a, b;

    for (RepSim* sim : {&a, &b}) {
        sim->spawn(SHIP, 1, 2, 30, 4);
        sim->spawn(BULLET, 5, 6, 70, 8);
    }
    Assert(captureState(a.serverWorld, a.registry) == captureState(b.serverWorld, b.registry), "the same world gives the same state");
    const auto opsA = diffOps(a.registry, nullptr, captureState(a.serverWorld, a.registry));
    const auto opsB = diffOps(b.registry, nullptr, captureState(b.serverWorld, b.registry));

    Assert(opsA == opsB, "and the same bytes");
}

Test(registry, rules)
{
    ReplicationRegistry registry;
    bool threw = false;

    registry.component<Health>("Health", Replicate::OnChange,
        [](kuge::ByteWriter&, const Health&) {}, [](kuge::ByteReader&) { return Health{}; });
    try {
        registry.component<Health>("Health", Replicate::OnChange,
            [](kuge::ByteWriter&, const Health&) {}, [](kuge::ByteReader&) { return Health{}; });
    } catch (const std::logic_error&) { threw = true; }
    Assert(threw, "a name is registered once");
    threw = false;
    try {
        registry.component<Team>("Team", Replicate::Interpolated,
            [](kuge::ByteWriter&, const Team&) {}, [](kuge::ByteReader&) { return Team{}; });
    } catch (const std::logic_error&) { threw = true; }
    Assert(threw, "an Interpolated component needs a lerp");
    ReplicationRegistry same = makeRegistry();
    ReplicationRegistry again = makeRegistry();

    AssertEq(same.schema(), again.schema(), "the same list has the same schema");
    Assert(same.schema() != registry.schema(), "another list has another");
}

// -- Interpolation -------------------------------------------------------------------------------
namespace
{
    // The server moves an entity by one unit per tick, and the test looks at what the client draws
    struct Motion
    {
        std::vector<float> client;    //!< x on the client after each tick
        std::vector<float> server;    //!< x on the server
    };

    Motion moveAndWatch(RepSim& sim, int ticks, int stopAt = -1)
    {
        Motion motion;
        const kw::Entity ship = sim.spawn(SHIP, 0, 0);

        for (int t = 0; t < ticks; ++t) {
            if (stopAt < 0 || t < stopAt) {
                sim.serverWorld.get<kuge::Transform2D>(ship).position.x += 1.0f;
            }
            sim.step();
            const auto entity = sim.client->entity(1);

            motion.server.push_back(sim.serverWorld.get<kuge::Transform2D>(ship).position.x);
            motion.client.push_back(entity ? sim.clientWorld.get<kuge::Transform2D>(*entity).position.x : -1.0f);
        }
        return motion;
    }
}

Test(interpolation, smooth_movement)
{
    RepSim sim;
    const Motion motion = moveAndWatch(sim, 300);
    float biggest = 0.0f, smallest = 100.0f;
    bool forward = true;

    for (std::size_t t = 60; t < motion.client.size(); ++t) {
        const float step = motion.client[t] - motion.client[t - 1];

        forward = forward && step >= 0.0f;
        biggest = std::max(biggest, step);
        smallest = std::min(smallest, step);
    }
    Assert(forward, "it never goes backwards");
    Assert(biggest < 1.5f && smallest > 0.5f, "one unit a tick, smoothly, though snapshots come every 2 ticks: steps from %f to %f", smallest, biggest);
    const float lag = motion.server.back() - motion.client.back();

    Assert(lag > 3.0f && lag < 10.0f, "drawn about 0.1 s (6 ticks) behind the server: %f", lag);
}

Test(interpolation, smooth_over_a_bad_network)
{
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        RepSim sim(kuge::net::LoopbackNetwork::Conditions{.loss = 0.2, .duplicate = 0.1, .latency = 0.04, .jitter = 0.03, .seed = seed});
        const Motion motion = moveAndWatch(sim, 400);
        float biggest = 0.0f;
        bool forward = true;

        for (std::size_t t = 100; t < motion.client.size(); ++t) {
            const float step = motion.client[t] - motion.client[t - 1];

            forward = forward && step >= -0.001f;
            biggest = std::max(biggest, step);
        }
        Assert(forward, "seed %llu: never backwards", static_cast<unsigned long long>(seed));
        Assert(biggest < 6.0f, "seed %llu: no jump (20%% of the snapshots lost): the biggest step is %f", static_cast<unsigned long long>(seed), biggest);
    }
}

Test(interpolation, it_arrives_and_stops)
{
    RepSim sim(kuge::net::LoopbackNetwork::Conditions{.loss = 0.2, .latency = 0.03, .jitter = 0.02, .seed = 7});
    const Motion motion = moveAndWatch(sim, 400, 150);      // moves for 150 ticks, then stops

    AssertEq(motion.server.back(), 150.0f, "the server stopped at 150");
    Assert(std::fabs(motion.client.back() - 150.0f) < 0.001f, "and the client ends there exactly: %f", motion.client.back());
}

Test(interpolation, still_then_moving)
{
    // What did not change is not sent, but the client must not slide from an old position to a new one
    RepSim sim;
    const kw::Entity ship = sim.spawn(SHIP, 5, 0);
    std::vector<float> seen;

    for (int t = 0; t < 300; ++t) {
        if (t >= 150) {
            sim.serverWorld.get<kuge::Transform2D>(ship).position.x += 1.0f;
        }
        sim.step();
        if (const auto entity = sim.client->entity(1)) {
            seen.push_back(sim.clientWorld.get<kuge::Transform2D>(*entity).position.x);
        }
    }
    bool still = true;

    for (std::size_t t = 0; t < 150; ++t) {
        still = still && seen[t] == 5.0f;
    }
    Assert(still, "for the first 150 ticks it stays exactly where it was");
    Assert(seen[152] <= 5.0f + 0.001f, "and it starts to move only a moment after the server: %f", seen[152]);
    Assert(seen.back() > 100.0f && seen.back() < sim.serverWorld.get<kuge::Transform2D>(ship).position.x, "then it follows, behind: %f", seen.back());
}

Test(interpolation, rotation_by_the_short_way)
{
    RepSim sim;
    const kw::Entity ship = sim.spawn(SHIP, 0, 0);
    bool through180 = false;

    sim.serverWorld.get<kuge::Transform2D>(ship).rotation = 350.0f;
    sim.run(60);
    sim.serverWorld.get<kuge::Transform2D>(ship).rotation = 10.0f;     // 20 degrees clockwise, through 0
    for (int t = 0; t < 60; ++t) {
        sim.step();
        const float r = sim.clientWorld.get<kuge::Transform2D>(*sim.client->entity(1)).rotation;

        through180 = through180 || (r > 20.0f && r < 340.0f);
    }
    Assert(!through180, "350 to 10 does not turn all the way round");
    Assert(std::fabs(sim.clientWorld.get<kuge::Transform2D>(*sim.client->entity(1)).rotation - 10.0f) < 0.01f, "and it ends at 10");
}

Test(interpolation, a_new_entity_is_placed)
{
    RepSim sim;

    sim.spawn(SHIP, 33, 44);
    sim.run(6);
    const auto entity = sim.client->entity(1);

    Assert(entity.has_value(), "it exists");
    Assert(sim.clientWorld.get<kuge::Transform2D>(*entity).position == kuge::Vec2(33, 44), "at the place it was told, not at the origin");
}
