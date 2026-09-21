extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Loopback.hpp"
#include <set>
#include <stdexcept>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using kuge::net::TransportEvent;

    std::vector<TransportEvent> polled(kuge::net::ITransport& transport)
    {
        std::vector<TransportEvent> events;

        transport.poll(events);
        return events;
    }

    std::vector<std::uint8_t> bytes(std::initializer_list<std::uint8_t> list) { return list; }
}

Test(loopback, connect_and_talk)
{
    kuge::net::LoopbackNetwork network;
    auto server = network.listen("a");
    auto client = network.connect("a");
    auto serverEvents = polled(*server);
    auto clientEvents = polled(*client);

    AssertEq(serverEvents.size(), 1, "the server sees the client come");
    Assert(serverEvents[0].kind == TransportEvent::Kind::Connected, "as a connection");
    AssertEq(clientEvents.size(), 1, "the client sees it is connected");
    const auto id = serverEvents[0].connection;

    Assert(client->send(kuge::net::CLIENT_CONNECTION, bytes({1, 2, 3})), "the client sends");
    Assert(server->send(id, bytes({9})), "and the server answers");
    serverEvents = polled(*server);
    clientEvents = polled(*client);
    Assert(serverEvents.size() == 1 && serverEvents[0].packet == bytes({1, 2, 3}), "a whole packet");
    Assert(clientEvents.size() == 1 && clientEvents[0].packet == bytes({9}), "and back");
}

Test(loopback, nobody_listens)
{
    kuge::net::LoopbackNetwork network;
    auto client = network.connect("nowhere");
    const auto events = polled(*client);

    Assert(events.size() == 1 && events[0].kind == TransportEvent::Kind::Disconnected, "the client is told at once");
    Assert(!client->send(kuge::net::CLIENT_CONNECTION, bytes({1})), "and cannot send");
}

Test(loopback, a_name_is_taken)
{
    kuge::net::LoopbackNetwork network;
    auto first = network.listen("a");
    bool threw = false;

    try { network.listen("a"); } catch (const std::runtime_error&) { threw = true; }
    Assert(threw, "one server per name");
    first.reset();
    Assert(network.listen("a") != nullptr, "the name is free again once it is gone");
}

Test(loopback, several_clients)
{
    kuge::net::LoopbackNetwork network;
    auto server = network.listen("a");
    auto one = network.connect("a");
    auto two = network.connect("a");
    std::set<kuge::net::ConnectionId> ids;

    for (const auto& event : polled(*server)) {
        ids.insert(event.connection);
    }
    AssertEq(ids.size(), 2, "two connections, two ids");
    one->send(kuge::net::CLIENT_CONNECTION, bytes({1}));
    two->send(kuge::net::CLIENT_CONNECTION, bytes({2}));
    std::set<std::uint8_t> got;
    std::set<kuge::net::ConnectionId> from;

    for (const auto& event : polled(*server)) {
        got.insert(event.packet[0]);
        from.insert(event.connection);
    }
    Assert(got == std::set<std::uint8_t>({1, 2}) && from == ids, "each packet says who sent it");
}

Test(loopback, leaving_is_seen)
{
    kuge::net::LoopbackNetwork network;
    auto server = network.listen("a");
    auto client = network.connect("a");
    const auto id = polled(*server)[0].connection;

    polled(*client);
    client->send(kuge::net::CLIENT_CONNECTION, bytes({5}));
    client.reset();
    const auto events = polled(*server);

    Assert(events.size() == 2 && events[0].kind == TransportEvent::Kind::Packet && events[0].packet == bytes({5}), "what was sent before leaving still arrives");
    Assert(events[1].kind == TransportEvent::Kind::Disconnected && events[1].connection == id, "then the disconnection");
    Assert(!server->send(id, bytes({1})), "no more sending to it");
}

