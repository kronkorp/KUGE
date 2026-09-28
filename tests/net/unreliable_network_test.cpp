extern "C" {
    #include "kronklab/kronklab.h"
}
#include "net_fixture.hpp"
#include <set>

// NOTE: kronklab test names are limited to 31 characters.
// A network that loses, repeats and delays packets: what an Endpoint promises must hold anyway.

namespace
{
    using namespace kuge::net;

    // The client sends `count` numbered reliable messages; the server must get all of them, once, in order
    bool reliableRun(LoopbackNetwork::Conditions conditions, std::uint32_t count, double maxSeconds, std::uint64_t* resends = nullptr)
    {
        Sim sim(conditions);
        auto server = sim.server();
        auto client = sim.client();
        std::vector<std::uint32_t> got;

        server->on<Number>([&](ConnectionId, const Number& number) { got.push_back(number.value); });
        if (!sim.until([&] { return client->connected(); }, 60.0, *server, *client)) {
            return false;
        }
        std::uint32_t sent = 0;
        const double end = sim.now + maxSeconds;

        while (got.size() < count && sim.now < end) {
            // A few messages each step (the queue is bounded: it says no when full, and we try again)
            for (int i = 0; i < 5 && sent < count; ++i) {
                if (client->send(CLIENT_CONNECTION, Number{sent}, Channel::Reliable)) {
                    ++sent;
                }
            }
            sim.now += 0.005;
            server->poll();
            client->poll();
        }
        if (resends) {
            *resends = client->stats().resends;
        }
        if (got.size() != count) {
            return false;
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            if (got[i] != i) {
                return false;
            }
        }
        return true;
    }
}

Test(unreliable, reliable_no_loss)
{
    std::uint64_t resends = 99;

    Assert(reliableRun({}, 1000, 30.0, &resends), "1000 messages, in order, once");
    AssertEq(resends, 0, "and none was sent twice");
}

Test(unreliable, reliable_10_percent)
{
    std::uint64_t resends = 0;

    Assert(reliableRun(LoopbackNetwork::Conditions{.loss = 0.10, .latency = 0.02, .jitter = 0.01, .seed = 3}, 1000, 120.0, &resends), "1000 messages over 10%% loss");
    Assert(resends > 0, "some were sent again");
}

Test(unreliable, reliable_30_percent)
{
    Assert(reliableRun(LoopbackNetwork::Conditions{.loss = 0.30, .duplicate = 0.1, .latency = 0.03, .jitter = 0.05, .seed = 11}, 500, 300.0), "500 messages over 30%% loss, repeats and reordering");
}

Test(unreliable, reliable_50_percent)
{
    Assert(reliableRun(LoopbackNetwork::Conditions{.loss = 0.50, .duplicate = 0.2, .latency = 0.02, .jitter = 0.1, .seed = 23}, 200, 600.0), "200 messages over 50%% loss");
}

Test(unreliable, reliable_many_seeds)
{
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        Assert(reliableRun(LoopbackNetwork::Conditions{.loss = 0.2, .duplicate = 0.1, .latency = 0.01, .jitter = 0.04, .seed = seed}, 150, 200.0), "seed %llu", static_cast<unsigned long long>(seed));
    }
}

Test(unreliable, both_ways_at_once)
{
    Sim sim(LoopbackNetwork::Conditions{.loss = 0.2, .duplicate = 0.1, .latency = 0.02, .jitter = 0.03, .seed = 5});
    auto server = sim.server();
    auto client = sim.client();
    std::vector<std::uint32_t> atServer, atClient;

    server->on<Number>([&](ConnectionId, const Number& n) { atServer.push_back(n.value); });
    client->on<Number>([&](ConnectionId, const Number& n) { atClient.push_back(n.value); });
    sim.until([&] { return client->connected(); }, 30.0, *server, *client);
    for (std::uint32_t i = 0; i < 200; ++i) {
        client->send(CLIENT_CONNECTION, Number{i});
        server->send(server->connections()[0], Number{i});
    }
    sim.until([&] { return atServer.size() == 200 && atClient.size() == 200; }, 200.0, *server, *client);
    bool ordered = atServer.size() == 200 && atClient.size() == 200;

    for (std::uint32_t i = 0; ordered && i < 200; ++i) {
        ordered = atServer[i] == i && atClient[i] == i;
    }
    Assert(ordered, "200 each way, in order, once");
}

Test(unreliable, unreliable_is_best_effort)
{
    Sim sim(LoopbackNetwork::Conditions{.loss = 0.5, .seed = 8});
    auto server = sim.server();
    auto client = sim.client();
    std::multiset<std::uint32_t> got;

    server->on<Number>([&](ConnectionId, const Number& n) { got.insert(n.value); });
    sim.until([&] { return client->connected(); }, 60.0, *server, *client);
    for (std::uint32_t i = 0; i < 1000; ++i) {
        client->send(CLIENT_CONNECTION, Number{i}, Channel::Unreliable);
    }
    sim.run(0.5, 0.01, *server, *client);
    Assert(got.size() > 350 && got.size() < 650, "about half arrive: %zu of 1000", got.size());
    Assert(server->stats().resends == 0 && client->stats().resends == 0, "and nothing is sent again");
}

Test(unreliable, connecting_over_loss)
{
    // The handshake itself must survive: Connect and Accept are repeated
    for (std::uint64_t seed = 1; seed <= 10; ++seed) {
        Sim sim(LoopbackNetwork::Conditions{.loss = 0.5, .seed = seed});
        auto server = sim.server();
        auto client = sim.client();

        Assert(sim.until([&] { return client->connected() && server->connected(); }, 10.0, *server, *client), "seed %llu: connected over 50%% loss", static_cast<unsigned long long>(seed));
    }
}

Test(unreliable, a_dead_link_ends)
{
    Sim sim;
    auto server = sim.server();
    auto client = sim.client();
    std::vector<DisconnectReason> reasons;

    client->onDisconnected([&](ConnectionId, DisconnectReason reason) { reasons.push_back(reason); });
    sim.until([&] { return client->connected(); }, 2.0, *server, *client);
    sim.network.setConditions(LoopbackNetwork::Conditions{.loss = 1.0});      // the cable is cut
    for (int i = 0; i < 100; ++i) {
        client->send(CLIENT_CONNECTION, Number{static_cast<std::uint32_t>(i)});
    }
    // (Only the client polls: a server that also timed out would close the link, which the client would see first)
    sim.run(15.0, 0.05, *client);
    Assert(reasons.size() == 1 && reasons[0] == DisconnectReason::Timeout, "the client gives up: a timeout (%zu events, first %d)", reasons.size(), reasons.empty() ? -1 : static_cast<int>(reasons[0]));
    Assert(client->stats().resends > 0, "after trying again");
}

Test(unreliable, a_full_queue_says_no)
{
    Sim sim;
    EndpointConfig config;
    config.reliable.window = 4;
    config.reliable.maxQueued = 10;
    auto server = sim.server();
    auto client = sim.client("room", config);
    int accepted = 0;

    sim.until([&] { return client->connected(); }, 2.0, *server, *client);
    sim.network.setConditions(LoopbackNetwork::Conditions{.loss = 1.0});
    for (int i = 0; i < 100; ++i) {
        accepted += client->send(CLIENT_CONNECTION, Number{1}) ? 1 : 0;
    }
    AssertEq(accepted, 14, "4 in the window and 10 that wait: the others are refused, so no growth without end");
    AssertEq(client->stats().refusedSends, 86, "and counted");
}
