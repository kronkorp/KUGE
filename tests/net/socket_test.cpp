extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Endpoint.hpp"
#include "SocketTransport.hpp"
#include "net_fixture.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <netinet/in.h>
#include <random>
#include <set>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

// NOTE: kronklab test names are limited to 31 characters.
// Real sockets, on the loopback interface of the machine.

namespace
{
    using namespace kuge::net;
    using Clock = std::chrono::steady_clock;

    // A server on a port that is free (kronknet cannot bind to port 0 and tell which one it got)
    struct Served
    {
        std::unique_ptr<ITransport> transport;
        std::uint16_t               port = 0;
    };

    Served serve(bool tcp)
    {
        thread_local std::mt19937 rng(std::random_device{}());

        for (int attempt = 0; attempt < 200; ++attempt) {
            const auto port = static_cast<std::uint16_t>(20000 + rng() % 30000);

            try {
                return Served{tcp ? makeTcpServer(port) : makeUdpServer(port), port};
            } catch (const std::runtime_error&) {
            }
        }
        throw std::runtime_error("no free port");
    }

    // Polls until done() or the time is up. Everything that is seen goes in the lists.
    struct Watch
    {
        std::vector<TransportEvent> serverEvents, clientEvents;
    };

    template<typename Done>
    bool pollUntil(ITransport& server, ITransport& client, Watch& watch, Done done, double seconds = 10.0)
    {
        const auto end = Clock::now() + std::chrono::duration<double>(seconds);

        while (!done() && Clock::now() < end) {
            server.poll(watch.serverEvents);
            client.poll(watch.clientEvents);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return done();
    }

    std::size_t count(const std::vector<TransportEvent>& events, TransportEvent::Kind kind)
    {
        return static_cast<std::size_t>(std::count_if(events.begin(), events.end(), [kind](const auto& e) { return e.kind == kind; }));
    }

    // A raw TCP connection, made by hand: to send what a well-behaved transport never would
    int rawConnect(std::uint16_t port)
    {
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address = {};

        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            close(fd);
            return -1;
        }
        return fd;
    }

    std::vector<std::uint8_t> patterned(std::size_t size, std::uint8_t seed)
    {
        std::vector<std::uint8_t> bytes(size);

        for (std::size_t i = 0; i < size; ++i) {
            bytes[i] = static_cast<std::uint8_t>(seed + i * 7);
        }
        return bytes;
    }
}

// -- The framer ------------------------------------------------------------------------------
Test(framer, whole_packets)
{
    StreamFramer framer;
    std::vector<std::uint8_t> packet;

    framer.push(StreamFramer::frame(patterned(10, 1)));
    Assert(framer.next(packet) && packet == patterned(10, 1), "a packet");
    Assert(!framer.next(packet), "no more");
    AssertEq(framer.pending(), 0, "nothing kept");
}

Test(framer, glued_packets)
{
    StreamFramer framer;
    std::vector<std::uint8_t> stream, packet;

    for (std::uint8_t i = 0; i < 5; ++i) {
        const auto framed = StreamFramer::frame(patterned(i + 1, i));

        stream.insert(stream.end(), framed.begin(), framed.end());
    }
    framer.push(stream);                    // five packets in one piece
    for (std::uint8_t i = 0; i < 5; ++i) {
        Assert(framer.next(packet) && packet == patterned(i + 1, i), "packet %d, in order", i);
    }
    Assert(!framer.next(packet), "and that is all");
}

Test(framer, one_byte_at_a_time)
{
    StreamFramer framer;
    std::vector<std::uint8_t> stream, packet;
    std::vector<std::vector<std::uint8_t>> got;

    for (std::uint8_t i = 0; i < 4; ++i) {
        const auto framed = StreamFramer::frame(patterned(100 + i, i));

        stream.insert(stream.end(), framed.begin(), framed.end());
    }
    for (const std::uint8_t byte : stream) {
        framer.push(std::span<const std::uint8_t>(&byte, 1));
        while (framer.next(packet)) {
            got.push_back(packet);
        }
    }
    AssertEq(got.size(), 4, "four packets came out of 1-byte pieces");
    Assert(got[3] == patterned(103, 3), "whole and right");
}