Test(loopback, the_server_leaves)
{
    kuge::net::LoopbackNetwork network;
    auto server = network.listen("a");
    auto client = network.connect("a");

    polled(*server);
    polled(*client);
    server.reset();
    const auto events = polled(*client);

    Assert(events.size() == 1 && events[0].kind == TransportEvent::Kind::Disconnected, "the client is told");
}

Test(loopback, close_from_the_server)
{
    kuge::net::LoopbackNetwork network;
    auto server = network.listen("a");
    auto client = network.connect("a");
    const auto id = polled(*server)[0].connection;

    polled(*client);
    server->close(id);
    const auto events = polled(*client);

    Assert(events.size() == 1 && events[0].kind == TransportEvent::Kind::Disconnected, "closing is seen by the client");
}

Test(loopback, too_big_is_refused)
{
    kuge::net::LoopbackNetwork network;
    auto server = network.listen("a");
    auto client = network.connect("a");

    polled(*server);
    polled(*client);
    Assert(client->send(kuge::net::CLIENT_CONNECTION, std::vector<std::uint8_t>(kuge::net::MAX_PACKET)), "the biggest packet goes");
    Assert(!client->send(kuge::net::CLIENT_CONNECTION, std::vector<std::uint8_t>(kuge::net::MAX_PACKET + 1)), "one byte more does not");
}

Test(loopback, loss_is_reproducible)
{
    auto run = [] {
        kuge::net::LoopbackNetwork network(kuge::net::LoopbackNetwork::Conditions{.loss = 0.5, .seed = 42});
        auto server = network.listen("a");
        auto client = network.connect("a");
        std::vector<std::uint8_t> got;

        polled(*server);
        polled(*client);
        for (std::uint8_t i = 0; i < 100; ++i) { client->send(kuge::net::CLIENT_CONNECTION, bytes({i})); }
        for (const auto& event : polled(*server)) { got.push_back(event.packet[0]); }
        return got;
    };
    const auto first = run();

    Assert(first == run(), "the same seed loses the same packets");
    Assert(first.size() > 30 && first.size() < 70, "about half: %zu of 100", first.size());
}

Test(loopback, latency_and_order)
{
    double now = 0.0;
    kuge::net::LoopbackNetwork network(kuge::net::LoopbackNetwork::Conditions{.latency = 0.1}, [&now] { return now; });
    auto server = network.listen("a");
    auto client = network.connect("a");

    polled(*server);
    polled(*client);
    client->send(kuge::net::CLIENT_CONNECTION, bytes({1}));
    now = 0.05;
    client->send(kuge::net::CLIENT_CONNECTION, bytes({2}));
    AssertEq(polled(*server).size(), 0, "nothing before the latency");
    now = 0.11;
    auto events = polled(*server);
    AssertEq(events.size(), 1, "the first arrives at 0.1");
    now = 0.2;
    events = polled(*server);
    AssertEq(events.size(), 1, "the second at 0.15");
}

Test(loopback, jitter_reorders)
{
    double now = 0.0;
    kuge::net::LoopbackNetwork network(kuge::net::LoopbackNetwork::Conditions{.jitter = 1.0, .seed = 5}, [&now] { return now; });
    auto server = network.listen("a");
    auto client = network.connect("a");

    polled(*server);
    polled(*client);
    for (std::uint8_t i = 0; i < 50; ++i) { client->send(kuge::net::CLIENT_CONNECTION, bytes({i})); }
    now = 2.0;
    std::vector<std::uint8_t> got;

    for (const auto& event : polled(*server)) { got.push_back(event.packet[0]); }
    AssertEq(got.size(), 50, "all arrive");
    Assert(!std::is_sorted(got.begin(), got.end()), "but not in the order they were sent");
}

Test(loopback, duplicates)
{
    kuge::net::LoopbackNetwork network(kuge::net::LoopbackNetwork::Conditions{.duplicate = 1.0});
    auto server = network.listen("a");
    auto client = network.connect("a");

    polled(*server);
    polled(*client);
    client->send(kuge::net::CLIENT_CONNECTION, bytes({1}));
    AssertEq(polled(*server).size(), 2, "the packet arrives twice");
}
