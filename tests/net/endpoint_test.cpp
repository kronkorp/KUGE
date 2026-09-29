extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Net.hpp"
#include "net_fixture.hpp"
#include <algorithm>
#include <set>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using namespace kuge::net;

    // (Messages are types of a namespace: a local class cannot have the static members they need)
    struct Cut { KUGE_MESSAGE(Chat, from) std::uint32_t from = 1; };
    struct A { KUGE_MESSAGE(SameId) };
    struct B { static constexpr std::uint32_t kugeMessageId = A::kugeMessageId; static constexpr const char* kugeMessageName = "Other";
               template<typename V> void kugeVisit(V&&) {} template<typename V> void kugeVisit(V&&) const {} };

    struct Log
    {
        std::vector<ConnectionId>                        connected;
        std::vector<std::pair<ConnectionId, DisconnectReason>> disconnected;
    };

    void watch(Endpoint& endpoint, Log& log)
    {
        endpoint.onConnected([&log](ConnectionId id) { log.connected.push_back(id); });
        endpoint.onDisconnected([&log](ConnectionId id, DisconnectReason reason) { log.disconnected.emplace_back(id, reason); });
    }
}

Test(endpoint, the_handshake)
{
    Sim sim;
    auto server = sim.server();
    auto client = sim.client();
    Log serverLog, clientLog;

    watch(*server, serverLog);
    watch(*client, clientLog);
    Assert(!client->connected() && !server->connected(), "nobody is connected yet");
    Assert(sim.until([&] { return client->connected(); }, 2.0, *server, *client), "the client is connected");
    sim.run(0.1, 0.01, *server, *client);
    AssertEq(serverLog.connected.size(), 1, "the server heard of it once");
    AssertEq(clientLog.connected.size(), 1, "and the client too");
    AssertEq(clientLog.connected[0], CLIENT_CONNECTION, "the server is the client's connection 1");
    Assert(server->connected() && server->connections().size() == 1, "one peer on the server");
    Assert(serverLog.disconnected.empty() && clientLog.disconnected.empty(), "nobody left");
    Assert(!client->peer(CLIENT_CONNECTION).empty(), "the peer is described: '%s'", client->peer(CLIENT_CONNECTION).c_str());
}

Test(endpoint, messages_both_ways)
{
    Sim sim;
    auto server = sim.server();
    auto client = sim.client();
    std::vector<Chat> atServer;
    std::vector<Number> atClient;
    ConnectionId who = 0;

    server->on<Chat>([&](ConnectionId from, const Chat& chat) { atServer.push_back(chat); who = from; });
    client->on<Number>([&](ConnectionId, const Number& number) { atClient.push_back(number); });
    sim.until([&] { return client->connected(); }, 2.0, *server, *client);
    Assert(client->send(CLIENT_CONNECTION, Chat{7, "hello"}), "the client sends");
    Assert(server->send(server->connections()[0], Number{42}, Channel::Unreliable), "the server answers");
    sim.run(0.2, 0.01, *server, *client);
    AssertEq(atServer.size(), 1, "one chat");
    AssertEq(atServer[0].from, 7, "with its fields");
    AssertStrEq(atServer[0].text.c_str(), "hello", "and its text");
    Assert(who == server->connections()[0], "the server knows who it is from");
    AssertEq(atClient.size(), 1, "one number");
    AssertEq(atClient[0].value, 42, "its value");
    Assert(server->stats().messagesReceived == 1 && client->stats().messagesSent == 1, "and the stats count them");
}

Test(endpoint, send_needs_a_connection)
{
    Sim sim;
    auto server = sim.server();
    auto client = sim.client();

    Assert(!client->send(CLIENT_CONNECTION, Ping{}), "not connected yet: refused");
    Assert(!server->send(99, Ping{}), "an unknown peer: refused");
    AssertEq(client->stats().refusedSends, 1, "and counted");
}

Test(endpoint, nobody_there)
{
    Sim sim;
    auto client = sim.client("nowhere");
    Log log;

    watch(*client, log);
    sim.run(0.1, 0.01, *client);
    AssertEq(log.disconnected.size(), 1, "the client says it failed");
    Assert(log.disconnected[0].second == DisconnectReason::Unreachable, "unreachable");
    Assert(client->closed() && !client->connected(), "and it is over");
}

