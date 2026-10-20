extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Auth.hpp"
#include "net_fixture.hpp"
#include <algorithm>
#include <cstring>

// NOTE: kronklab test names are limited to 31 characters.
// The handshake that proves an address, and the tag of each packet (ticket 038). The attackers here are raw transports
// that write the packets by hand.

namespace
{
    using namespace kuge::net;
    using Packet = std::vector<std::uint8_t>;

    struct Log
    {
        std::vector<ConnectionId>                              connected;
        std::vector<std::pair<ConnectionId, DisconnectReason>> disconnected;
    };

    void watch(Endpoint& endpoint, Log& log)
    {
        endpoint.onConnected([&log](ConnectionId id) { log.connected.push_back(id); });
        endpoint.onDisconnected([&log](ConnectionId id, DisconnectReason reason) { log.disconnected.emplace_back(id, reason); });
    }

    constexpr std::uint8_t CONNECT = 1, ACCEPT = 2, REJECT = 3, DATA = 4, GOODBYE = 5, CHALLENGE = 6;
    constexpr std::uint32_t MAGIC = 0x4547554Bu;

    void put(Packet& out, std::uint64_t value, int bytes)
    {
        for (int i = 0; i < bytes; ++i) {
            out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        }
    }

    std::uint64_t get(const Packet& in, std::size_t at, int bytes)
    {
        std::uint64_t value = 0;

        for (int i = 0; i < bytes; ++i) {
            value |= static_cast<std::uint64_t>(in.at(at + static_cast<std::size_t>(i))) << (8 * i);
        }
        return value;
    }

    Packet connectPacket(std::uint64_t salt, std::uint64_t cookie = 0, std::uint16_t version = EndpointConfig{}.protocol)
    {
        Packet packet{CONNECT};

        put(packet, MAGIC, 4);
        put(packet, version, 2);
        put(packet, salt, 8);
        put(packet, cookie, 8);
        return packet;
    }

    // Someone who writes the packets himself, from an address of his own (a link of the loopback)
    struct Raw
    {
        std::unique_ptr<ITransport> link;
        std::vector<Packet>         received;

        explicit Raw(Sim& sim, const char* name = "room") : link(sim.network.connect(name)) { poll(); }

        void poll(void)
        {
            std::vector<TransportEvent> events;

            link->poll(events);
            for (auto& event : events) {
                if (event.kind == TransportEvent::Kind::Packet) {
                    received.push_back(std::move(event.packet));
                }
            }
        }

        void send(const Packet& packet) { link->send(CLIENT_CONNECTION, packet); }

        // The packets that came since the last call
        std::vector<Packet> take(void)
        {
            poll();
            std::vector<Packet> taken;

            taken.swap(received);
            return taken;
        }
    };

    // The transport of an endpoint, with a man in the middle: it can spoil what the endpoint sends, and slip in packets
    class Tamper final : public ITransport
    {
        public:
            explicit Tamper(std::unique_ptr<ITransport> inner) : m_inner(std::move(inner)) {}

            void poll(std::vector<TransportEvent>& out) override
            {
                m_inner->poll(out);
                for (auto& packet : m_slipped) {
                    out.push_back(TransportEvent{TransportEvent::Kind::Packet, CLIENT_CONNECTION, std::move(packet), {}});
                }
                m_slipped.clear();
            }

            bool send(ConnectionId connection, std::span<const std::uint8_t> packet) override
            {
                Packet copy(packet.begin(), packet.end());

                if (spoilData && copy[0] == DATA) {
                    copy.back() ^= 0xFF;   // the tag is wrong
                }
                return m_inner->send(connection, copy);
            }

            void close(ConnectionId connection) override { m_inner->close(connection); }
            bool reliable(void) const noexcept override { return false; }
            std::string describe(void) const override { return "tamper"; }

            void slipIn(Packet packet) { m_slipped.push_back(std::move(packet)); }              // to the endpoint, as if from the server
            void sendAsClient(const Packet& packet) { m_inner->send(CLIENT_CONNECTION, packet); }   // to the server, as if from the endpoint

            bool spoilData = false;

        private:
            std::unique_ptr<ITransport> m_inner;
            std::vector<Packet>         m_slipped;
    };

