extern "C" {
    #include "kronklab/kronklab.h"
}
#include "server_fixture.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <set>
#include <sys/socket.h>
#include <unistd.h>

// NOTE: kronklab test names are limited to 31 characters.
// A real server (its lobby on the main thread of a thread of the test, its rooms on their own), and clients over a loopback.

namespace
{
    using namespace kuge;
    using State = net::MatchmakingClient::State;
    using server::ServerConfig;
    using server::RoomTypeConfig;
    using server::Transport;

    // A client that talks to the lobby by hand: to do what a well behaved client never does
    struct RawClient
    {
        net::Net                   net;
        net::Endpoint*             lobby = nullptr;
        net::Endpoint*             room = nullptr;
        std::vector<net::RoomAssigned> assigned;
        std::vector<net::JoinError>    refused;
        std::vector<net::HelloError>   rejected;
        std::vector<net::Welcome>      welcomes;
        std::vector<net::RoomEnd>      closed;
        bool                           roomLost = false;

        explicit RawClient(Harness& harness)
        {
            lobby = &net.connect("lobby", harness.network);
            lobby->on<net::RoomAssigned>([this](net::ConnectionId, const net::RoomAssigned& a) { assigned.push_back(a); });
            lobby->on<net::JoinRefused>([this](net::ConnectionId, const net::JoinRefused& r) { refused.push_back(r.reason); });
            lobby->on<net::RoomClosed>([this](net::ConnectionId, const net::RoomClosed& c) { closed.push_back(c.reason); });
        }

        bool until(const std::function<bool(void)>& done, double seconds = 20.0)
        {
            return waitUntil([this, &done] { net.poll(); return done(); }, seconds);
        }

        // Says hello to the room of the last assignment with a token
        void hello(Harness& harness, std::uint64_t token, const std::string& address)
        {
            room = &net.connect(address, harness.network);
            room->onConnected([this, token](net::ConnectionId id) { room->send(id, net::Hello{token}); });
            room->on<net::Welcome>([this](net::ConnectionId, const net::Welcome& w) { welcomes.push_back(w); });
            room->on<net::Rejected>([this](net::ConnectionId, const net::Rejected& r) { rejected.push_back(r.reason); });
            room->onDisconnected([this](net::ConnectionId, net::DisconnectReason) { roomLost = true; });
        }
    };
}

Test(server, two_clients_one_room)
{
    Harness harness;
    TestClient ana("Ana"), ben("Ben");

    ana.connect(harness);
    ben.connect(harness);
    ana.join();
    ben.join();
    Assert(ana.until([&] { ben.poll(); return ana.inRoom() && ben.inRoom(); }), "both are welcomed (ana %d, ben %d)", static_cast<int>(ana.matchmaking.state()), static_cast<int>(ben.matchmaking.state()));
    AssertEq(ana.roomId, ben.roomId, "in the same room");
    Assert(ana.networkId != ben.networkId && ana.networkId >= 1 && ben.networkId >= 1, "with their own network ids: %u and %u", ana.networkId, ben.networkId);
    AssertEq(ana.matchmaking.networkId(), ana.networkId, "the client knows its id");
    AssertEq(harness.server->stats().rooms.load(), 1, "one room runs");
    Assert(waitUntil([&] { return harness.server->stats().players == 2; }), "with two players");
    Assert(waitUntil([] { return roomLog().joinedCount() == 2; }), "and the room heard of both");
    std::lock_guard lock(roomLog().mutex);
    Assert(roomLog().threads.size() == 1 && roomLog().threads[0] != std::this_thread::get_id(), "the room runs on its own thread");
}

Test(server, the_game_talks)
{
    Harness harness;
    TestClient ana("Ana"), ben("Ben");

    ana.connect(harness);
    ben.connect(harness);
    ana.join();
    ben.join();
    Assert(ana.until([&] { ben.poll(); return ana.inRoom() && ben.inRoom(); }), "in the room");
    ana.matchmaking.room()->send(net::CLIENT_CONNECTION, Echo{"hello"});
    ben.matchmaking.room()->send(net::CLIENT_CONNECTION, Echo{"hi"});
    Assert(ana.until([&] { ben.poll(); return ana.echoes.size() == 1 && ben.echoes.size() == 1; }), "each gets its own echo");
    AssertStrEq(ana.echoes[0].c_str(), "Ana: hello", "the room knows who says it");
    AssertStrEq(ben.echoes[0].c_str(), "Ben: hi", "and who is who");
}