Test(endpoint, a_server_that_never_answers)
{
    Sim sim;
    auto server = sim.server();            // exists, but nobody polls it
    auto client = sim.client();
    Log log;

    watch(*client, log);
    sim.run(4.0, 0.05, *client);
    AssertEq(log.disconnected.size(), 0, "still trying after 4 s (the limit is 5)");
    sim.run(2.0, 0.05, *client);
    Assert(log.disconnected.size() == 1 && log.disconnected[0].second == DisconnectReason::Timeout, "it gives up: a timeout");
}

Test(endpoint, another_protocol)
{
    Sim sim;
    EndpointConfig old;
    old.protocol = 1;
    EndpointConfig recent;
    recent.protocol = 2;
    auto server = sim.server("room", recent);
    auto client = sim.client("room", old);
    Log clientLog, serverLog;

    watch(*client, clientLog);
    watch(*server, serverLog);
    sim.run(1.0, 0.01, *server, *client);
    Assert(clientLog.disconnected.size() == 1 && clientLog.disconnected[0].second == DisconnectReason::Refused, "the client is refused");
    Assert(serverLog.connected.empty() && serverLog.disconnected.empty(), "and the server heard of nothing");
    Assert(!server->connected(), "no connection");
}

Test(endpoint, the_server_is_full)
{
    Sim sim;
    EndpointConfig small;
    small.maxConnections = 2;
    auto server = sim.server("room", small);
    auto a = sim.client();
    auto b = sim.client();
    auto c = sim.client();
    Log logC, logServer;

    watch(*c, logC);
    watch(*server, logServer);
    sim.run(1.0, 0.01, *server, *a, *b, *c);
    AssertEq(server->connections().size(), 2, "two are in");
    Assert(a->connected() && b->connected() && !c->connected(), "the third is not");
    Assert(logC.disconnected.size() == 1 && logC.disconnected[0].second == DisconnectReason::Refused, "and it is told that the server refused it, at once");
    AssertEq(logServer.connected.size(), 2, "the server heard of the two only");
    AssertEq(logServer.disconnected.size(), 0, "and of nobody leaving");
    // A place that frees is for the next one, even if some were refused meanwhile
    a->disconnect(a->connections().front());
    sim.run(0.5, 0.01, *server, *a, *b, *c);
    auto d = sim.client();
    Log logD;

    watch(*d, logD);
    sim.run(1.0, 0.01, *server, *b, *d);
    Assert(d->connected() && logD.disconnected.empty(), "a place that was freed is given to the next one");
}

Test(endpoint, refused_do_not_pile_up)
{
    // A crowd that knocks on a full server: it answers each one, and does not keep them
    Sim sim;
    EndpointConfig small;
    small.maxConnections = 2;
    auto server = sim.server("room", small);
    auto a = sim.client();
    auto b = sim.client();
    std::vector<std::unique_ptr<Endpoint>> crowd;
    std::vector<Log> logs(30);

    sim.run(0.5, 0.01, *server, *a, *b);
    for (std::size_t i = 0; i < logs.size(); ++i) {
        crowd.push_back(sim.client());
        watch(*crowd.back(), logs[i]);
    }
    for (int step = 0; step < 100; ++step) {
        sim.run(0.01, 0.01, *server, *a, *b);
        for (auto& client : crowd) {
            client->poll();
        }
    }
    AssertEq(server->connections().size(), 2, "the two are still the only ones");
    std::size_t refused = 0;

    for (const Log& log : logs) {
        refused += log.disconnected.size() == 1 && log.disconnected[0].second == DisconnectReason::Refused ? 1 : 0;
    }
    Assert(refused >= 20, "most of the crowd was told no (%zu of %zu)", refused, logs.size());
    a->disconnect(a->connections().front());
    sim.run(0.2, 0.01, *server, *b);
    auto next = sim.client();
    Log logNext;

    watch(*next, logNext);
    sim.run(1.0, 0.01, *server, *b, *next);
    Assert(next->connected() && logNext.disconnected.empty(), "and the crowd did not take the place that was freed");
}

Test(endpoint, keep_alive_keeps_it)
{
    Sim sim;
    auto server = sim.server();
    auto client = sim.client();
    Log serverLog, clientLog;

    watch(*server, serverLog);
    watch(*client, clientLog);
    sim.run(120.0, 0.05, *server, *client);     // two minutes of nothing to say
    Assert(client->connected() && server->connected(), "still connected");
    Assert(serverLog.disconnected.empty() && clientLog.disconnected.empty(), "nobody timed out");
    Assert(server->stats().packetsReceived > 100, "thanks to the keep-alives: %llu packets", static_cast<unsigned long long>(server->stats().packetsReceived));
}

