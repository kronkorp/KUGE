# Step 8: Three ways to run it

You have a room and a client. Now run the same code three ways.

## 1. A dedicated server, and clients

```sh
build/example/rtype/kuge_rtype_server 4242
build/example/rtype/kuge_rtype_client --port 4242 --name Ana
build/example/rtype/kuge_rtype_client --port 4242 --name Ben      # another terminal
```

The server process has **no window** and links no SDL. Clients reach its lobby over **TCP**
(kept open for the session: losing it is felt at once) and each room over **UDP** (fast, and the engine adds
reliable messages where needed).

The clients can start **before** the server: they retry every second until it is there.

## 2. A player who hosts

```sh
build/example/rtype/kuge_rtype_host
```

One process, one window, and **the same room** running on a thread of its own. `host/main.cpp`:

```cpp
// The server: an engine with no window, on its own thread, reached by name
kuge::net::LoopbackNetwork network;
kuge::server::ServerConfig config;

config.transport = kuge::server::Transport::Loopback;
config.loopback = &network;
kuge::server::GameServer server(config);

server.addRoomType<rtype::RTypeRoom>("rtype", {.maxPlayers = 4, .idleTimeout = 30.0});
std::thread serverThread([&server] { server.run(); });
while (!server.stats().lobbyOpen) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }

// The client: the same scene as rtype_client, told to use the in-memory network
kuge::Engine engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed, .tickRate = 60});
auto& client = engine.addModule<kuge::ClientModule>(kuge::makeSdlBackend(window), ...);
rtype::ClientOptions options;

options.sockets = false;
options.network = &network;
engine.scenes().change<rtype::RTypeScene>(options, report);
engine.run();

server.stop();
serverThread.join();
```

Look at what changed compared to the dedicated setup:

- The server uses `Transport::Loopback` instead of sockets. The room does not notice.
- The client uses `sockets = false`. The scene does not notice.
- Two engines run in one process: the server's (headless, on a thread) and the client's (windowed, on the main
  thread). They **share nothing** but the `LoopbackNetwork`, which is thread-safe.

That is a "listen server" in a page.

A `GameServer` serves **one** transport. The host above uses the loopback, so only its own window can play. If
the host should also accept friends over the network, start its `GameServer` with `Transport::Sockets` instead and
let the host's own client connect to `127.0.0.1` like any other client: the room and the scene are the same,
and only two lines of configuration change.

## 3. Solo

There is no R-Type solo (the rules are server-authoritative), but `example/platformer` is a solo game built from
the same engine: the core, the client and the physics, with no network at all. What R-Type shows is that **the
network is an addition, not a rewrite**: the rules, the arena and the movement were written before any network
existed (step 4), and a solo version would run them in the client's own scene.

## Scripted runs, for machines with no screen

Both windowed programs take `--frames N [--screenshot f.ppm]`:

```sh
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy \
    build/example/rtype/kuge_rtype_host --frames 600 --screenshot game.ppm
```

A scripted pilot flies for 600 frames as fast as it can (with the absolute schedule of step 7) and writes the
last frame. Any image viewer opens a PPM; it is the simplest image format there is, and `Script.hpp` writes it
in 10 lines.

## Verifying it all at once: `RunRType.sh`

`cmake/RunRType.sh` (run by `ctest` as `rtype_smoke`) does what you just did by hand:

1. starts `kuge_rtype_server` in the background;
2. runs `kuge_rtype_client` scripted, which must join a game and see it;
3. stops the server with Ctrl+C and checks that it left its room properly;
4. runs `kuge_rtype_host` scripted;
5. checks both pictures: the right size, and containing the colours of a ship (white), an enemy (red) and a bullet
   (yellow).

It is a smoke test: it does not prove every detail, but it proves that **the real programs start, talk to each
other and draw the game**, which no unit test can.

Next: [Step 9: Testing a networked game](../09-testing-a-networked-game/README.md).