Test(server, a_full_room_makes_another)
{
    Harness harness;
    std::vector<std::unique_ptr<TestClient>> clients;

    for (const char* name : {"A", "B", "C", "D", "E"}) {
        clients.push_back(std::make_unique<TestClient>(name));
        clients.back()->connect(harness);
        clients.back()->join();
    }
    Assert(pumpAll(clients, [&] { return std::all_of(clients.begin(), clients.end(), [](auto& c) { return c->inRoom(); }); }), "all five are in");
    std::set<std::uint32_t> rooms;

    for (auto& client : clients) {
        rooms.insert(client->roomId);
    }
    AssertEq(rooms.size(), 3, "two players a room: 3 rooms for 5 players");
    AssertEq(harness.server->stats().rooms.load(), 3, "and the server counts 3");
}

Test(server, unknown_room_type)
{
    Harness harness;
    TestClient ana("Ana");

    ana.connect(harness);
    ana.join("chess");
    Assert(ana.until([&] { return !ana.failures.empty(); }), "refused");
    Assert(ana.inLobby(), "and it stays in the lobby");
    AssertStrEq(ana.failures[0].c_str(), "the server has no such kind of room", "with the reason");
    ana.join("duel");
    Assert(ana.until([&] { return ana.inRoom(); }), "and it can ask for another");
}

Test(server, no_more_rooms)
{
    Harness harness([](ServerConfig& c) { c.maxRooms = 1; }, RoomTypeConfig{.maxPlayers = 1});
    TestClient ana("Ana"), ben("Ben");

    ana.connect(harness);
    ben.connect(harness);
    ana.join();
    Assert(ana.until([&] { return ana.inRoom(); }), "the first is in");
    ben.join();
    Assert(ben.until([&] { return !ben.failures.empty(); }), "the second is refused");
    AssertStrEq(ben.failures[0].c_str(), "every room is full", "the server is full");
    Assert(ben.inLobby(), "it waits in the lobby");
}

Test(server, a_wrong_token)
{
    Harness harness;
    RawClient raw(harness);

    raw.lobby->onConnected([&raw](net::ConnectionId id) { raw.lobby->send(id, net::JoinRoom{"duel", "Mallory"}); });
    Assert(raw.until([&raw] { return !raw.assigned.empty(); }), "assigned a room");
    raw.hello(harness, raw.assigned[0].token ^ 0xFFFF, raw.assigned[0].address);
    Assert(raw.until([&raw] { return !raw.rejected.empty(); }), "a token that is not the right one is rejected");
    AssertEq(static_cast<int>(raw.rejected[0]), static_cast<int>(net::HelloError::UnknownToken), "as unknown");
    Assert(raw.until([&raw] { return raw.roomLost; }), "and dropped");
    AssertEq(raw.welcomes.size(), 0, "never welcomed");
    AssertEq(roomLog().joinedCount(), 0, "the room never saw a player");
}

Test(server, a_token_opens_once)
{
    Harness harness;
    RawClient raw(harness);

    raw.lobby->onConnected([&raw](net::ConnectionId id) { raw.lobby->send(id, net::JoinRoom{"duel", "Ana"}); });
    Assert(raw.until([&raw] { return !raw.assigned.empty(); }), "assigned");
    const auto assigned = raw.assigned[0];

    raw.hello(harness, assigned.token, assigned.address);
    Assert(raw.until([&raw] { return raw.welcomes.size() == 1; }), "the first hello is welcomed");
    // Someone who learned the token uses it too
    RawClient thief(harness);

    thief.hello(harness, assigned.token, assigned.address);
    Assert(thief.until([&thief] { return !thief.rejected.empty(); }), "the same token again is rejected");
    AssertEq(thief.welcomes.size(), 0, "no second welcome");
    Assert(roomLog().joinedCount() == 1, "the room has one player");
}

