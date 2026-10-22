extern "C" {
    #include "kronklab/kronklab.h"
}
#include "server_fixture.hpp"
#include <map>

// NOTE: kronklab test names are limited to 31 characters.
// The game says who a player is (ServerConfig::authenticate); the lobby asks it before it puts anyone in a room.

namespace
{
    // The accounts of the tests: a token, and the id of the player it stands for
    void accounts(kuge::server::ServerConfig& config)
    {
        config.authenticate = [](const std::string& token, const std::string&) -> std::optional<std::uint32_t> {
            static const std::map<std::string, std::uint32_t> known{{"ana-secret", 42}, {"ben-secret", 43}, {"zero", 0}};
            const auto found = known.find(token);

            return found == known.end() ? std::nullopt : std::optional<std::uint32_t>(found->second);
        };
    }
}

Test(auth, a_known_token_gets_its_id)
{
    Harness harness(accounts);
    TestClient ana("Ana"), ben("Ben");

    ana.connect(harness);
    ben.connect(harness);
    ana.matchmaking.setToken("ana-secret");
    ben.matchmaking.setToken("ben-secret");
    ana.join();
    ben.join();
    Assert(ana.until([&] { ben.poll(); return ana.inRoom() && ben.inRoom(); }), "both are in");
    std::lock_guard lock(roomLog().mutex);
    std::vector<std::uint32_t> ids = roomLog().playerIds;

    std::sort(ids.begin(), ids.end());
    Assert(ids.size() == 2 && ids[0] == 42 && ids[1] == 43, "the room sees the ids that the game gave, not the lobby's");
}

Test(auth, others_are_refused)
{
    Harness harness(accounts);
    TestClient none("None"), wrong("Wrong"), huge("Huge"), zero("Zero");

    for (TestClient* client : {&none, &wrong, &huge, &zero}) {
        client->connect(harness);
    }
    wrong.matchmaking.setToken("ana-secre");
    huge.matchmaking.setToken(std::string(kuge::net::MAX_AUTH_TOKEN + 1, 'x'));
    zero.matchmaking.setToken("zero");
    for (TestClient* client : {&none, &wrong, &huge, &zero}) {
        client->join();
    }
    Assert(none.until([&] { wrong.poll(); huge.poll(); zero.poll(); return none.failures.size() + wrong.failures.size() + huge.failures.size() + zero.failures.size() == 4; }), "all four are told");
    for (TestClient* client : {&none, &wrong, &huge, &zero}) {
        AssertStrEq(client->failures.back().c_str(), "the server does not know who you are", "with the reason");
        Assert(!client->inRoom(), "and not in a room");
    }
    AssertEq(harness.server->stats().authRefused.load(), 4, "counted");
    AssertEq(harness.server->stats().refused.load(), 4, "also among the refusals");
    AssertEq(harness.server->stats().players.load(), 0, "nobody was let in");
}

Test(auth, every_way_in_is_checked)
{
    Harness harness(accounts);
    TestClient host("Host"), stranger("Stranger");

    host.connect(harness);
    stranger.connect(harness);
    host.matchmaking.setToken("ana-secret");
    host.create("my room");
    Assert(host.until([&] { return host.inRoom(); }), "a known token can make a room");
    stranger.create("other room");
    Assert(stranger.until([&] { return stranger.failures.size() == 1; }), "an unknown one cannot");
    stranger.joinNamed(host.roomId, "my room");
    Assert(stranger.until([&] { return stranger.failures.size() == 2; }), "nor take a place in a room by its name");
    AssertStrEq(stranger.failures.back().c_str(), "the server does not know who you are", "");
}

Test(auth, one_account_one_room)
{
    Harness harness(accounts);
    TestClient ana("Ana"), copy("Ana again");

    ana.connect(harness);
    copy.connect(harness);
    ana.matchmaking.setToken("ana-secret");
    copy.matchmaking.setToken("ana-secret");
    ana.join();
    Assert(ana.until([&] { return ana.inRoom(); }), "the account is in");
    copy.join();
    Assert(copy.until([&] { return !copy.failures.empty(); }), "the same account from another connection is refused");
    AssertStrEq(copy.failures.back().c_str(), "already in a room", "");
    AssertEq(harness.server->stats().authRefused.load(), 0, "(it was known: that is not a failed login)");
}

Test(auth, no_check_by_default)
{
    Harness harness;
    TestClient ana("Ana");

    ana.connect(harness);
    ana.matchmaking.setToken("whatever");
    ana.join();
    Assert(ana.until([&] { return ana.inRoom(); }), "a server that does not check ignores the token");
}
