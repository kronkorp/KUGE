extern "C" {
    #include "kronklab/kronklab.h"
}
#include "server_fixture.hpp"

// NOTE: kronklab test names are limited to 31 characters.
// The server decides what is played: a player whose mods are not the server's is refused with a reason.

Test(mods_net, a_client_with_other_mods)
{
    Harness harness([](kuge::server::ServerConfig& c) { c.mods = 0xABCDEF; });
    TestClient same("Same"), other("Other"), none("None");

    same.connect(harness);
    other.connect(harness);
    none.connect(harness);
    same.matchmaking.setMods(0xABCDEF);
    other.matchmaking.setMods(0x123456);
    same.join();
    other.join();
    none.join();
    Assert(same.until([&] { other.poll(); none.poll(); return same.inRoom() && !other.failures.empty() && !none.failures.empty(); }), "the one with the server's mods is in, the others are refused");
    AssertStrEq(other.failures.back().c_str(), "your mods are not the server's", "with the reason");
    AssertStrEq(none.failures.back().c_str(), "your mods are not the server's", "also a player with no mods at all");
    Assert(!other.inRoom() && !none.inRoom(), "and not in a room");
    other.matchmaking.setMods(0xABCDEF);
    other.join();
    Assert(other.until([&] { return other.inRoom(); }), "with the right mods, it can join");
    TestClient create("Creator");

    create.connect(harness);
    create.create("my room");
    Assert(create.until([&] { return !create.failures.empty(); }), "creating a room needs them too");
    create.matchmaking.setMods(0xABCDEF);
    create.create("my room");
    Assert(create.until([&] { return create.inRoom(); }), "and works with them");
}

Test(mods_net, no_mods_is_the_default)
{
    Harness harness;
    TestClient ana("Ana");

    ana.connect(harness);
    ana.join();
    Assert(ana.until([&] { return ana.inRoom(); }), "a server and a client with no mods agree");
}
