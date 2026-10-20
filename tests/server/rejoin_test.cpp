extern "C" {
    #include "kronklab/kronklab.h"
}
#include "server_fixture.hpp"

// NOTE: kronklab test names are limited to 31 characters.
// A player whose connection is lost keeps its place for a while, and comes back as the same player.

namespace
{
    using namespace kuge;
    using State = net::MatchmakingClient::State;

    // Cuts the cable until the room has noticed that these two are gone and they have noticed it too, then mends it
    void cutTheCable(Harness& harness, TestClient& ana, TestClient& ben, std::size_t away)
    {
        harness.network.setConditions(net::LoopbackNetwork::Conditions{.loss = 1.0});
        waitUntil([&] {
            ana.poll();
            ben.poll();
            return roomLog().awayCount() >= away && ana.matchmaking.state() == State::Failed && ben.matchmaking.state() == State::Failed;
        }, 15.0);
        harness.network.setConditions(net::LoopbackNetwork::Conditions{});
    }
}

Test(rejoin, the_place_is_kept)
{
    Harness harness([](server::ServerConfig& c) { c.endpoint.timeout = 1.0; }, server::RoomTypeConfig{.maxPlayers = 2, .idleTimeout = 30.0, .reconnectSeconds = 4.0});
    TestClient ana("Ana"), ben("Ben");

    ana.connect(harness, 1.0);
    ben.connect(harness, 1.0);
    ana.join();
    ben.join();
    Assert(ana.until([&] { ben.poll(); return ana.inRoom() && ben.inRoom(); }), "both in");
    const std::uint32_t networkId = ana.networkId;
    const std::uint32_t roomId = ana.roomId;

    Assert(ana.matchmaking.canRejoin(), "the client keeps what it needs to come back");
    cutTheCable(harness, ana, ben, 2);
    Assert(roomLog().awayCount() == 2 && roomLog().leftCount() == 0, "the room says both are away, and nobody has left");
    Assert(harness.server->stats().players == 2, "the lobby keeps both places");
    ana.connect(harness, 1.0);
    ana.matchmaking.rejoin();
    Assert(ana.until([&] { return ana.inRoom() && ana.joined == 2; }), "Ana is back in the room");
    AssertEq(ana.networkId, networkId, "with the same network id");
    AssertEq(ana.roomId, roomId, "in the same room");
    Assert(waitUntil([] { return roomLog().rejoinedCount() == 1; }), "the room says she is back");
    {
        std::lock_guard lock(roomLog().mutex);
        Assert(roomLog().rejoined[0].first == "Ana" && roomLog().rejoined[0].second == networkId, "the same player: %u", roomLog().rejoined[0].second);
        Assert(roomLog().joined.size() == 2, "(it was not a new player: only the two first joined)");
    }
    ana.matchmaking.room()->send(net::CLIENT_CONNECTION, Echo{"back"});
    Assert(ana.until([&] { return !ana.echoes.empty(); }), "and the room talks to her");
    AssertStrEq(ana.echoes.back().c_str(), "Ana: back", "as Ana");
}

Test(rejoin, the_time_runs_out)
{
    Harness harness([](server::ServerConfig& c) { c.endpoint.timeout = 1.0; }, server::RoomTypeConfig{.maxPlayers = 2, .idleTimeout = 30.0, .reconnectSeconds = 2.0});
    TestClient ana("Ana"), ben("Ben");

    ana.connect(harness, 1.0);
    ben.connect(harness, 1.0);
    ana.join();
    ben.join();
    Assert(ana.until([&] { ben.poll(); return ana.inRoom() && ben.inRoom(); }), "both in");
    cutTheCable(harness, ana, ben, 2);
    Assert(waitUntil([] { return roomLog().leftCount() == 2; }, 10.0), "after the time of the room, both are gone for good");
    Assert(waitUntil([&] { return harness.server->stats().players == 0; }, 10.0), "and the lobby forgets them");
    ana.connect(harness, 1.0);
    ana.matchmaking.rejoin();
    Assert(ana.until([&] { return !ana.failures.empty() && ana.inLobby(); }), "too late: the place is gone");
    Assert(!ana.matchmaking.canRejoin(), "and the client knows there is nothing to come back to");
    ana.join();
    Assert(ana.until([&] { return ana.inRoom(); }), "she can join like anyone");
}