    // A client and a server that are connected, with a tamperer on the client's side
    struct Pair
    {
        Sim                        sim;
        std::unique_ptr<Endpoint>  server;
        std::unique_ptr<Endpoint>  client;
        Tamper*                    tamper = nullptr;
        std::vector<std::string>   heard;   //!< What the server received
        Log                        clientLog;

        explicit Pair(EndpointConfig serverConfig = {})
        {
            server = sim.server("room", serverConfig);
            auto spy = std::make_unique<Tamper>(sim.network.connect("room"));
            EndpointConfig config = sim.config();

            tamper = spy.get();
            client = std::make_unique<Endpoint>(std::move(spy), Role::Client, config);
            server->on<Chat>([this](ConnectionId, const Chat& chat) { heard.push_back(chat.text); });
            watch(*client, clientLog);
            sim.until([&] { return client->connected() && server->connected(); }, 5.0, *server, *client);
        }

        void run(double seconds) { sim.run(seconds, 0.01, *server, *client); }
    };
}

Test(handshake, a_spoofed_connect_is_free)
{
    // A Connect from an address that never answers: it gets a small Challenge, and the server keeps no connection for it
    Sim sim;
    auto server = sim.server();
    Log log;
    Raw ghost(sim);

    watch(*server, log);
    AssertEq(server->halfOpen(), 0, "nobody is there");
    ghost.send(connectPacket(0x1122334455667788ull));
    server->poll();
    const auto replies = ghost.take();

    AssertEq(replies.size(), 1, "one answer");
    AssertEq(replies[0][0], CHALLENGE, "a Challenge");
    Assert(replies[0].size() < connectPacket(0).size(), "smaller than the Connect (%zu bytes for %zu): nothing is amplified", replies[0].size(), connectPacket(0).size());
    AssertEq(get(replies[0], 1, 8), 0x1122334455667788ull, "it repeats the salt");
    AssertEq(server->stats().challenges, 1, "counted");
    AssertEq(server->halfOpen(), 1, "the address is there, and is nothing more");
    sim.run(30.0, 0.1, *server);   // the ghost never answers
    AssertEq(server->halfOpen(), 0, "and it goes after the handshake's time");
    Assert(log.connected.empty() && log.disconnected.empty() && !server->connected(), "no connection was ever made, and none is reported");
    AssertEq(server->connections().size(), 0, "none to send to");
}

Test(handshake, a_reply_is_smaller)
{
    // Whatever the server answers to a Connect is smaller than the Connect: an address that is forged gets less than was sent
    Sim sim;
    auto server = sim.server();
    Raw one(sim), two(sim), three(sim), four(sim);

    one.send(connectPacket(7));
    two.send(connectPacket(7, 0, 1));   // another version of the protocol
    three.send(connectPacket(7, 0xBAD, EndpointConfig{}.protocol));   // a cookie that is not ours
    four.send(Packet{CONNECT, 'K', 'U', 'G', 'E'});   // cut short
    server->poll();
    for (Raw* raw : {&one, &two, &three, &four}) {
        for (const auto& reply : raw->take()) {
            Assert(reply.size() < connectPacket(0).size(), "a reply of %zu bytes, type %u", reply.size(), reply[0]);
        }
    }
    AssertEq(one.received.size() + two.received.size(), 0, "(all taken)");
    Raw five(sim);

    five.send(connectPacket(7, 0, 1));
    server->poll();
    const auto rejected = five.take();

    Assert(rejected.size() == 1 && rejected[0][0] == REJECT, "another version is refused with a Reject");
    Raw six(sim);

    six.send(Packet(400, CONNECT));   // junk
    server->poll();
    AssertEq(six.take().size(), 0, "junk gets no answer");
    Assert(server->stats().malformed >= 2, "and is counted");
}