Test(framer, random_cuts)
{
    std::mt19937 rng(7);
    std::vector<std::vector<std::uint8_t>> sent;
    std::vector<std::uint8_t> stream;

    for (int i = 0; i < 300; ++i) {
        sent.push_back(patterned(1 + rng() % MAX_PACKET, static_cast<std::uint8_t>(i)));
        const auto framed = StreamFramer::frame(sent.back());

        stream.insert(stream.end(), framed.begin(), framed.end());
    }
    StreamFramer framer;
    std::vector<std::vector<std::uint8_t>> got;
    std::vector<std::uint8_t> packet;

    for (std::size_t at = 0; at < stream.size();) {
        const std::size_t piece = std::min<std::size_t>(1 + rng() % 5000, stream.size() - at);

        framer.push(std::span<const std::uint8_t>(stream.data() + at, piece));
        at += piece;
        while (framer.next(packet)) {
            got.push_back(packet);
        }
    }
    Assert(got == sent, "300 packets cut at random arrive as they were");
}

Test(framer, nonsense_lengths)
{
    StreamFramer zero;
    std::vector<std::uint8_t> packet;
    bool threw = false;

    zero.push(std::vector<std::uint8_t>{0, 0, 0, 0});
    try { zero.next(packet); } catch (const std::length_error&) { threw = true; }
    Assert(threw, "a packet of no bytes is not one");
    StreamFramer big;

    threw = false;
    big.push(std::vector<std::uint8_t>{0xFF, 0xFF, 0xFF, 0x7F, 1, 2, 3});
    try { big.next(packet); } catch (const std::length_error&) { threw = true; }
    Assert(threw, "2 GB is refused before anything is kept for it");
    StreamFramer limit;

    threw = false;
    limit.push(StreamFramer::frame(std::vector<std::uint8_t>(MAX_PACKET, 1)));
    Assert(limit.next(packet) && packet.size() == MAX_PACKET, "the biggest packet is fine");
    limit.push(std::vector<std::uint8_t>{0x01, 0x20, 0, 0});     // MAX_PACKET + 1
    try { limit.next(packet); } catch (const std::length_error&) { threw = true; }
    Assert(threw, "one byte more is not");
}

// -- TCP -------------------------------------------------------------------------------------
Test(tcp, packets_both_ways)
{
    auto [server, port] = serve(true);
    auto client = makeTcpClient("127.0.0.1", port);
    Watch watch;

    Assert(pollUntil(*server, *client, watch, [&] { return count(watch.serverEvents, TransportEvent::Kind::Connected) == 1 && count(watch.clientEvents, TransportEvent::Kind::Connected) == 1; }), "connected");
    const ConnectionId id = watch.serverEvents[0].connection;

    Assert(client->send(CLIENT_CONNECTION, patterned(100, 1)), "the client sends");
    Assert(server->send(id, patterned(200, 2)), "the server sends");
    Assert(pollUntil(*server, *client, watch, [&] { return count(watch.serverEvents, TransportEvent::Kind::Packet) == 1 && count(watch.clientEvents, TransportEvent::Kind::Packet) == 1; }), "both arrive");
    Assert(watch.serverEvents.back().packet == patterned(100, 1) && watch.clientEvents.back().packet == patterned(200, 2), "whole, and right");
    Assert(server->reliable() && client->reliable(), "tcp says it is reliable");
}