Test(rejoin, off_by_default)
{
    Harness harness([](server::ServerConfig& c) { c.endpoint.timeout = 1.0; });
    TestClient ana("Ana"), ben("Ben");

    ana.connect(harness, 1.0);
    ben.connect(harness, 1.0);
    ana.join();
    ben.join();
    Assert(ana.until([&] { ben.poll(); return ana.inRoom() && ben.inRoom(); }), "both in");
    cutTheCable(harness, ana, ben, 0);
    Assert(waitUntil([] { return roomLog().leftCount() == 2; }, 10.0), "a lost connection is a player who left, as always");
    AssertEq(roomLog().awayCount(), 0u, "nobody was away");
    ana.connect(harness, 1.0);
    ana.matchmaking.rejoin();
    Assert(ana.until([&] { return !ana.failures.empty() && !ana.matchmaking.canRejoin(); }), "and coming back is refused");
}

Test(rejoin, a_key_is_needed)
{
    Harness harness([](server::ServerConfig& c) { c.endpoint.timeout = 1.0; }, server::RoomTypeConfig{.maxPlayers = 2, .idleTimeout = 30.0, .reconnectSeconds = 10.0});
    TestClient ana("Ana");
    net::Net net;
    net::Endpoint* lobby = &net.connect("lobby", harness.network);
    std::vector<net::JoinError> refused;
    std::vector<net::RoomAssigned> assigned;
    std::uint32_t playerId = 0;

    ana.connect(harness);
    ana.join();
    Assert(ana.until([&] { return ana.inRoom(); }), "Ana is in");
    lobby->on<net::JoinRefused>([&](net::ConnectionId, const net::JoinRefused& r) { refused.push_back(r.reason); });
    lobby->on<net::RoomAssigned>([&](net::ConnectionId, const net::RoomAssigned& a) { assigned.push_back(a); });
    Assert(waitUntil([&] { net.poll(); return lobby->connected(); }), "a stranger reaches the lobby");
    playerId = 1;   // (the first player of the server: someone could guess it)
    lobby->send(net::CLIENT_CONNECTION, net::RejoinRoom{ana.roomId, playerId, 12345});
    lobby->send(net::CLIENT_CONNECTION, net::RejoinRoom{ana.roomId, 999, 12345});
    Assert(waitUntil([&] { net.poll(); return refused.size() == 2; }), "a wrong key, and a player that does not exist, are both refused");
    Assert(refused[0] == net::JoinError::UnknownRoom && refused[1] == net::JoinError::UnknownRoom, "with the same answer");
    Assert(assigned.empty(), "and nothing was given");
    ana.poll();
    Assert(ana.inRoom(), "Ana did not notice");
    net::RejoinRoom wrongProtocol{ana.roomId, playerId, 1, 1};

    lobby->send(net::CLIENT_CONNECTION, wrongProtocol);
    Assert(waitUntil([&] { net.poll(); return refused.size() == 3; }), "another version of the protocol");
    Assert(refused[2] == net::JoinError::Protocol, "is said");
}

Test(teams, a_message_to_a_team)
{
    Harness harness(nullptr, server::RoomTypeConfig{.maxPlayers = 3, .idleTimeout = 30.0});
    std::vector<std::unique_ptr<TestClient>> clients;

    for (const char* name : {"Ana", "Ben", "Cleo"}) {
        clients.push_back(std::make_unique<TestClient>(name));
        clients.back()->connect(harness);
        clients.back()->join();
    }
    Assert(pumpAll(clients, [&] { return std::all_of(clients.begin(), clients.end(), [](auto& c) { return c->inRoom(); }); }), "three in");
    const std::uint32_t teams[3] = {1, 2, 1};

    for (std::size_t i = 0; i < 3; ++i) {
        clients[i]->matchmaking.room()->send(net::CLIENT_CONNECTION, SetTeam{teams[i]});
        clients[i]->matchmaking.room()->send(net::CLIENT_CONNECTION, Echo{"set"});   // answered after the team was set (same order)
    }
    Assert(pumpAll(clients, [&] { return std::all_of(clients.begin(), clients.end(), [](auto& c) { return c->echoes.size() == 1; }); }), "every team is set");
    clients[0]->matchmaking.room()->send(net::CLIENT_CONNECTION, TeamSay{"flank left"});
    Assert(pumpAll(clients, [&] { return clients[0]->echoes.size() == 2 && clients[2]->echoes.size() == 2; }), "Ana and Cleo, who are in team 1, hear it");
    for (int i = 0; i < 50; ++i) {
        pumpAll(clients, [] { return true; }, 0.01);
    }
    AssertEq(clients[1]->echoes.size(), 1u, "Ben, in the other team, does not");
    AssertStrEq(clients[2]->echoes.back().c_str(), "Ana (team): flank left", "it comes from Ana");
}