Test(handshake, the_cookie_connects)
{
    Sim sim;
    auto server = sim.server();
    Log log;
    Raw raw(sim);

    watch(*server, log);
    raw.send(connectPacket(99));
    server->poll();
    const auto challenge = raw.take();

    AssertEq(challenge.size(), 1, "challenged");
    const std::uint64_t cookie = get(challenge[0], 9, 8);

    AssertEq(log.connected.size(), 0, "not connected yet");
    raw.send(connectPacket(99, cookie));
    server->poll();
    const auto accept = raw.take();

    AssertEq(accept.size(), 1, "answered");
    AssertEq(accept[0][0], ACCEPT, "an Accept");
    Assert(accept[0].size() < connectPacket(0).size(), "smaller than the Connect");
    AssertEq(get(accept[0], 1, 8), 99, "for the salt");
    AssertEq(get(accept[0], 9, 8), cookie, "with the cookie, the other half of the key");
    AssertEq(log.connected.size(), 1, "and the connection is made");
    raw.send(connectPacket(99, cookie));
    server->poll();
    AssertEq(raw.take().size(), 1, "the Connect again (the Accept was lost): another Accept");
    AssertEq(log.connected.size(), 1, "but not a second connection");
}

Test(handshake, a_cookie_is_for_one_address)
{
    Sim sim;
    auto server = sim.server();
    Log log;
    Raw a(sim), b(sim);

    watch(*server, log);
    a.send(connectPacket(5));
    server->poll();
    const std::uint64_t cookie = get(a.take().at(0), 9, 8);

    b.send(connectPacket(5, cookie));   // someone else's cookie, same salt
    a.send(connectPacket(6, cookie));   // its own, but for another salt
    a.send(connectPacket(5, cookie ^ 1));
    server->poll();
    for (const auto& reply : b.take()) {
        AssertEq(reply[0], CHALLENGE, "the cookie of another address is no cookie");
    }
    for (const auto& reply : a.take()) {
        AssertEq(reply[0], CHALLENGE, "nor is one for another salt, or a changed one");
    }
    AssertEq(log.connected.size(), 0, "nobody was connected");
}

Test(handshake, a_cookie_runs_out)
{
    // The cookie is good for the window it was made in, and the next one
    for (const double later : {0.9, 1.4, 2.6}) {
        Sim sim;
        EndpointConfig config;

        config.cookieWindow = 1.0;
        auto server = sim.server("room", config);
        Log log;
        Raw raw(sim);

        watch(*server, log);
        sim.now = 0.5;
        raw.send(connectPacket(1));
        server->poll();
        const std::uint64_t cookie = get(raw.take().at(0), 9, 8);

        sim.now = 0.5 + later;
        raw.send(connectPacket(1, cookie));
        server->poll();
        const auto reply = raw.take();

        AssertEq(reply.size(), 1, "an answer after %.1f s", later);
        Assert(reply[0][0] == (later < 2.0 ? ACCEPT : CHALLENGE), "%.1f s later: %s", later, later < 2.0 ? "still good" : "too old: challenged again");
    }
}

Test(handshake, a_lost_challenge_is_retried)
{
    // The first Challenge is lost: the client asks again, and so on, until it is in. Over a network that loses half of it.
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
        Sim sim(LoopbackNetwork::Conditions{.loss = 0.5, .seed = seed});
        EndpointConfig config;

        config.connectTimeout = 30.0;
        auto server = sim.server("room", config);
        auto client = sim.client("room", config);

        Assert(sim.until([&] { return client->connected() && server->connected(); }, 30.0, *server, *client), "seed %llu: connected", static_cast<unsigned long long>(seed));
        AssertEq(server->connections().size(), 1, "seed %llu: one connection", static_cast<unsigned long long>(seed));
    }
}

Test(handshake, one_address_is_limited)
{
    Sim sim;
    EndpointConfig config;

    config.connectsPerSecond = 10;
    auto server = sim.server("room", config);
    Raw flood(sim), other(sim);

    for (int i = 0; i < 1000; ++i) {
        flood.send(connectPacket(static_cast<std::uint64_t>(i)));
    }
    other.send(connectPacket(1));
    server->poll();
    AssertEq(server->stats().challenges, 11, "10 from the flood, and one from the other address");
    AssertEq(server->stats().rateLimited, 990, "the rest was dropped");
    AssertEq(flood.take().size(), 10, "and unanswered");
    AssertEq(other.take().size(), 1, "an address has its own count");
    sim.now += 1.5;
    flood.send(connectPacket(5000));
    server->poll();
    AssertEq(flood.take().size(), 1, "a second later, it is heard again");
}