Test(endpoint, a_silent_peer_times_out)
{
    Sim sim;
    auto server = sim.server();
    auto client = sim.client();
    Log serverLog;

    watch(*server, serverLog);
    sim.until([&] { return client->connected(); }, 2.0, *server, *client);
    const double start = sim.now;

    sim.run(9.0, 0.05, *server);                   // the client stops polling
    AssertEq(serverLog.disconnected.size(), 0, "not yet after 9 s");
    sim.run(2.0, 0.05, *server);
    Assert(serverLog.disconnected.size() == 1 && serverLog.disconnected[0].second == DisconnectReason::Timeout, "a timeout after 10 s");
    Assert(sim.now - start > 10.0, "and not before");
    Assert(!server->connected(), "it is gone");
}

Test(endpoint, disconnecting)
{
    Sim sim;
    auto server = sim.server();
    auto client = sim.client();
    Log serverLog, clientLog;

    watch(*server, serverLog);
    watch(*client, clientLog);
    sim.until([&] { return client->connected(); }, 2.0, *server, *client);
    client->disconnect(CLIENT_CONNECTION);
    Assert(clientLog.disconnected.size() == 1 && clientLog.disconnected[0].second == DisconnectReason::Local, "the client reports it at once: Local");
    sim.run(0.1, 0.01, *server, *client);
    Assert(serverLog.disconnected.size() == 1 && serverLog.disconnected[0].second == DisconnectReason::Remote, "the server: Remote, with no timeout to wait for");
    Assert(!server->connected() && !client->connected(), "both are over");
}

Test(endpoint, a_destroyed_peer_says_bye)
{
    Sim sim;
    auto server = sim.server();
    auto client = sim.client();
    Log serverLog;

    watch(*server, serverLog);
    sim.until([&] { return client->connected(); }, 2.0, *server, *client);
    client.reset();
    sim.run(0.1, 0.01, *server);
    Assert(serverLog.disconnected.size() == 1, "the server knows in a tick, not in 10 s");
}

Test(endpoint, the_server_kicks_one)
{
    Sim sim;
    auto server = sim.server();
    auto a = sim.client();
    auto b = sim.client();
    Log logA, logB;

    watch(*a, logA);
    watch(*b, logB);
    sim.run(0.5, 0.01, *server, *a, *b);
    const auto ids = server->connections();

    AssertEq(ids.size(), 2, "two peers");
    server->disconnect(ids[0]);
    sim.run(0.5, 0.01, *server, *a, *b);
    AssertEq(server->connections().size(), 1, "one left");
    AssertEq(logA.disconnected.size() + logB.disconnected.size(), 1, "one of the clients was told it is out");
}

Test(endpoint, handlers_may_do_anything)
{
    // An echo server that also kicks whoever says "bye", from inside the handler
    Sim sim;
    auto server = sim.server();
    auto client = sim.client();
    Log serverLog;
    std::vector<std::string> echoed;

    watch(*server, serverLog);
    server->on<Chat>([&](ConnectionId from, const Chat& chat) {
        server->send(from, chat, Channel::Reliable);
        server->broadcast(Number{1}, Channel::Unreliable);
        if (chat.text == "bye") {
            server->disconnect(from);
        }
    });
    client->on<Chat>([&](ConnectionId, const Chat& chat) { echoed.push_back(chat.text); });
    sim.until([&] { return client->connected(); }, 2.0, *server, *client);
    client->send(CLIENT_CONNECTION, Chat{1, "one"});
    client->send(CLIENT_CONNECTION, Chat{1, "bye"});
    sim.run(0.5, 0.01, *server, *client);
    Assert(echoed.size() == 2 && echoed[0] == "one" && echoed[1] == "bye", "both were echoed, in order, before the kick");
    Assert(serverLog.disconnected.size() == 1 && serverLog.disconnected[0].second == DisconnectReason::Local, "the server ended it from a handler");
    Assert(!client->connected(), "and the client was told");
}