Test(server, hello_without_joining)
{
    Harness harness;
    TestClient ana("Ana");

    ana.connect(harness);
    ana.join();
    Assert(ana.until([&] { return ana.inRoom(); }), "one room exists");
    RawClient stranger(harness);

    stranger.hello(harness, 12345, "room-1");
    Assert(stranger.until([&stranger] { return !stranger.rejected.empty(); }), "a stranger with a made-up token is rejected");
    AssertEq(roomLog().joinedCount(), 1, "only Ana is in");
}

Test(server, silence_is_not_kept)
{
    Harness harness([](ServerConfig& c) { c.helloTimeout = 0.4; });
    TestClient ana("Ana");

    ana.connect(harness);
    ana.join();
    Assert(ana.until([&] { return ana.inRoom(); }), "the room exists");
    RawClient mute(harness);

    mute.room = &mute.net.connect("room-1", harness.network);    // connects, says nothing
    mute.room->onDisconnected([&mute](net::ConnectionId, net::DisconnectReason) { mute.roomLost = true; });
    Assert(mute.until([&mute] { return mute.roomLost; }, 5.0), "a connection that never says hello is dropped");
    Assert(ana.inRoom(), "and the players are not troubled");
}

Test(server, leaving_a_room)
{
    Harness harness;
    TestClient ana("Ana");

    ana.connect(harness);
    ana.join();
    Assert(ana.until([&] { return ana.inRoom(); }), "in");
    ana.matchmaking.leave();
    Assert(ana.inLobby(), "back in the lobby");
    Assert(waitUntil([] { return roomLog().leftCount() == 1; }), "the room saw her leave");
    Assert(waitUntil([&] { return harness.server->stats().players == 0; }), "the lobby too");
    ana.join();
    Assert(ana.until([&] { return ana.joined == 2 && ana.inRoom(); }), "she can join again");
}

Test(server, losing_the_lobby)
{
    Harness harness;
    auto ana = std::make_unique<TestClient>("Ana");
    TestClient ben("Ben");

    ana->connect(harness);
    ben.connect(harness);
    ana->join();
    ben.join();
    Assert(ana->until([&] { ben.poll(); return ana->inRoom() && ben.inRoom(); }), "both in");
    ana.reset();     // her lobby connection ends: she is out of the room
    Assert(waitUntil([] { return roomLog().leftCount() == 1; }), "the room lets her go");
    Assert(waitUntil([&] { ben.poll(); return harness.server->stats().players == 1; }), "and the lobby too");
    Assert(ben.inRoom(), "Ben is still there");
    TestClient cleo("Cleo");

    cleo.connect(harness);
    cleo.join();
    Assert(cleo.until([&] { ben.poll(); return cleo.inRoom(); }), "the free place goes to someone else");
    AssertEq(cleo.roomId, ben.roomId, "in the same room");
}

Test(server, a_brutal_disconnect)
{
    // The cable is cut: nothing says goodbye. The room finds out by the timeout.
    Harness harness([](ServerConfig& c) { c.endpoint.timeout = 1.0; });
    TestClient ana("Ana"), ben("Ben");

    ana.connect(harness);
    ben.connect(harness);
    ana.join();
    ben.join();
    Assert(ana.until([&] { ben.poll(); return ana.inRoom() && ben.inRoom(); }), "both in");
    harness.network.setConditions(net::LoopbackNetwork::Conditions{.loss = 1.0});
    Assert(waitUntil([] { return roomLog().leftCount() == 2; }, 10.0), "the room notices both after the timeout");
    {
        std::lock_guard lock(roomLog().mutex);
        // (the lobby, which times out at the same moment, may send her away first: Local)
        Assert(roomLog().left[0].second == net::DisconnectReason::Timeout || roomLog().left[0].second == net::DisconnectReason::Local, "by a timeout, or because the lobby lost her");
    }
    Assert(waitUntil([&] { return harness.server->stats().players == 0; }, 10.0), "and the lobby forgets them");
}

