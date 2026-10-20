extern "C" {
    #include "kronklab/kronklab.h"
}
#include "rep_fixture.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>

// Counts the allocations of this program (it is only read by ten_players_at_128_hz). GCC sees the free() of a
// replaced operator delete as a mismatch with the new it inlined: it is not one.
#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
namespace { std::atomic<std::size_t> g_allocations{0}; }

void* operator new(std::size_t size)
{
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* memory = std::malloc(size ? size : 1)) {
        return memory;
    }
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

// NOTE: kronklab test names are limited to 31 characters.
// The rates of a game: the tick, the snapshots, and how far behind the others are drawn (ticket 028).

namespace
{
    using namespace kuge::replication;
    using Conditions = kuge::net::LoopbackNetwork::Conditions;

    ReplicationServerConfig serverAt(double sendRate, std::uint32_t tickRate)
    {
        ReplicationServerConfig config;

        config.sendRate = sendRate;
        config.tickRate = tickRate;
        return config;
    }

    ReplicationClientConfig clientAt(std::uint32_t tickRate)
    {
        ReplicationClientConfig config;

        config.tickRate = tickRate;
        return config;
    }

    // One entity that the server moves by one unit a tick; the client is stepped as usual. Returns the x that it draws.
    float moveFor(RepSim& sim, kw::Entity ship, int ticks)
    {
        for (int t = 0; t < ticks; ++t) {
            sim.serverWorld.get<kuge::Transform2D>(ship).position.x += 1.0f;
            sim.step();
        }
        return sim.clientWorld.get<kuge::Transform2D>(*sim.client->entity(1)).position.x;
    }
}

Test(rates, snapshots_in_hertz)
{
    kw::World world;
    kuge::net::LoopbackNetwork network;
    ReplicationRegistry registry = makeRegistry();
    kuge::net::Endpoint endpoint(network.listen("rates"), kuge::net::Role::Server);

    AssertEq(ReplicationServer(world, registry, endpoint, serverAt(30.0, 60)).interval(), 2, "30 a second at 60 Hz: every 2 ticks");
    AssertEq(ReplicationServer(world, registry, endpoint, serverAt(64.0, 128)).interval(), 2, "64 a second at 128 Hz: every 2 ticks");
    AssertEq(ReplicationServer(world, registry, endpoint, serverAt(60.0, 128)).interval(), 2, "60 a second at 128 Hz: 2.13 ticks, rounded to 2");
    AssertEq(ReplicationServer(world, registry, endpoint, serverAt(1000.0, 60)).interval(), 1, "never faster than the tick");
    AssertEq(ReplicationServer(world, registry, endpoint).interval(), 2, "without a rate, sendInterval: 2");
    ReplicationServerConfig ticks;

    ticks.sendInterval = 5;
    ticks.sendRate = 0.0;
    AssertEq(ReplicationServer(world, registry, endpoint, ticks).interval(), 5, "in ticks, as before");
    bool threw = false;

    try { ReplicationServer(world, registry, endpoint, serverAt(30.0, 0)); } catch (const std::invalid_argument&) { threw = true; }
    Assert(threw, "a rate with no tick rate to be a part of is a mistake");
}

Test(rates, the_delay_follows_the_rate)
{
    // Two snapshot intervals, whatever the rates
    for (const auto& [tickRate, sendRate] : {std::pair{60u, 30.0}, std::pair{128u, 64.0}, std::pair{128u, 32.0}, std::pair{30u, 10.0}}) {
        RepSim sim({}, serverAt(sendRate, tickRate), clientAt(tickRate), tickRate);
        const kw::Entity ship = sim.spawn(SHIP, 0, 0);

        moveFor(sim, ship, 120);
        const double expected = 2.0 / sendRate;

        Assert(std::fabs(sim.client->delay() - expected) < 1e-9, "%u Hz, %.0f snapshots a second: drawn %.1f ms behind, not %.1f", tickRate, sendRate, sim.client->delay() * 1000.0, expected * 1000.0);
    }
    // The seconds are still there, as an override
    ReplicationClientConfig fixed = clientAt(128);

    fixed.interpolationDelay = 0.1;
    RepSim sim({}, serverAt(64.0, 128), fixed, 128);
    const kw::Entity ship = sim.spawn(SHIP, 0, 0);

    moveFor(sim, ship, 60);
    Assert(std::fabs(sim.client->delay() - 0.1) < 1e-9, "a delay in seconds replaces it");
}