Test(endpoint, broadcast_can_skip_one)
{
    Sim sim;
    auto server = sim.server();
    auto a = sim.client();
    auto b = sim.client();
    auto c = sim.client();
    int gotA = 0, gotB = 0, gotC = 0;

    a->on<Number>([&](ConnectionId, const Number&) { ++gotA; });
    b->on<Number>([&](ConnectionId, const Number&) { ++gotB; });
    c->on<Number>([&](ConnectionId, const Number&) { ++gotC; });
    sim.run(0.5, 0.01, *server, *a, *b, *c);
    const auto ids = server->connections();

    AssertEq(server->broadcast(Number{1}), 3, "to all three");
    AssertEq(server->broadcast(Number{2}, Channel::Reliable, ids[1]), 2, "to two");
    sim.run(0.5, 0.01, *server, *a, *b, *c);
    AssertEq(gotA + gotB + gotC, 5, "5 messages in all");
    Assert(gotA == 2 || gotB == 2 || gotC == 2, "and one client got one only");
}

Test(endpoint, many_clients)
{
    Sim sim;
    auto server = sim.server();
    std::vector<std::unique_ptr<Endpoint>> clients;
    std::vector<int> counts(20, 0);
    std::set<std::uint32_t> heard;

    server->on<Number>([&](ConnectionId, const Number& number) { heard.insert(number.value); });
    for (std::size_t i = 0; i < counts.size(); ++i) {
        clients.push_back(sim.client());
        clients[i]->on<Number>([&counts, i](ConnectionId, const Number&) { ++counts[i]; });
    }
    for (int step = 0; step < 100; ++step) {
        sim.now += 0.01;
        server->poll();
        for (auto& client : clients) { client->poll(); }
    }
    for (std::size_t i = 0; i < clients.size(); ++i) {
        Assert(clients[i]->send(CLIENT_CONNECTION, Number{static_cast<std::uint32_t>(i)}), "client %zu is connected", i);
    }
    server->broadcast(Number{99});
    sim.until([&] { return heard.size() == 20; }, 1.0, *server);
    for (int step = 0; step < 50; ++step) {
        sim.now += 0.01;
        server->poll();
        for (auto& client : clients) { client->poll(); }
    }
    AssertEq(heard.size(), 20, "the server heard from all 20");
    AssertEq(std::count(counts.begin(), counts.end(), 1), 20, "each client got the broadcast once");
}

Test(endpoint, the_rtt_is_measured)
{
    Sim sim(LoopbackNetwork::Conditions{.latency = 0.05});
    auto server = sim.server();
    auto client = sim.client();

    sim.until([&] { return client->connected(); }, 5.0, *server, *client);
    Assert(!client->rtt(CLIENT_CONNECTION).has_value(), "no round trip is known before the first ack");
    for (int i = 0; i < 20; ++i) {
        client->send(CLIENT_CONNECTION, Number{1});
        sim.run(0.5, 0.01, *server, *client);
    }
    const auto rtt = client->rtt(CLIENT_CONNECTION);

    Assert(rtt.has_value() && *rtt > 0.09 && *rtt < 0.13, "about 2 x 50 ms: %f", rtt.value_or(0.0));
}

Test(endpoint, biggest_message)
{
    Sim sim;
    auto server = sim.server();
    auto client = sim.client();
    std::size_t got = 0;

    server->on<Chat>([&](ConnectionId, const Chat& chat) { got = chat.text.size(); });
    sim.until([&] { return client->connected(); }, 2.0, *server, *client);
    // (a packet is 8192 bytes at most: the header, the id of the message and the length of the text take some)
    Assert(client->send(CLIENT_CONNECTION, Chat{1, std::string(8192 - 14 - 4 - 4 - 4, 'x')}, Channel::Reliable), "the biggest goes");
    Assert(!client->send(CLIENT_CONNECTION, Chat{1, std::string(8192, 'x')}, Channel::Reliable), "a bigger one is refused");
    Assert(!client->send(CLIENT_CONNECTION, Chat{1, std::string(8192, 'x')}, Channel::Unreliable), "on any channel");
    sim.run(0.2, 0.01, *server, *client);
    AssertEq(got, 8192 - 14 - 4 - 4 - 4, "and it arrived whole");
}

