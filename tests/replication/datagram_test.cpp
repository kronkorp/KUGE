extern "C" {
    #include "kronklab/kronklab.h"
}
#include "rep_fixture.hpp"
#include <cstdio>
#include <numeric>

// NOTE: kronklab test names are limited to 31 characters.
// Snapshots and the size of a datagram (ticket 036): a path that carries 1200 bytes at a time.

namespace
{
    using namespace kuge::replication;
    using Conditions = kuge::net::LoopbackNetwork::Conditions;

    // A busy world: ships that all move, so that a snapshot weighs about 13 KiB
    void busyWorld(RepSim& sim, std::vector<kw::Entity>& ships, int count)
    {
        for (int i = 0; i < count; ++i) {
            ships.push_back(sim.spawn(SHIP, static_cast<float>(i), 0.0f));
        }
    }

    void moveAll(RepSim& sim, const std::vector<kw::Entity>& ships, int ticks)
    {
        for (int t = 0; t < ticks; ++t) {
            for (const kw::Entity ship : ships) {
                sim.serverWorld.get<kuge::Transform2D>(ship).position.x += 0.5f;
            }
            sim.step();
        }
    }
}

Test(datagram, parts_fit_a_datagram)
{
    // The default part, with the headers of a snapshot, is under what a path carries in one piece
    RepSim sim({}, {}, {});
    std::vector<kw::Entity> ships;

    AssertEq(ReplicationServerConfig{}.maxPartBytes, 1100, "the default part is 1100 bytes");
    busyWorld(sim, ships, 300);
    moveAll(sim, ships, 60);
    Assert(sim.server->stats().maxParts > 5, "a snapshot of 300 ships takes several parts: %llu", static_cast<unsigned long long>(sim.server->stats().maxParts));
    AssertEq(sim.network.packetsTooBig(), 0, "none over the mtu (unset)");
    AssertEq(sim.serverEndpoint->stats().bigPackets, 0, "and the endpoint sent no packet over DATAGRAM_PAYLOAD");
    AssertEq(sim.clientCount(), 300, "the client has them all");
}

Test(datagram, an_mtu_of_1200)
{
    // 300 ships moving, 2 % lost, and a path that drops what is over 1200 bytes
    const Conditions path{.loss = 0.02, .latency = 0.02, .seed = 5, .mtu = 1200};

    RepSim good(path, {}, {});
    std::vector<kw::Entity> ships;

    busyWorld(good, ships, 300);
    moveAll(good, ships, 400);
    AssertEq(good.network.packetsTooBig(), 0, "parts of 1100 bytes all pass");
    AssertEq(good.clientCount(), 300, "so the client has the 300 ships");
    Assert(good.tick - good.client->lastTick() < 20, "and follows: tick %u, server %u", good.client->lastTick(), good.tick);
    Assert(good.client->stats().incomplete > 0, "a part lost now and then loses its snapshot (counted: %llu)", static_cast<unsigned long long>(good.client->stats().incomplete));

    // The old default: 6000 bytes in a datagram. Nothing gets through.
    ReplicationServerConfig old;

    old.maxPartBytes = 6000;
    RepSim bad(path, old, {});
    std::vector<kw::Entity> others;

    busyWorld(bad, others, 300);
    moveAll(bad, others, 400);
    Assert(bad.network.packetsTooBig() > 100, "parts of 6000 bytes are dropped by the path: %llu", static_cast<unsigned long long>(bad.network.packetsTooBig()));
    AssertEq(bad.clientCount(), 0, "and the client never sees a ship");
    AssertEq(bad.client->stats().snapshotsApplied, 0, "not one snapshot");
    Assert(bad.serverEndpoint->stats().bigPackets > 100, "the endpoint counted the packets over DATAGRAM_PAYLOAD that it sent: %llu", static_cast<unsigned long long>(bad.serverEndpoint->stats().bigPackets));
}

Test(datagram, smaller_parts_cost_little)
{
    // The same world on a clean network: what the smaller parts cost in bytes sent, headers included
    std::uint64_t bytes[2] = {0, 0};
    int which = 0;

    for (const std::size_t part : {std::size_t{6000}, std::size_t{1100}}) {
        ReplicationServerConfig config;

        config.maxPartBytes = part;
        RepSim sim({}, config, {});
        std::vector<kw::Entity> ships;

        busyWorld(sim, ships, 300);
        moveAll(sim, ships, 300);
        bytes[which++] = sim.serverEndpoint->stats().bytesSent;
    }
    const double more = static_cast<double>(bytes[1]) / static_cast<double>(bytes[0]);

    std::printf("parts of 1100 cost %.2f %% more than parts of 6000\n", (more - 1.0) * 100.0);
    Assert(more > 1.0 && more < 1.06, "parts of 1100 cost %.1f %% more than parts of 6000 (%llu against %llu bytes)", (more - 1.0) * 100.0,
        static_cast<unsigned long long>(bytes[1]), static_cast<unsigned long long>(bytes[0]));
}

Test(datagram, where_the_bytes_go)
{
    RepSim sim({}, {}, {});
    std::vector<kw::Entity> ships;

    busyWorld(sim, ships, 50);
    moveAll(sim, ships, 120);
    const auto& stats = sim.server->stats();

    AssertEq(stats.componentBytes.size(), sim.registry.size(), "one count for each component");
    const auto total = std::accumulate(stats.componentBytes.begin(), stats.componentBytes.end(), std::uint64_t{0});

    Assert(total > 0 && total < stats.bytesSent, "the components are most of what is sent, not all of it (%llu of %llu)",
        static_cast<unsigned long long>(total), static_cast<unsigned long long>(stats.bytesSent));
    Assert(stats.componentBytes[0] > stats.componentBytes[1] * 10, "the Transform2D (index 0) is where it goes: %llu bytes, against %llu for Health",
        static_cast<unsigned long long>(stats.componentBytes[0]), static_cast<unsigned long long>(stats.componentBytes[1]));
}