Test(handshake, half_open_never_fills_it)
{
    // A server that takes 2 connections, and a crowd of addresses that never answer: the real clients still get in
    Sim sim;
    EndpointConfig config;

    config.maxConnections = 2;
    config.maxHalfOpen = 8;
    auto server = sim.server("room", config);
    std::vector<std::unique_ptr<Raw>> crowd;

    for (int i = 0; i < 100; ++i) {
        crowd.push_back(std::make_unique<Raw>(sim));
        crowd.back()->send(connectPacket(static_cast<std::uint64_t>(i + 1)));
        server->poll();
    }
    AssertEq(server->stats().challenges, 100, "each was challenged");
    AssertEq(server->halfOpen(), 8, "but the server keeps 8 of them, the latest");
    auto first = sim.client("room", config);
    auto second = sim.client("room", config);

    Assert(sim.until([&] { return first->connected() && second->connected(); }, 5.0, *server, *first, *second), "and two real clients are connected");
    AssertEq(server->connections().size(), 2, "they took the places");
    auto third = sim.client("room", config);
    Log log;

    watch(*third, log);
    sim.run(2.0, 0.01, *server, *first, *second, *third);
    Assert(log.disconnected.size() == 1 && log.disconnected[0].second == DisconnectReason::Refused, "the third is refused: the places are for the ones who answered");
}

Test(handshake, packets_carry_a_tag)
{
    Pair pair;

    Assert(pair.client->send(CLIENT_CONNECTION, Chat{1, "first"}, Channel::Unreliable), "sent");
    pair.run(0.2);
    AssertEq(pair.heard.size(), 1, "and heard, with its tag");
    pair.tamper->spoilData = true;
    pair.client->send(CLIENT_CONNECTION, Chat{1, "second"}, Channel::Unreliable);
    pair.run(0.2);
    AssertEq(pair.heard.size(), 1, "a packet whose tag is wrong is not heard");
    Assert(pair.server->stats().malformed > 0, "it is counted: %llu", static_cast<unsigned long long>(pair.server->stats().malformed));
    pair.tamper->spoilData = false;
    pair.client->send(CLIENT_CONNECTION, Chat{1, "third"}, Channel::Unreliable);
    pair.run(0.2);
    AssertEq(pair.heard.size(), 2, "and the next is, the connection is the same");
    AssertEq(pair.heard.back(), "third", "the third");
}

Test(handshake, a_forged_packet_is_dropped)
{
    // Someone who knows the address and the format, but not the key: he writes a Data packet by hand
    Pair pair;
    Packet forged{DATA, 0};   // unreliable
    kuge::ByteWriter message;

    put(forged, 0, 4);        // ack
    put(forged, 0, 4);        // ack bits
    message.write<std::uint32_t>(Chat::kugeMessageId);
    encode(message, Chat{1, "forged"});
    forged.insert(forged.end(), message.bytes().begin(), message.bytes().end());
    put(forged, 0x12345678, 4);   // a tag, made up
    pair.tamper->sendAsClient(forged);
    pair.run(0.2);
    AssertEq(pair.heard.size(), 0, "a made-up tag is not the right one");
    Assert(pair.server->stats().malformed > 0, "counted");
    // The same for the other direction, and for a Goodbye
    Packet goodbye{GOODBYE};

    put(goodbye, 0xDEADBEEF, 4);
    pair.tamper->slipIn(goodbye);
    Packet stranger{DATA, 2};

    put(stranger, 0, 8);
    put(stranger, 0xCAFEF00D, 4);
    pair.tamper->slipIn(stranger);
    pair.run(0.2);
    Assert(pair.client->connected() && pair.clientLog.disconnected.empty(), "a forged Goodbye does not end the connection");
    Assert(pair.client->stats().malformed >= 2, "the client counted both");
    pair.client->send(CLIENT_CONNECTION, Chat{1, "still here"}, Channel::Unreliable);
    pair.run(0.2);
    AssertEq(pair.heard.size(), 1, "and the connection works");
}