Test(tcp, a_thousand_packets)
{
    auto [server, port] = serve(true);
    auto client = makeTcpClient("127.0.0.1", port);
    Watch watch;
    std::mt19937 rng(3);
    std::vector<std::vector<std::uint8_t>> sent;

    Assert(pollUntil(*server, *client, watch, [&] { return count(watch.serverEvents, TransportEvent::Kind::Connected) == 1; }), "connected");
    for (int i = 0; i < 1000; ++i) {
        sent.push_back(patterned(1 + rng() % 3000, static_cast<std::uint8_t>(i)));
        // (the kernel's buffer can be full for a moment: try again after a poll)
        for (int attempt = 0; attempt < 1000 && !client->send(CLIENT_CONNECTION, sent.back()); ++attempt) {
            server->poll(watch.serverEvents);
            client->poll(watch.clientEvents);
        }
    }
    Assert(pollUntil(*server, *client, watch, [&] { return count(watch.serverEvents, TransportEvent::Kind::Packet) == 1000; }, 20.0), "all 1000 arrive");
    bool same = true;
    std::size_t next = 0;

    for (const auto& event : watch.serverEvents) {
        if (event.kind == TransportEvent::Kind::Packet) {
            same = same && event.packet == sent[next++];
        }
    }
    Assert(same, "each whole, in order, though TCP cut and glued them as it liked");
}

Test(tcp, the_biggest_packet)
{
    auto [server, port] = serve(true);
    auto client = makeTcpClient("127.0.0.1", port);
    Watch watch;

    Assert(pollUntil(*server, *client, watch, [&] { return count(watch.serverEvents, TransportEvent::Kind::Connected) == 1; }), "connected");
    Assert(client->send(CLIENT_CONNECTION, patterned(MAX_PACKET, 9)), "8192 bytes");
    Assert(!client->send(CLIENT_CONNECTION, patterned(MAX_PACKET + 1, 9)), "8193 are refused");
    Assert(pollUntil(*server, *client, watch, [&] { return count(watch.serverEvents, TransportEvent::Kind::Packet) == 1; }), "it arrives");
    Assert(watch.serverEvents.back().packet == patterned(MAX_PACKET, 9), "whole");
}

Test(tcp, nobody_listens)
{
    // A port where nobody listens: a server made then destroyed
    std::uint16_t port;
    {
        auto served = serve(true);

        port = served.port;
    }
    auto client = makeTcpClient("127.0.0.1", port);
    Watch watch;
    auto other = serve(true);

    Assert(pollUntil(*other.transport, *client, watch, [&] { return count(watch.clientEvents, TransportEvent::Kind::Disconnected) == 1; }), "the client learns it cannot connect");
}

Test(tcp, a_bad_address)
{
    auto client = makeTcpClient("not an address", 4000);
    std::vector<TransportEvent> events;

    client->poll(events);
    Assert(count(events, TransportEvent::Kind::Disconnected) == 1, "Disconnected, no exception");
}

Test(tcp, the_client_leaves)
{
    auto [server, port] = serve(true);
    auto client = makeTcpClient("127.0.0.1", port);
    Watch watch;

    Assert(pollUntil(*server, *client, watch, [&] { return count(watch.serverEvents, TransportEvent::Kind::Connected) == 1; }), "connected");
    const ConnectionId id = watch.serverEvents[0].connection;

    client.reset();
    Assert(pollUntil(*server, *server, watch, [&] { return count(watch.serverEvents, TransportEvent::Kind::Disconnected) >= 1; }), "the server sees it go");
    Assert(!server->send(id, patterned(3, 1)), "and cannot send to it");
}