Test(rates, it_never_waits_at_64_hz)
{
    // 64 snapshots a second at 128 Hz, 2 % lost: two intervals (31 ms) ride over a lost snapshot. (They do not ride over
    // a lost snapshot AND jitter of a few milliseconds: that is what adaptiveDelay is for.)
    RepSim sim(Conditions{.loss = 0.02, .latency = 0.02, .seed = 11}, serverAt(64.0, 128), clientAt(128), 128);
    const kw::Entity ship = sim.spawn(SHIP, 0, 0);

    moveFor(sim, ship, 400);
    const auto starved = sim.client->stats().starved;
    float biggest = 0.0f, last = sim.clientWorld.get<kuge::Transform2D>(*sim.client->entity(1)).position.x;

    for (int t = 0; t < 1280; ++t) {
        const float x = moveFor(sim, ship, 1);

        biggest = std::max(biggest, x - last);
        last = x;
    }
    AssertEq(sim.client->stats().starved, starved, "the entity never waited in ten seconds (it did %llu times, lost %llu)", (unsigned long long)(sim.client->stats().starved - starved), (unsigned long long)sim.network.packetsLost());
    Assert(std::fabs(sim.client->delay() - 2.0 / 64.0) < 1e-9, "drawn 31 ms behind: %f", sim.client->delay());
    Assert(biggest < 2.0f, "and never jumped: the biggest step is %f", biggest);
}

Test(rates, a_delay_that_adapts)
{
    ReplicationClientConfig adaptive = clientAt(60);

    adaptive.adaptiveDelay = true;
    // Bad: 40 % of the snapshots are lost
    RepSim sim(Conditions{.loss = 0.4, .latency = 0.01, .seed = 3}, serverAt(30.0, 60), adaptive, 60);
    const kw::Entity ship = sim.spawn(SHIP, 0, 0);
    const double base = 2.0 / 30.0;

    moveFor(sim, ship, 600);
    Assert(sim.client->stats().starved > 0, "the buffer ran dry");
    Assert(sim.client->delay() > base + 1.0 / 30.0 - 1e-9, "so the delay grew: %f s (base %f)", sim.client->delay(), base);
    Assert(sim.client->delay() <= 6.0 / 30.0 + 1e-9, "but not past 6 intervals: %f", sim.client->delay());

    // Good again
    sim.network.setConditions(Conditions{.latency = 0.01, .seed = 3});
    moveFor(sim, ship, 60 * 40);
    Assert(std::fabs(sim.client->delay() - base) < 1e-9, "and it came back to its base once the snapshots were steady: %f", sim.client->delay());

    // The same network with no adaptation: the delay never moves
    RepSim fixed(Conditions{.loss = 0.4, .latency = 0.01, .seed = 3}, serverAt(30.0, 60), clientAt(60), 60);
    const kw::Entity other = fixed.spawn(SHIP, 0, 0);

    moveFor(fixed, other, 600);
    Assert(fixed.client->stats().starved > 0 && std::fabs(fixed.client->delay() - base) < 1e-9, "off by default");
}