Test(handshake, a_forged_reject_or_accept)
{
    Pair pair;
    Packet reject{REJECT};
    Packet accept{ACCEPT};

    put(reject, 0, 4);
    put(accept, 0, 8);
    put(accept, 0, 8);
    pair.tamper->slipIn(reject);
    pair.tamper->slipIn(accept);
    pair.tamper->slipIn(Packet{CHALLENGE, 1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 4, 5, 6, 7, 8});
    pair.run(0.5);
    Assert(pair.client->connected() && pair.clientLog.disconnected.empty(), "a connection that is made is not ended by a Reject (it has no tag)");
    pair.client->send(CLIENT_CONNECTION, Chat{1, "alive"}, Channel::Reliable);
    pair.run(0.5);
    AssertEq(pair.heard.size(), 1, "and it works");
}

Test(handshake, an_old_client_is_refused)
{
    // A client of the former protocol (11 bytes of Connect, a salt of 32 bits) gets a clean refusal, not silence
    Sim sim;
    auto server = sim.server();
    Raw old(sim);
    Packet connect{CONNECT};

    put(connect, MAGIC, 4);
    put(connect, 1, 2);       // version 1
    put(connect, 0xAABBCCDD, 4);
    old.send(connect);
    server->poll();
    const auto reply = old.take();

    Assert(reply.size() == 1 && reply[0][0] == REJECT, "Reject");
    Assert(reply[0].size() <= connect.size(), "and not bigger than what was asked");
}

Test(handshake, a_full_set_of_connections)
{
    // The tag is 4 bytes: the biggest message is 4 bytes smaller than it was
    Pair pair;
    const std::size_t room = 8192 - 14 - 4 - 4 - 4 - 4;

    Assert(pair.client->send(CLIENT_CONNECTION, Chat{1, std::string(room, 'x')}, Channel::Reliable), "the biggest goes");
    Assert(!pair.client->send(CLIENT_CONNECTION, Chat{1, std::string(room + 1, 'x')}, Channel::Reliable), "one byte more does not");
    pair.run(0.3);
    AssertEq(pair.heard.size(), 1, "and it arrived");
    AssertEq(pair.heard[0].size(), room, "whole");
}

Test(handshake, the_tag_is_as_documented)
{
    // What docs/19 says of the tag, checked against the endpoint: a raw client that knows its salt and the cookie
    Sim sim;
    auto server = sim.server();
    std::vector<std::string> heard;
    Raw raw(sim);
    const std::uint64_t salt = 0x1122334455667788ull, fromServer = 0x5352565253525652ull;

    server->on<Chat>([&](ConnectionId, const Chat& chat) { heard.push_back(chat.text); });
    raw.send(connectPacket(salt));
    server->poll();
    const std::uint64_t cookie = get(raw.take().at(0), 9, 8);

    raw.send(connectPacket(salt, cookie));
    server->poll();
    Assert(server->connected(), "connected");
    raw.take();
    // The server has nothing to say for a second: it sends a keep-alive, whose tag is the documented one
    sim.run(1.2, 0.1, *server);
    const auto sent = raw.take();

    Assert(!sent.empty() && sent[0][0] == DATA && sent[0][1] == 2, "a keep-alive");
    const Packet& keepAlive = sent[0];
    const auto body = std::span<const std::uint8_t>(keepAlive.data(), keepAlive.size() - 4);

    AssertEq(get(keepAlive, keepAlive.size() - 4, 4), static_cast<std::uint32_t>(sipHash24(salt ^ fromServer, cookie, body)), "tagged with the salt xor FROM_SERVER, and the cookie");
    // And a packet that the client tags the documented way is accepted
    Packet data{DATA, 0};
    kuge::ByteWriter message;

    put(data, 0, 4);
    put(data, 0, 4);
    message.write<std::uint32_t>(Chat::kugeMessageId);
    encode(message, Chat{1, "hello"});
    data.insert(data.end(), message.bytes().begin(), message.bytes().end());
    put(data, static_cast<std::uint32_t>(sipHash24(salt, cookie, data)), 4);
    raw.send(data);
    server->poll();
    sim.run(0.1, 0.01, *server);
    Assert(heard.size() == 1 && heard[0] == "hello", "the server heard it");
}