Test(server, the_game_ends)
{
    Harness harness;
    TestClient ana("Ana"), ben("Ben");

    ana.connect(harness);
    ben.connect(harness);
    ana.join();
    ben.join();
    Assert(ana.until([&] { ben.poll(); return ana.inRoom() && ben.inRoom(); }), "in the room");
    const auto first = ana.roomId;

    ana.matchmaking.room()->send(net::CLIENT_CONNECTION, FinishNow{});
    Assert(ana.until([&] { ben.poll(); return !ana.closed.empty() && !ben.closed.empty(); }), "both are told the room closed");
    AssertEq(static_cast<int>(ana.closed[0]), static_cast<int>(net::RoomEnd::GameOver), "the game is over");
    Assert(ana.inLobby() && ben.inLobby(), "and they are back in the lobby");
    Assert(waitUntil([&] { return harness.server->stats().rooms == 0; }), "the room is gone from the server");
    Assert(waitUntil([] { return roomLog().exited == 1; }), "its scene was left");
    Assert(waitUntil([&] { return harness.server->engine().spawned() == 0; }), "and its thread is over");
    ana.join();
    Assert(ana.until([&] { return ana.joined == 2 && ana.inRoom(); }), "a new game can start");
    Assert(ana.roomId != first, "in a new room");
}

Test(server, playing_again_at_once)
{
    // The room says the game is over, and the client asks for the next one at the very same moment:
    // the lobby may not have heard yet that the room ended, and must not answer "already in a room"
    Harness harness(nullptr, RoomTypeConfig{.maxPlayers = 1, .idleTimeout = 30.0});
    TestClient ana("Ana");

    ana.connect(harness);
    for (int game = 1; game <= 20; ++game) {
        ana.join();
        Assert(ana.until([&] { return ana.joined == game && ana.inRoom(); }), "game %d starts (failures so far: %zu)", game, ana.failures.size());
        ana.matchmaking.room()->send(net::CLIENT_CONNECTION, FinishNow{});
        Assert(ana.until([&] { return ana.inLobby() && ana.closed.size() == static_cast<std::size_t>(game); }), "game %d ends", game);
    }
    AssertEq(ana.failures.size(), 0, "and never refused");
}

Test(server, an_empty_room_closes)
{
    Harness harness(nullptr, RoomTypeConfig{.maxPlayers = 2, .idleTimeout = 0.3});
    TestClient ana("Ana");

    ana.connect(harness);
    ana.join();
    Assert(ana.until([&] { return ana.inRoom(); }), "in");
    ana.matchmaking.leave();
    Assert(waitUntil([&] { return harness.server->stats().rooms == 0; }, 5.0), "with nobody left, the room closes by itself");
    Assert(waitUntil([&] { return harness.server->engine().spawned() == 0; }), "and ends");
}

Test(server, a_client_that_never_comes)
{
    Harness harness([](ServerConfig& c) { c.tokenTtl = 0.3; });
    RawClient raw(harness);

    raw.lobby->onConnected([&raw](net::ConnectionId id) { raw.lobby->send(id, net::JoinRoom{"duel", "Ghost"}); });
    Assert(raw.until([&raw] { return !raw.assigned.empty(); }), "assigned a place");
    Assert(waitUntil([&] { return harness.server->stats().players == 1; }), "which is kept for it");
    Assert(raw.until([&] { return harness.server->stats().players == 0; }, 5.0), "but not for ever");
    Assert(!raw.closed.empty(), "and it is told");
}