Test(rates, extrapolation_is_bounded)
{
    // The network is cut: what is drawn goes on for maxExtrapolation, then waits
    float frozen = 0.0f, extra = 0.0f, longer = 0.0f;

    for (const double maxExtrapolation : {0.0, 0.05, 0.2}) {
        ReplicationClientConfig config = clientAt(60);

        config.maxExtrapolation = maxExtrapolation;
        RepSim sim({}, {}, config);
        const kw::Entity ship = sim.spawn(SHIP, 0, 0);

        moveFor(sim, ship, 120);
        sim.network.setConditions(Conditions{.loss = 1.0});
        const float x = moveFor(sim, ship, 90);

        (maxExtrapolation == 0.0 ? frozen : maxExtrapolation == 0.05 ? extra : longer) = x;
        if (maxExtrapolation > 0.0) {
            Assert(sim.client->stats().extrapolated > 0, "it was extrapolated");
        } else {
            AssertEq(sim.client->stats().extrapolated, 0, "off by default");
        }
    }
    Assert(extra > frozen + 1.0f, "it goes on past the last value: %f against %f", extra, frozen);
    Assert(extra <= frozen + 0.05f * 60.0f + 0.01f, "for 50 ms at most (3 ticks): %f against %f", extra, frozen);
    Assert(longer > extra + 3.0f && longer <= frozen + 0.2f * 60.0f + 0.01f, "and 200 ms if asked: %f", longer);
}

Test(rates, ten_players_at_128_hz)
{
    // Ten ships moving, a snapshot every other tick at 128 Hz: what one snapshot costs, to one client and to ten
    RepSim sim({}, serverAt(64.0, 128), clientAt(128), 128);
    std::vector<kw::Entity> ships;

    for (int i = 0; i < 10; ++i) {
        ships.push_back(sim.spawn(SHIP, static_cast<float>(i), 0));
    }
    for (int t = 0; t < 256; ++t) {
        for (const kw::Entity ship : ships) {
            sim.serverWorld.get<kuge::Transform2D>(ship).position.x += 0.5f;
        }
        sim.step();
    }
    const auto before = sim.server->stats();
    std::size_t serverAllocations = 0, clientAllocations = 0;

    for (int t = 0; t < 1280; ++t) {   // ten seconds
        for (const kw::Entity ship : ships) {
            sim.serverWorld.get<kuge::Transform2D>(ship).position.x += 0.5f;
        }
        // The cost of a tick, in allocations: the server's update (collecting the state, diffing, sending) and the client's
        sim.now += sim.dt;
        ++sim.tick;
        sim.serverEndpoint->poll();
        std::size_t mark = g_allocations.load();

        sim.server->update(sim.tick);
        serverAllocations += g_allocations.load() - mark;
        sim.clientEndpoint->poll();
        mark = g_allocations.load();
        sim.client->update(sim.dt);
        clientAllocations += g_allocations.load() - mark;
    }
    const auto after = sim.server->stats();
    const auto snapshots = after.snapshotsSent - before.snapshotsSent;
    const double perSnapshot = static_cast<double>(after.bytesSent - before.bytesSent) / static_cast<double>(snapshots);

    AssertEq(snapshots, 640, "64 snapshots a second for ten seconds");
    AssertEq(after.packetsSent - before.packetsSent, snapshots, "each in one packet");
    Assert(perSnapshot < 600.0, "ten moving ships cost %.0f bytes a snapshot", perSnapshot);
    // (Not zero: the state is captured into fresh maps at each snapshot. Counted so that it is known, and cannot grow unseen.)
    Assert(serverAllocations / 1280.0 < 400.0 && clientAllocations / 1280.0 < 100.0, "allocations a tick: %.1f in the server's update, %.1f in the client's", serverAllocations / 1280.0, clientAllocations / 1280.0);
    std::printf("allocations per tick: %.1f in ReplicationServer::update (%.1f per snapshot), %.1f in ReplicationClient::update\n",
        serverAllocations / 1280.0, serverAllocations / 640.0, clientAllocations / 1280.0);
    std::printf("128 Hz tick, 64 snapshots/s, 10 moving ships: %.0f bytes per snapshot to one client, %.1f KiB/s; to ten clients %.1f KiB/s\n",
        perSnapshot, perSnapshot * 64.0 / 1024.0, perSnapshot * 64.0 * 10.0 / 1024.0);
}
