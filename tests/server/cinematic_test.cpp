extern "C" {
    #include "kronklab/kronklab.h"
}
#include "server_fixture.hpp"

// NOTE: kronklab test names are limited to 31 characters.
// A cinematic that stops the game: the room starts it, every player plays it, the room goes on when they have all finished.

namespace
{
    using namespace kuge;

    // Two players in a room
    struct Pair
    {
        Harness    harness;
        TestClient ana{"Ana"}, ben{"Ben"};

        explicit Pair(server::RoomTypeConfig type = {.maxPlayers = 2, .idleTimeout = 30.0}) : harness({}, type)
        {
            ana.connect(harness);
            ben.connect(harness);
            ana.join();
            ben.join();
            waitUntil([&] { ana.poll(); ben.poll(); return ana.inRoom() && ben.inRoom(); });
        }

        bool both(const std::function<bool()>& done) { return waitUntil([&] { ana.poll(); ben.poll(); return done(); }); }

        // What a client sent before is done by the room when this comes back (one reliable channel, in order)
        void settle(TestClient& who)
        {
            const std::size_t before = who.echoes.size();

            who.matchmaking.room()->send(net::CLIENT_CONNECTION, Echo{"sync"});
            both([&] { return who.echoes.size() > before; });
        }
    };
}

Test(cinematic, all_players_finish)
{
    Pair pair;

    pair.ana.matchmaking.room()->send(net::CLIENT_CONNECTION, StartCine{"intro", 30.0});
    Assert(pair.both([&] { return pair.ana.cinematicsStarted.size() == 1 && pair.ben.cinematicsStarted.size() == 1; }), "both are told to play it");
    AssertStrEq(pair.ben.cinematicsStarted[0].c_str(), "intro", "which one");
    pair.ana.matchmaking.room()->send(net::CLIENT_CONNECTION, net::CinematicDone{"intro"});
    pair.settle(pair.ana);
    AssertEq(roomLog().cinematicCount(), 0u, "one is not enough: the room waits for the other");
    pair.ben.matchmaking.room()->send(net::CLIENT_CONNECTION, net::CinematicDone{"another"});
    pair.settle(pair.ben);
    AssertEq(roomLog().cinematicCount(), 0u, "a Done for another cinematic does not count");
    pair.ben.matchmaking.room()->send(net::CLIENT_CONNECTION, net::CinematicDone{"intro"});
    Assert(pair.both([&] { return roomLog().cinematicCount() == 1; }), "when both have finished, the room is told");
    std::lock_guard lock(roomLog().mutex);

    Assert(roomLog().cinematics[0].first == "intro" && roomLog().cinematics[0].second, "completed");
    Assert(pair.ana.cinematicsStopped.empty() && pair.ben.cinematicsStopped.empty(), "and nobody was cut short");
}

Test(cinematic, the_time_limit_cuts)
{
    Pair pair;

    pair.ana.matchmaking.room()->send(net::CLIENT_CONNECTION, StartCine{"boss", 0.5});
    Assert(pair.both([&] { return pair.ben.cinematicsStarted.size() == 1; }), "started");
    pair.ana.matchmaking.room()->send(net::CLIENT_CONNECTION, net::CinematicDone{"boss"});
    Assert(pair.both([&] { return roomLog().cinematicCount() == 1; }), "the time limit ends it though Ben never answered");
    Assert(pair.both([&] { return pair.ben.cinematicsStopped.size() == 1; }), "Ben, who still plays, is told to stop");
    Assert(pair.ana.cinematicsStopped.empty(), "Ana, who had finished, is not");
    std::lock_guard lock(roomLog().mutex);

    Assert(!roomLog().cinematics[0].second, "it was cut: not completed");
}

Test(cinematic, one_at_a_time_and_leavers)
{
    Pair pair;

    pair.ana.matchmaking.room()->send(net::CLIENT_CONNECTION, StartCine{"one", 30.0});
    pair.ana.matchmaking.room()->send(net::CLIENT_CONNECTION, StartCine{"two", 30.0});
    Assert(pair.both([&] { return pair.ben.cinematicsStarted.size() >= 1; }), "started");
    pair.settle(pair.ana);
    pair.settle(pair.ben);
    AssertEq(pair.ben.cinematicsStarted.size(), 1u, "a second one is refused while the first runs");
    pair.ana.matchmaking.room()->send(net::CLIENT_CONNECTION, net::CinematicDone{"one"});
    pair.ben.matchmaking.leave();
    Assert(pair.both([&] { return roomLog().cinematicCount() == 1; }), "a player who leaves is no longer waited for");
    std::lock_guard lock(roomLog().mutex);

    Assert(roomLog().cinematics[0].second, "and the others finished it");
}