Test(server, many_clients_many_rooms)
{
    Harness harness(nullptr, RoomTypeConfig{.maxPlayers = 4, .idleTimeout = 30.0});
    std::vector<std::unique_ptr<TestClient>> clients;

    for (int i = 0; i < 24; ++i) {
        clients.push_back(std::make_unique<TestClient>("P" + std::to_string(i)));
        clients.back()->connect(harness);
        clients.back()->join();
    }
    Assert(pumpAll(clients, [&] { return std::all_of(clients.begin(), clients.end(), [](auto& c) { return c->inRoom(); }); }), "24 clients are in a room");
    std::map<std::uint32_t, std::set<std::uint32_t>> ids;

    for (auto& client : clients) {
        ids[client->roomId].insert(client->networkId);
    }
    AssertEq(ids.size(), 6, "six rooms of four");
    for (const auto& [room, networkIds] : ids) {
        AssertEq(networkIds.size(), 4, "room %u: four different network ids", room);
    }
    for (auto& client : clients) {
        client->matchmaking.room()->send(net::CLIENT_CONNECTION, Echo{"x"});
    }
    Assert(pumpAll(clients, [&] { return std::all_of(clients.begin(), clients.end(), [](auto& c) { return c->echoes.size() == 1; }); }), "and every one gets its echo");
    std::lock_guard lock(roomLog().mutex);
    AssertEq(std::set<std::thread::id>(roomLog().threads.begin(), roomLog().threads.end()).size(), 6, "on six threads");
}

Test(server, pooled_rooms)
{
    Harness harness([](ServerConfig& c) { c.workers = 2; }, RoomTypeConfig{.maxPlayers = 2, .idleTimeout = 30.0, .policy = RunPolicy::Pooled});
    std::vector<std::unique_ptr<TestClient>> clients;

    for (int i = 0; i < 8; ++i) {
        clients.push_back(std::make_unique<TestClient>("P" + std::to_string(i)));
        clients.back()->connect(harness);
        clients.back()->join();
    }
    Assert(pumpAll(clients, [&] { return std::all_of(clients.begin(), clients.end(), [](auto& c) { return c->inRoom(); }); }), "8 clients in 4 pooled rooms");
    for (auto& client : clients) {
        client->matchmaking.room()->send(net::CLIENT_CONNECTION, Echo{"x"});
    }
    Assert(pumpAll(clients, [&] { return std::all_of(clients.begin(), clients.end(), [](auto& c) { return c->echoes.size() == 1; }); }), "and they talk");
    std::lock_guard lock(roomLog().mutex);
    Assert(std::set<std::thread::id>(roomLog().threads.begin(), roomLog().threads.end()).size() <= 2, "on the two workers");
}

Test(server, stopping_with_players)
{
    for (int round = 0; round < 15; ++round) {
        auto harness = std::make_unique<Harness>();
        TestClient ana("Ana"), ben("Ben");

        ana.connect(*harness);
        ben.connect(*harness);
        ana.join();
        ben.join();
        Assert(ana.until([&] { ben.poll(); return ana.inRoom() && ben.inRoom(); }), "round %d: in the room", round);
        harness.reset();     // the server stops with players in a room
        Assert(roomLog().exited == roomLog().entered, "round %d: the rooms were left", round);
        Assert(ana.until([&] { ben.poll(); return ana.matchmaking.state() == State::Failed && ben.matchmaking.state() == State::Failed; }, 5.0), "round %d: the clients see it end", round);
    }
}

// -- The real network -------------------------------------------------------------------------
namespace
{
    // A port of each kind that nobody uses, and a range of room ports after it
    struct Ports
    {
        std::uint16_t lobby = 0;
        std::uint16_t rooms = 0;
    };

    Ports freePorts(void)
    {
        std::mt19937 rng(std::random_device{}());

        for (int attempt = 0; attempt < 200; ++attempt) {
            const auto base = static_cast<std::uint16_t>(20000 + rng() % 20000);

            try {
                auto tcp = net::makeTcpServer(base);
                std::vector<std::unique_ptr<net::ITransport>> udp;

                for (std::uint16_t i = 1; i <= 9; ++i) {
                    udp.push_back(net::makeUdpServer(static_cast<std::uint16_t>(base + i)));
                }
                return Ports{base, static_cast<std::uint16_t>(base + 1)};
            } catch (const std::runtime_error&) {
            }
        }
        throw std::runtime_error("no free ports");
    }

    struct SocketServerRun
    {
        std::unique_ptr<server::GameServer> server;
        std::thread                         thread;