Test(endpoint, garbage_is_dropped)
{
    Sim sim;
    auto server = sim.server();
    auto raw = sim.network.connect("room");       // something that is not an endpoint
    std::vector<TransportEvent> events;
    Log log;

    watch(*server, log);
    raw->poll(events);
    sim.run(0.1, 0.01, *server);
    const std::vector<std::vector<std::uint8_t>> junk = {
        {}, {0}, {99, 1, 2, 3}, {1}, {1, 0, 0, 0, 0}, {4, 1}, {4, 2, 0, 0, 0, 0, 0, 0, 0, 0}, {2, 1, 2, 3, 4}, {5},
    };

    for (const auto& packet : junk) {
        raw->send(CLIENT_CONNECTION, packet);
    }
    sim.run(0.2, 0.01, *server);
    Assert(log.connected.empty(), "no connection came out of it");
    Assert(server->stats().malformed >= 5, "the junk was counted: %llu", static_cast<unsigned long long>(server->stats().malformed));
    Assert(sim.until([&] { return true; }, 0.0, *server), "and the server is alive");
    // A real client can still come
    auto client = sim.client();

    Assert(sim.until([&] { return client->connected(); }, 2.0, *server, *client), "it still accepts a real client");
}

Test(endpoint, a_broken_message_is_dropped)
{
    Sim sim;
    auto server = sim.server();
    auto client = sim.client();
    int chats = 0;

    server->on<Chat>([&](ConnectionId, const Chat&) { ++chats; });
    sim.until([&] { return client->connected(); }, 2.0, *server, *client);
    // A message whose id is Chat's but whose fields are cut
    client->send(CLIENT_CONNECTION, Cut{});
    client->send(CLIENT_CONNECTION, Chat{1, "fine"});
    client->send(CLIENT_CONNECTION, Number{5}, Channel::Unreliable);      // nobody listens to Number here
    sim.run(0.3, 0.01, *server, *client);
    AssertEq(chats, 1, "only the good one reached the handler");
    AssertEq(server->stats().malformed, 1, "the cut one was counted");
    AssertEq(server->stats().unknownMessages, 1, "and the unknown one");
    Assert(server->connected(), "the connection is fine");
}

Test(endpoint, two_names_one_id)
{
    // Different types with the same id cannot listen on one endpoint: it says so at once
    Sim sim;
    auto server = sim.server();
    bool threw = false;

    server->on<A>([](ConnectionId, const A&) {});
    try { server->on<B>([](ConnectionId, const B&) {}); } catch (const std::logic_error&) { threw = true; }
    Assert(threw, "a collision is an error, not a wrong dispatch");
    server->on<A>([](ConnectionId, const A&) {});
}

// -- An endpoint ended by its own handlers ----------------------------------------------------------
// Handlers may end their endpoint: MatchmakingClient removes a room's endpoint from its Net when the room
// is lost. The endpoint must not touch itself once a handler has destroyed it.

Test(endpoint, a_handler_destroys_its_endpoint)
{
    Sim sim;
    auto server = sim.server();
    auto client = sim.client();
    bool told = false;

    sim.until([&] { return client->connected(); }, 2.0, *server, *client);
    client->onDisconnected([&](ConnectionId, DisconnectReason) {
        told = true;
        client.reset();
    });
    client->disconnect(CLIENT_CONNECTION);      // outside a poll: the handler runs in there
    Assert(told && !client, "the handler ran, and destroyed the endpoint");
    sim.run(0.1, 0.01, *server);
    Assert(!server->connected(), "the server was told");
}

Test(endpoint, net_remove_in_a_handler)
{
    Sim sim;
    auto server = sim.server();
    kuge::net::Net net;
    Endpoint& client = net.connect("room", sim.network, sim.config());
    bool told = false;

    sim.until([&] { return client.connected(); }, 2.0, *server, net);
    client.onDisconnected([&](ConnectionId, DisconnectReason) {
        told = true;
        net.remove(client);                     // what MatchmakingClient does
    });
    client.disconnect(CLIENT_CONNECTION);
    Assert(told && net.count() == 0, "the handler ran, and the Net let go of the endpoint");
}

Test(endpoint, destroyed_by_a_message)
{
    Sim sim;
    auto server = sim.server();
    auto client = sim.client();
    int heard = 0;

    sim.until([&] { return client->connected(); }, 2.0, *server, *client);
    client->on<Number>([&](ConnectionId, const Number&) {
        ++heard;
        client.reset();                         // "the game is over": the connection is dropped
    });
    const ConnectionId id = server->connections().front();

    server->send(id, Number{1});
    server->send(id, Number{2});
    for (int i = 0; i < 50 && client; ++i) {
        sim.now += 0.01;
        server->poll();
        client->poll();                         // both messages arrive in the same poll
    }
    Assert(!client, "the endpoint is gone");
    AssertEq(heard, 1, "and nothing of it ran after the handler that destroyed it");
}
