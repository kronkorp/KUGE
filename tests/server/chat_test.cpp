extern "C" {
    #include "kronklab/kronklab.h"
}
#include "ChatRelay.hpp"
#include "server_fixture.hpp"

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using kuge::server::ChatRelay;
}

Test(chat, a_burst_then_a_refill)
{
    ChatRelay relay({.maxBytes = 200, .burst = 4, .refillSeconds = 1.0});
    int passed = 0;

    for (int i = 0; i < 5; ++i) {
        passed += relay.accept(1, "hi", 10.0).has_value() ? 1 : 0;
    }
    AssertEq(passed, 4, "five lines at once with a burst of 4: four pass (%d)", passed);
    Assert(!relay.accept(1, "hi", 10.5), "half a second later: still none");
    Assert(relay.accept(1, "hi", 11.0), "one second after the start of the refill: one more");
    Assert(!relay.accept(1, "hi", 11.0), "and only one");
    Assert(relay.accept(2, "hi", 10.0), "another player has a bucket of its own");
    Assert(relay.accept(1, "hi", 100.0) && relay.accept(1, "hi", 100.0) && relay.accept(1, "hi", 100.0) && relay.accept(1, "hi", 100.0)
        && !relay.accept(1, "hi", 100.0), "a long silence refills the bucket, but not beyond the burst");
    relay.forget(1);
    Assert(relay.accept(1, "hi", 100.0), "a connection that goes is forgotten: a new one starts full");
}

Test(chat, bad_lines_cost_nothing)
{
    ChatRelay relay({.maxBytes = 10, .burst = 1, .refillSeconds = 1000.0});

    Assert(!relay.accept(1, "", 0.0) && !relay.accept(1, "   \t ", 0.0), "an empty line, or only spaces");
    Assert(!relay.accept(1, "12345678901", 0.0), "one byte over the limit");
    Assert(!relay.accept(1, "a\nb", 0.0) && !relay.accept(1, "a\tb", 0.0) && !relay.accept(1, "a\x1b[2J", 0.0) && !relay.accept(1, "a\x7f", 0.0), "control characters");
    Assert(!relay.accept(1, "a\xC2\x85" "b", 0.0), "even the ones of Latin-1 (U+0085)");
    Assert(!relay.accept(1, "a\xFF", 0.0) && !relay.accept(1, "\xE2\x82", 0.0) && !relay.accept(1, "\xC0\xAF", 0.0), "text that is not UTF-8");
    const auto good = relay.accept(1, "  \xC3\xA9t\xC3\xA9  ", 0.0);

    Assert(good && *good == "\xC3\xA9t\xC3\xA9", "none of that used the one line of the bucket; a line is cleaned of its spaces");
    Assert(!relay.accept(1, "again", 0.0), "and now it is used");
}

Test(chat, a_room_relays)
{
    Harness harness;
    TestClient ana("Ana"), ben("Ben");

    ana.connect(harness);
    ben.connect(harness);
    ana.join();
    ben.join();
    Assert(ana.until([&] { ben.poll(); return ana.inRoom() && ben.inRoom(); }), "in the room");
    auto say = [&](TestClient& who, const char* text) { who.matchmaking.room()->send(kuge::net::CLIENT_CONNECTION, kuge::net::ChatSend{text}); };

    say(ana, "hello");
    Assert(ana.until([&] { ben.poll(); return ana.chats.size() == 1 && ben.chats.size() == 1; }), "both hear it");
    Assert(ben.chats[0].from == ana.networkId && ben.chats[0].text == "hello", "from the network id of the speaker");
    say(ben, "line\nbreak");
    say(ben, "");
    say(ben, std::string(41, 'x').c_str());
    say(ben, "fine");
    Assert(ana.until([&] { ben.poll(); return ana.chats.size() == 2 && ben.chats.size() == 2; }), "only the good line gets through");
    AssertStrEq(ana.chats[1].text.c_str(), "fine", "it is the good one");
    Assert(ana.chats[1].from == ben.networkId, "from Ben");
    say(ana, "two");
    say(ana, "three");   // (the burst of the room is 2: "hello" and "two")
    Assert(ana.until([&] { ben.poll(); return ana.chats.size() == 3; }), "a second line passes");
    for (int i = 0; i < 50; ++i) {
        ana.poll();
        ben.poll();
        std::this_thread::yield();
    }
    AssertEq(ana.chats.size(), 3u, "the third is over the rate: nobody hears it");
}