        explicit SocketServerRun(const Ports& ports, std::function<void(server::ServerConfig&)> tune = {})
        {
            server::ServerConfig config;

            config.transport = server::Transport::Sockets;
            config.lobbyPort = ports.lobby;
            config.roomPortFirst = ports.rooms;
            config.roomPortCount = 8;
            config.tickRate = 200;
            if (tune) {
                tune(config);
            }
            server = std::make_unique<server::GameServer>(config);
            server->addRoomType<TestRoom>("duel", server::RoomTypeConfig{.maxPlayers = 2});
            thread = std::thread([this] { server->run(); });
            waitUntil([this] { return server->stats().lobbyOpen.load(); });
        }

        ~SocketServerRun()
        {
            server->stop();
            thread.join();
        }
    };
}

Test(server, over_the_real_network)
{
    const Ports ports = freePorts();
    SocketServerRun run(ports);
    TestClient ana("Ana"), ben("Ben");

    ana.matchmaking.connectLobby(net::Protocol::Tcp, "127.0.0.1", ports.lobby);
    ben.matchmaking.connectLobby(net::Protocol::Tcp, "127.0.0.1", ports.lobby);
    ana.join();
    ben.join();
    Assert(ana.until([&] { ben.poll(); return ana.inRoom() && ben.inRoom(); }), "joined over tcp (lobby) and udp (room)");
    AssertEq(ana.roomId, ben.roomId, "in the same room");
    ana.matchmaking.room()->send(net::CLIENT_CONNECTION, Echo{"over the wire"});
    Assert(ana.until([&] { return ana.echoes.size() == 1; }), "the game talks");
    AssertStrEq(ana.echoes[0].c_str(), "Ana: over the wire", "");
    ana.matchmaking.room()->send(net::CLIENT_CONNECTION, FinishNow{});
    Assert(ana.until([&] { ben.poll(); return ana.inLobby() && ben.inLobby(); }), "the game ends and they are back in the lobby");
    ana.join();
    Assert(ana.until([&] { return ana.joined == 2 && ana.inRoom(); }), "and can play again");
}

Test(server, a_room_that_cannot_open)
{
    // The port of the first room is taken by someone else: that room fails, and the next one takes another port
    const Ports ports = freePorts();
    // (A plain socket: kronknet's own set SO_REUSEADDR, with which two UDP sockets can share a port)
    const int squatter = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in address = {};

    address.sin_family = AF_INET;
    address.sin_port = htons(ports.rooms);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    Assert(bind(squatter, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "the first port of the rooms is taken");
    SocketServerRun run(ports);
    TestClient ana("Ana");

    ana.matchmaking.connectLobby(net::Protocol::Tcp, "127.0.0.1", ports.lobby);
    ana.join();
    Assert(ana.until([&] { return !ana.failures.empty(); }), "the first attempt fails");
    AssertStrEq(ana.failures[0].c_str(), "the room could not start", "with a reason");
    Assert(ana.inLobby(), "the client is still in the lobby");
    ana.join();
    Assert(ana.until([&] { return ana.inRoom(); }), "and the next room, on another port, works");
    Assert(waitUntil([&] { return run.server->stats().rooms == 1; }), "one room runs");
    close(squatter);
}

Test(server, rooms_take_ports_turn)
{
    // Ports come back when a room ends
    const Ports ports = freePorts();
    SocketServerRun run(ports, [](server::ServerConfig& c) { c.roomPortCount = 1; });
    TestClient ana("Ana");

    ana.matchmaking.connectLobby(net::Protocol::Tcp, "127.0.0.1", ports.lobby);
    for (int game = 1; game <= 3; ++game) {
        ana.join();
        Assert(ana.until([&] { return ana.joined == game && ana.inRoom(); }), "game %d starts on the only port", game);
        ana.matchmaking.room()->send(net::CLIENT_CONNECTION, FinishNow{});
        Assert(ana.until([&] { return ana.inLobby(); }), "game %d ends", game);
        Assert(waitUntil([&] { return run.server->stats().rooms == 0; }), "and the port is free again");
    }
}
