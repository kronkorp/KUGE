# Step 9: Testing a networked game

Networked games are hard to test, so people often do not, and then they cannot change anything without fear.
This step shows how R-Type is tested at **five levels**, from the cheapest to the most real. File:
`tests/rtype/rtype_test.cpp`. The tests use kronklab: a test is `Test(suite, name) { ... }` with `Assert` and
`AssertEq`; names are limited to 31 characters.

## Level 1: the rules, alone (milliseconds)

No server, no client, no thread. A `World` with the arena and a `Match`:

```cpp
struct Arena
{
    kw::World world;

    Arena() { buildArena(world); world.addResource<Match>(); world.getResource<Match>().track = [](...) {}; }
    kw::Entity ship(kuge::Vec2 at, std::uint32_t player);
    kw::Entity enemy(kuge::Vec2 at);
};
```

Tests: *a bullet kills*, *an enemy hits a ship*, *a ship dies and the players lose*, *the wave and the end*, and
**`same_seed_same_game`**: play 600 ticks twice with a seeded generator and the same inputs, compare a trace of
every tick. It fails the day a rule starts to depend on the order of entities.

If the rules are wrong you find out here, in a test that takes a millisecond and says exactly which line lied.

## Level 2: a client with no screen

The client module has a **dummy backend**: it draws nothing and lets you press keys from code. A `Pilot` is a whole
client (engine, scene, prediction) in the test:

```cpp
struct Pilot
{
    kuge::DummyBackend            dummy;
    kuge::Engine                  engine;
    kuge::ClientModule*           client;
    std::shared_ptr<ClientReport> report = std::make_shared<ClientReport>();

    explicit Pilot(const ClientOptions& options)
        : dummy(kuge::makeDummyBackend({960.0f, 540.0f})),
          engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed, .tickRate = 60}),
          client(&engine.addModule<kuge::ClientModule>(std::move(dummy.backend)))
    {
        bindDefaults(client->input());
        engine.scenes().change<RTypeScene>(options, report);
    }

    void press(kuge::Key key, bool down) { client->input().handle(kuge::KeyEvent{key, down}); }
    bool frame() { return engine.step(1.0 / 60.0); }
};
```

It is the **real scene and the real systems**. Only the window and the sound device are missing. The test reads
the scene's `ClientReport` to know what the pilot saw: `ships`, `mostBullets`, `prediction.corrections`...

## Level 3: a server on a thread, clients in the test

```cpp
struct Server
{
    kuge::net::LoopbackNetwork network;
    std::unique_ptr<kuge::server::GameServer> server;
    std::thread thread;

    explicit Server(bool sockets, ...)
    {
        ...
        server = std::make_unique<kuge::server::GameServer>(config);
        server->addRoomType<RTypeRoom>("rtype", RoomTypeConfig{.maxPlayers = maxPlayers, .idleTimeout = 30.0});
        thread = std::thread([this] { server->run(); });
        while (!server->stats().lobbyOpen) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    }
    ~Server() { server->stop(); thread.join(); }
};
```

The test steps the pilots at 60 Hz in real time (`fly(...)`) because the server has a clock of its own, and
waits for conditions with a timeout instead of sleeping for a guess:

```cpp
Assert(waitFor({&ana, &ben}, [&] { return ana.report->ships == 2 && ben.report->ships == 2; }), "both joined");
ana.press(kuge::Key::Right, true);
ana.press(kuge::Key::Space, true);
...
Assert(waitFor({&ana, &ben}, [&] { return ana.report->mostBullets > 0 && ben.report->mostBullets > 0; }),
       "bullets fly, on both screens");
```

**Wait for a condition, do not sleep.** `waitFor` polls a condition and stops as soon as it is true, or fails after
a timeout. It makes tests fast when things work and clear when they do not, and it is not flaky on a slow machine.

The host mode *is* this test: a server thread and a client in the same process over the loopback. The tests
`two_pilots_one_process`, `a_game_ends`, `pilots_come_and_go` and `clients_before_the_server` cover a game, its
end and the next one, players leaving, and the server starting late.

## Level 4: several rooms, and real sockets

- **Several rooms at once** (`rtype_rooms`): five pilots with two places a room make three rooms; each pilot sees
  only its own room; a room whose pilots leave does not disturb the other; two rooms end at different times and
  every pilot asks for and gets the next game.
- **Real sockets** (`rtype_dedicated`): the same over TCP and UDP on localhost (with free ports found by trying),
  with four pilots in one room and six in two.
- **The server stopping with players in a game**, five times in a row: everything is left, threads join, clients
  see the loss.

The trick that keeps these tests honest: check for something that can only be true if the **whole** chain works. "A
pilot sees a bullet" needs the key, the input packet, the room, the rules, the snapshot and the prefab.

## Level 5: the real programs

`cmake/RunRType.sh` starts the actual binaries (see step 8). It catches what tests that link the code cannot:
a missing `main`, a wrong argument, a crash on startup, a picture with nothing in it.

## Habits that pay off

1. **Test a mistake by making it.** After writing a test, break the code on purpose and check the test fails.
   For example: let the lobby fill one room past its limit and watch the multi-room tests fail; make the socket
   transport read one datagram per poll and watch the burst test fail. A test that cannot fail proves nothing.
2. **Run under sanitizers.** `-DKUGE_SANITIZE=thread` and `address`. A networked, threaded game is exactly where
   races and use-after-free hide. (ASan found the pointer to a destroyed room endpoint; TSan proved the process-wide
   lock around kronknet was needed.) See [12](../../12-testing-and-debugging/README.md).
3. **Repeat.** Run the suite ten times in a row before believing it. A test that passes 9 times in 10 has a bug
   (in the test or the code).
4. **Make the network bad on purpose.** `LoopbackNetwork::Conditions` gives loss, duplicates, latency and jitter,
   reproducibly with a seed.
5. **Take a picture.** A scripted run that writes a PPM lets you *look* at the result, which finds things no
   assert thinks to ask: one glance at the first screenshot of R-Type showed the ship, the bullets, the enemies and
   the stars where they should be.

Next: [Step 10: Where to go next](../10-where-to-go-next/README.md).
