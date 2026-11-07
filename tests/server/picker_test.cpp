extern "C" {
    #include "kronklab/kronklab.h"
}
#include "server_fixture.hpp"
#include <atomic>

// NOTE: kronklab test names are limited to 31 characters.
// The game chooses who plays with whom (ServerConfig::roomPicker); the lobby only asks.

namespace
{
    using kuge::server::JoinRequest;
    using kuge::server::RoomChoice;

    // Players with the same options go together: the first makes the room, the others take the one with its options
    std::optional<RoomChoice> byOptions(const JoinRequest& request, const std::vector<kuge::net::RoomInfo>& open)
    {
        for (const auto& room : open) {
            if (room.options == request.options) {
                return RoomChoice::join(room.roomId);
            }
        }
        return RoomChoice::make(request.options);
    }
}

Test(picker, same_options_same_room)
{
    Harness harness([](kuge::server::ServerConfig& c) { c.roomPicker = byOptions; },
        kuge::server::RoomTypeConfig{.maxPlayers = 4, .idleTimeout = 30.0});
    TestClient a("A"), b("B"), c("C");

    for (TestClient* client : {&a, &b, &c}) {
        client->connect(harness);
    }
    a.matchmaking.join("duel", "A", "mode=1");
    Assert(a.until([&] { return a.inRoom(); }), "A makes a room");
    b.matchmaking.join("duel", "B", "mode=1");
    c.matchmaking.join("duel", "C", "mode=2");
    Assert(a.until([&] { b.poll(); c.poll(); return b.inRoom() && c.inRoom(); }), "B and C are in");
    AssertEq(a.roomId, b.roomId, "B asked for the same game as A");
    Assert(c.roomId != a.roomId, "C asked for another one, and got a room of its own");
    std::lock_guard lock(roomLog().mutex);
    Assert(roomLog().roomOptions.size() == 2 && roomLog().roomOptions[0] != roomLog().roomOptions[1], "and the rooms got the options that the picker made them with");
}

Test(picker, wait_then_go)
{
    std::atomic<bool> release{false};
    std::atomic<int> asked{0};
    std::atomic<double> longest{0.0};
    Harness harness([&](kuge::server::ServerConfig& c) {
        c.roomPicker = [&](const JoinRequest& request, const std::vector<kuge::net::RoomInfo>&) -> std::optional<RoomChoice> {
            ++asked;
            longest = std::max(longest.load(), request.waited);
            return release ? RoomChoice::make() : RoomChoice::wait();
        };
    });
    TestClient ana("Ana");

    ana.connect(harness);
    ana.join();
    Assert(!ana.until([&] { return ana.inRoom(); }, 0.5), "while the picker says wait, no room");
    Assert(ana.failures.empty() && asked > 5, "and no refusal: it was asked again and again (%d times)", asked.load());
    Assert(longest > 0.2, "with the time it waited: %f", longest.load());
    release = true;
    Assert(ana.until([&] { return ana.inRoom(); }), "then the room is made");
}

Test(picker, leaving_ends_the_wait)
{
    std::atomic<int> asked{0};
    Harness harness([&](kuge::server::ServerConfig& c) {
        c.roomPicker = [&](const JoinRequest&, const std::vector<kuge::net::RoomInfo>&) -> std::optional<RoomChoice> {
            ++asked;
            return RoomChoice::wait();
        };
    });
    TestClient ana("Ana");

    ana.connect(harness);
    ana.join();
    Assert(waitUntil([&] { ana.poll(); return asked > 3; }), "it waits");
    ana.matchmaking.leave();
    for (int i = 0; i < 50; ++i) {   // (the lobby gets the message)
        ana.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const int atLeave = asked;

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    AssertEq(asked.load(), atLeave, "after leave() nobody asks about it any more");
}

Test(picker, a_bad_answer_is_refused)
{
    std::atomic<int> mode{0};
    Harness harness([&](kuge::server::ServerConfig& c) {
        c.roomPicker = [&](const JoinRequest&, const std::vector<kuge::net::RoomInfo>&) -> std::optional<RoomChoice> {
            switch (mode) {
                case 0: return RoomChoice::join(9999);              // not a room
                case 1: return RoomChoice::make("bad\noptions");    // not options
                default: return std::nullopt;                       // the usual rule
            }
        };
    });
    TestClient ana("Ana");

    ana.connect(harness);
    ana.join();
    Assert(ana.until([&] { return ana.failures.size() == 1; }), "a room that does not exist");
    AssertStrEq(ana.failures.back().c_str(), "no such room", "");
    mode = 1;
    ana.join();
    Assert(ana.until([&] { return ana.failures.size() == 2; }), "options that are not options");
    AssertStrEq(ana.failures.back().c_str(), "these are not options for a room", "");
    mode = 2;
    ana.join();
    Assert(ana.until([&] { return ana.inRoom(); }), "nothing: the usual rule puts the player in a room");
}

Test(picker, bad_player_options)
{
    Harness harness([](kuge::server::ServerConfig& c) { c.roomPicker = byOptions; });
    TestClient ana("Ana");

    ana.connect(harness);
    ana.matchmaking.join("duel", "Ana", "not\noptions");
    Assert(ana.until([&] { return !ana.failures.empty(); }), "the options of a request are checked");
    AssertStrEq(ana.failures.back().c_str(), "these are not options for a room", "");
}

Test(picker, only_for_any_room)
{
    std::atomic<int> asked{0};
    Harness harness([&](kuge::server::ServerConfig& c) {
        c.roomPicker = [&](const JoinRequest&, const std::vector<kuge::net::RoomInfo>&) -> std::optional<RoomChoice> {
            ++asked;
            return RoomChoice::make();
        };
    });
    TestClient host("Host"), guest("Guest");

    host.connect(harness);
    guest.connect(harness);
    host.create("my room");
    Assert(host.until([&] { return host.inRoom(); }), "a named room is made");
    guest.joinNamed(host.roomId, "my room");
    Assert(guest.until([&] { return guest.inRoom(); }), "and taken by name");
    AssertEq(asked.load(), 0, "the picker was not asked: those are the client's choice");
}