Test(tcp, the_server_leaves)
{
    auto served = serve(true);
    auto client = makeTcpClient("127.0.0.1", served.port);
    Watch watch;

    Assert(pollUntil(*served.transport, *client, watch, [&] { return count(watch.clientEvents, TransportEvent::Kind::Connected) == 1 && count(watch.serverEvents, TransportEvent::Kind::Connected) == 1; }), "connected");
    served.transport.reset();
    std::vector<TransportEvent> events;
    const auto end = Clock::now() + std::chrono::seconds(10);

    while (count(events, TransportEvent::Kind::Disconnected) == 0 && Clock::now() < end) {
        client->poll(events);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Assert(count(events, TransportEvent::Kind::Disconnected) == 1, "the client sees the end of the stream");
}

Test(tcp, the_server_closes_one)
{
    auto [server, port] = serve(true);
    auto a = makeTcpClient("127.0.0.1", port);
    auto b = makeTcpClient("127.0.0.1", port);
    Watch watch;
    std::vector<TransportEvent> eventsB;

    Assert(pollUntil(*server, *a, watch, [&] { b->poll(eventsB); return count(watch.serverEvents, TransportEvent::Kind::Connected) == 2; }), "two connected");
    const ConnectionId first = watch.serverEvents[0].connection;

    server->close(first);
    Assert(pollUntil(*server, *a, watch, [&] { b->poll(eventsB); return count(watch.clientEvents, TransportEvent::Kind::Disconnected) + count(eventsB, TransportEvent::Kind::Disconnected) == 1; }), "one client sees it end");
}

Test(tcp, fragments_by_hand)
{
    // A peer that sends a packet in pieces, with pauses, and two packets glued: what TCP does to us
    auto [server, port] = serve(true);
    const int fd = rawConnect(port);
    std::vector<TransportEvent> events;

    Assert(fd >= 0, "a raw connection");
    const auto one = StreamFramer::frame(patterned(500, 1));
    auto two = StreamFramer::frame(patterned(20, 2));
    const auto three = StreamFramer::frame(patterned(30, 3));

    two.insert(two.end(), three.begin(), three.end());
    for (std::size_t at = 0; at < one.size(); at += 7) {
        const std::size_t piece = std::min<std::size_t>(7, one.size() - at);

        write(fd, one.data() + at, piece);
        server->poll(events);
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    AssertEq(count(events, TransportEvent::Kind::Packet), 1, "the first, once its last byte came");
    write(fd, two.data(), two.size());
    const auto end = Clock::now() + std::chrono::seconds(5);

    while (count(events, TransportEvent::Kind::Packet) < 3 && Clock::now() < end) {
        server->poll(events);
    }
    close(fd);
    std::vector<std::vector<std::uint8_t>> packets;

    for (const auto& event : events) {
        if (event.kind == TransportEvent::Kind::Packet) {
            packets.push_back(event.packet);
        }
    }
    Assert(packets.size() == 3 && packets[0] == patterned(500, 1) && packets[1] == patterned(20, 2) && packets[2] == patterned(30, 3), "three whole packets, from pieces and glue");
}

Test(tcp, a_hostile_length)
{
    auto [server, port] = serve(true);
    const int fd = rawConnect(port);
    std::vector<TransportEvent> events;
    const std::uint8_t evil[] = {0xFF, 0xFF, 0xFF, 0xFF, 1, 2, 3};

    Assert(fd >= 0, "a raw connection");
    write(fd, evil, sizeof(evil));
    const auto end = Clock::now() + std::chrono::seconds(5);

    while (count(events, TransportEvent::Kind::Disconnected) == 0 && Clock::now() < end) {
        server->poll(events);
    }
    Assert(count(events, TransportEvent::Kind::Disconnected) == 1, "the peer is dropped");
    Assert(count(events, TransportEvent::Kind::Packet) == 0, "nothing came out of it");
    char byte;
    const auto closedBy = Clock::now() + std::chrono::seconds(5);
    ssize_t got = 1;

    while (got > 0 && Clock::now() < closedBy) {
        server->poll(events);
        got = recv(fd, &byte, 1, MSG_DONTWAIT);
        if (got < 0 && errno == EAGAIN) { got = 1; }
    }
    Assert(got == 0, "and the connection is closed on its side too");
    close(fd);
}

// -- UDP -------------------------------------------------------------------------------------
Test(udp, packets_both_ways)
{
    auto [server, port] = serve(false);
    auto client = makeUdpClient("127.0.0.1", port);
    Watch watch;

    Assert(!server->reliable() && !client->reliable(), "udp says it is not reliable");
    Assert(pollUntil(*server, *client, watch, [&] { return count(watch.clientEvents, TransportEvent::Kind::Connected) == 1; }), "the client socket is made");
    Assert(client->send(CLIENT_CONNECTION, patterned(100, 1)), "the client sends");
    Assert(pollUntil(*server, *client, watch, [&] { return count(watch.serverEvents, TransportEvent::Kind::Packet) == 1; }), "the server gets it");
    AssertEq(count(watch.serverEvents, TransportEvent::Kind::Connected), 1, "and it saw the peer come");
    const ConnectionId id = watch.serverEvents[0].connection;

    Assert(server->send(id, patterned(300, 2)), "the server answers");
    Assert(pollUntil(*server, *client, watch, [&] { return count(watch.clientEvents, TransportEvent::Kind::Packet) == 1; }), "the client gets it");
    Assert(watch.serverEvents.back().packet == patterned(100, 1) && watch.clientEvents.back().packet == patterned(300, 2), "each datagram whole");
}

Test(udp, a_burst_in_one_poll)
{
    // kronknet reads one datagram per call: a transport must not leave the rest in the socket, or a peer that
    // sends more than it polls (an input each tick, plus acknowledgements) is heard later and later
    auto [server, port] = serve(false);
    auto client = makeUdpClient("127.0.0.1", port);
    Watch watch;

    Assert(pollUntil(*server, *client, watch, [&] { return count(watch.clientEvents, TransportEvent::Kind::Connected) == 1; }), "ready");
    for (std::uint8_t i = 0; i < 200; ++i) {
        Assert(client->send(CLIENT_CONNECTION, std::vector<std::uint8_t>(20, i)), "sent %d", i);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::vector<TransportEvent> events;

    server->poll(events);                                           // one poll
    AssertEq(count(events, TransportEvent::Kind::Packet), 200, "all 200 came out of one poll");
    // And the other way
    ConnectionId id = 0;

    for (const auto& event : events) {
        if (event.kind == TransportEvent::Kind::Connected) { id = event.connection; }
    }
    for (std::uint8_t i = 0; i < 100; ++i) {
        server->send(id, std::vector<std::uint8_t>(20, i));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    events.clear();
    client->poll(events);
    AssertEq(count(events, TransportEvent::Kind::Packet), 100, "and the client too");
}

Test(udp, the_biggest_datagram)
{
    auto [server, port] = serve(false);
    auto client = makeUdpClient("127.0.0.1", port);
    Watch watch;

    Assert(pollUntil(*server, *client, watch, [&] { return count(watch.clientEvents, TransportEvent::Kind::Connected) == 1; }), "ready");
    Assert(client->send(CLIENT_CONNECTION, patterned(MAX_PACKET, 4)), "8192 bytes");
    Assert(pollUntil(*server, *client, watch, [&] { return count(watch.serverEvents, TransportEvent::Kind::Packet) == 1; }), "it arrives");
    Assert(watch.serverEvents.back().packet == patterned(MAX_PACKET, 4), "whole");
}

Test(udp, several_clients)
{
    auto [server, port] = serve(false);
    std::vector<std::unique_ptr<ITransport>> clients;
    std::vector<TransportEvent> serverEvents;
    std::vector<std::vector<TransportEvent>> clientEvents(5);

    for (int i = 0; i < 5; ++i) {
        clients.push_back(makeUdpClient("127.0.0.1", port));
    }
    for (int round = 0; round < 50; ++round) {
        for (std::size_t i = 0; i < clients.size(); ++i) {
            clients[i]->poll(clientEvents[i]);
            if (round == 3) {
                clients[i]->send(CLIENT_CONNECTION, std::vector<std::uint8_t>(1, static_cast<std::uint8_t>(i)));
            }
        }
        server->poll(serverEvents);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    AssertEq(count(serverEvents, TransportEvent::Kind::Connected), 5, "five peers, told apart by address and port");
    std::set<ConnectionId> ids;
    for (const auto& event : serverEvents) {
        if (event.kind == TransportEvent::Kind::Packet) {
            ids.insert(event.connection);
        }
    }
    AssertEq(ids.size(), 5, "each packet is attributed to its peer");
    for (const auto id : ids) {
        server->send(id, std::vector<std::uint8_t>(1, static_cast<std::uint8_t>(id)));
    }
    for (int round = 0; round < 50; ++round) {
        for (std::size_t i = 0; i < clients.size(); ++i) {
            clients[i]->poll(clientEvents[i]);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (std::size_t i = 0; i < clients.size(); ++i) {
        AssertEq(count(clientEvents[i], TransportEvent::Kind::Packet), 1, "client %zu got its answer, and only its own", i);
    }
}

Test(udp, a_closed_address_comes_back)
{
    // The server closed a peer; the same address (a new endpoint on the same socket) says hello again: a new peer
    auto [server, port] = serve(false);
    auto client = makeUdpClient("127.0.0.1", port);
    Watch watch;

    Assert(pollUntil(*server, *client, watch, [&] { return count(watch.clientEvents, TransportEvent::Kind::Connected) == 1; }), "ready");
    client->send(CLIENT_CONNECTION, patterned(5, 1));
    Assert(pollUntil(*server, *client, watch, [&] { return count(watch.serverEvents, TransportEvent::Kind::Packet) == 1; }), "first hello");
    const ConnectionId first = watch.serverEvents[0].connection;

    server->close(first);
    Assert(!server->send(first, patterned(5, 1)), "no sending to a closed peer");
    client->send(CLIENT_CONNECTION, patterned(5, 2));
    Assert(pollUntil(*server, *client, watch, [&] { return count(watch.serverEvents, TransportEvent::Kind::Packet) == 2; }), "it says hello again");
    Assert(count(watch.serverEvents, TransportEvent::Kind::Connected) == 2, "as a new peer");
    Assert(watch.serverEvents.back().connection != first, "with a new id");
    Assert(server->send(watch.serverEvents.back().connection, patterned(5, 3)), "which can be sent to");
}

// -- An Endpoint over each -------------------------------------------------------------------
namespace
{
    void endpointsTalk(bool tcp)
    {
        auto [transport, port] = serve(tcp);
        Endpoint server(std::move(transport), Role::Server);
        Endpoint client(tcp ? makeTcpClient("127.0.0.1", port) : makeUdpClient("127.0.0.1", port), Role::Client);
        std::vector<std::uint32_t> got;
        std::vector<std::string> answers;

        server.on<Number>([&](ConnectionId from, const Number& number) {
            got.push_back(number.value);
            if (number.value == 499) {
                server.send(from, Chat{1, "done"});
            }
        });
        client.on<Chat>([&](ConnectionId, const Chat& chat) { answers.push_back(chat.text); });
        const auto end = Clock::now() + std::chrono::seconds(20);
        std::uint32_t sent = 0;

        while (answers.empty() && Clock::now() < end) {
            server.poll();
            client.poll();
            for (int i = 0; i < 10 && sent < 500 && client.connected(); ++i) {
                if (client.send(CLIENT_CONNECTION, Number{sent}, Channel::Reliable)) {
                    ++sent;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Assert(client.connected() && server.connected(), "connected (a real handshake, over %s)", tcp ? "tcp" : "udp");
        AssertEq(got.size(), 500, "500 reliable messages arrived");
        bool ordered = true;
        for (std::uint32_t i = 0; i < got.size(); ++i) {
            ordered = ordered && got[i] == i;
        }
        Assert(ordered, "in order, once each");
        Assert(answers.size() == 1 && answers[0] == "done", "and the answer came back");
        Assert(client.rtt(CLIENT_CONNECTION).has_value(), "with a round trip measured");
    }
}

Test(endpoint_tcp, handshake_and_messages)
{
    endpointsTalk(true);
}

Test(endpoint_udp, handshake_and_messages)
{
    endpointsTalk(false);
}

Test(endpoint_tcp, disconnect_is_seen)
{
    auto [transport, port] = serve(true);
    Endpoint server(std::move(transport), Role::Server);
    auto client = std::make_unique<Endpoint>(makeTcpClient("127.0.0.1", port), Role::Client);
    std::vector<DisconnectReason> reasons;
    const auto end = Clock::now() + std::chrono::seconds(10);

    server.onDisconnected([&](ConnectionId, DisconnectReason reason) { reasons.push_back(reason); });
    while (!(client->connected() && server.connected()) && Clock::now() < end) {
        server.poll();
        client->poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    client.reset();
    while (reasons.empty() && Clock::now() < end) {
        server.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Assert(reasons.size() == 1, "the server knows the client left, by the bye or the closed socket");
}

// A full server says no, and the client hears it (a closed socket is not enough: it would read "unreachable")
namespace
{
    void fullServerRefuses(bool tcp)
    {
        auto [transport, port] = serve(tcp);
        EndpointConfig small;
        small.maxConnections = 1;
        Endpoint server(std::move(transport), Role::Server, small);
        auto make = [&] { return std::make_unique<Endpoint>(tcp ? makeTcpClient("127.0.0.1", port) : makeUdpClient("127.0.0.1", port), Role::Client); };
        auto first = make();
        const auto end = Clock::now() + std::chrono::seconds(10);

        while (!(first->connected() && server.connected()) && Clock::now() < end) {
            server.poll();
            first->poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        auto second = make();
        std::vector<DisconnectReason> reasons;

        second->onDisconnected([&](ConnectionId, DisconnectReason reason) { reasons.push_back(reason); });
        while (reasons.empty() && Clock::now() < end) {
            server.poll();
            first->poll();
            second->poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Assert(first->connected() && server.connections().size() == 1, "the first is still the only one (%s)", tcp ? "tcp" : "udp");
        Assert(reasons.size() == 1 && reasons[0] == DisconnectReason::Refused, "the second is told that it was refused, not left to time out (%s)", tcp ? "tcp" : "udp");
    }
}

Test(endpoint_tcp, a_full_server_refuses)
{
    fullServerRefuses(true);
}

Test(endpoint_udp, a_full_server_refuses)
{
    fullServerRefuses(false);
}

Test(endpoint_udp, no_server_times_out)
{
    // Nobody answers a datagram: only the timeout tells
    EndpointConfig config;
    config.connectTimeout = 0.5;
    auto served = serve(false);
    const auto port = served.port;

    served.transport.reset();
    Endpoint client(makeUdpClient("127.0.0.1", port), Role::Client, config);
    std::vector<DisconnectReason> reasons;
    const auto end = Clock::now() + std::chrono::seconds(10);

    client.onDisconnected([&](ConnectionId, DisconnectReason reason) { reasons.push_back(reason); });
    while (reasons.empty() && Clock::now() < end) {
        client.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Assert(reasons.size() == 1 && (reasons[0] == DisconnectReason::Timeout || reasons[0] == DisconnectReason::Unreachable), "the client gives up (a timeout, or the refusal that the OS reports)");
}

// kronknet counts its connections in a variable that the whole process shares, without protection:
// servers on several threads, each accepting many connections, must not step on each other
Test(sockets, servers_on_threads)
{
    constexpr int THREADS = 4;
    constexpr int CLIENTS = 10;
    std::atomic<int> connected{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back([&connected, t] {
            const bool tcp = t % 2 == 0;
            Served served;

            try { served = serve(tcp); } catch (const std::runtime_error&) { return; }
            std::vector<std::unique_ptr<ITransport>> clients;
            std::vector<TransportEvent> events, ignored;

            for (int i = 0; i < CLIENTS; ++i) {
                clients.push_back(tcp ? makeTcpClient("127.0.0.1", served.port) : makeUdpClient("127.0.0.1", served.port));
            }
            const auto end = Clock::now() + std::chrono::seconds(10);
            std::size_t peers = 0;

            while (peers < CLIENTS && Clock::now() < end) {
                for (auto& client : clients) {
                    client->poll(ignored);
                    client->send(CLIENT_CONNECTION, std::vector<std::uint8_t>(1, 7));   // (UDP: the server only sees a client that speaks)
                }
                served.transport->poll(events);
                peers = static_cast<std::size_t>(std::count_if(events.begin(), events.end(), [](const auto& e) { return e.kind == TransportEvent::Kind::Connected; }));
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            connected += static_cast<int>(peers);
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    AssertEq(connected.load(), THREADS * CLIENTS, "every connection of every server was made");
}
