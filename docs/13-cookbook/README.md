# 13 Cookbook

Short recipes. Each one names the page that explains why.

## Engine

**Run some code every tick / every frame.** A system in the Fixed / Frame schedule ([03](../03-scenes-systems-and-the-ecs/README.md)):

```cpp
addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<MySystem>());
```

**Run some code every second.** A system with `interval` (in ticks): `addSystem(schedule, stage, sys, /*delay*/ 1, /*interval*/ 60)`,
or count `Time::tick` yourself.

**Do something once, later.** A system that returns `false` when it is done:

```cpp
bool handle(kw::World& world) override
{
    if (++m_ticks < 120) { return true; }
    doIt();
    return false;                       // not called again
}
```

**Change scene from a system.** `ctx().scenes().change<Next>()` (deferred: safe at any time).

**A pause menu.** `push<PauseScene>()` over the game and `pop()` to come back; the scene below gets `onPause` and `onResume`
and does not run meanwhile.

**Share something between scenes.** Pass a `std::shared_ptr<Shared>` to their constructors. If they are on different
threads, share only what is read-only or atomic, otherwise use messages ([04](../04-threads-and-messages/README.md)).

**Stop the program from a scene.** `ctx().engine().stop()`.

## Client

**Draw a coloured rectangle.** A `Sprite` with no texture and a `tint` ([05](../05-the-client/README.md)).

**Draw a picture.** `sprite.texture = client.textures().load("file.png")`.

**Play a sound.** `client.audio().play(*client.sounds().load("boom.wav"))`. With no audio device the game runs silently.

**Let the player rebind keys.** `input.loadBindings("keybinds.cfg")` at start; `startCapture()` then `takeCaptured()` for
"press the key you want"; `saveBindings(...)`.

**Follow the player with the camera.** A Frame system: `camera.position = playerPosition;`.

**Draw the screen at a fixed logical size.** `camera.zoom = min(screen.x / W, screen.y / H)` and centre the camera (R-Type).

**Take a screenshot.** `client.requestScreenshot()` before a step, `client.takeScreenshot()` after.

**Test a scene with no window.** `makeDummyBackend()`, press keys with `client.input().handle(KeyEvent{...})`,
step the engine ([05](../05-the-client/README.md)).

## Physics

**Stop something at a wall.** A `Collider` with no `Body` is a wall; a `Body` + `Collider` moves and stops.

**Something that reacts when touched but does not block.** A collider with `.asTrigger()`, and read
`physics.events()` in a `Late` system. Check the layers and masks are compatible both ways ([06](../06-physics/README.md)).

**Find what a ray hits.** `physics.raycast(origin, direction, maxDistance)`.

## Data

**Save and load a game.** `SnapshotRegistry` for what, `SaveSlots` for where ([07](../07-data-saves-and-config/README.md)).

**Read a setting.** `ConfigFile::load` then `getBool/getInt/getDouble/getString(key, fallback)`.

## Network

**Add a message.**

```cpp
struct Emote { KUGE_MESSAGE(Emote, kind) std::uint8_t kind = 0; };     // in a namespace, in a shared header

endpoint.on<Emote>([](kuge::net::ConnectionId from, const Emote& emote) { ... });   // the receiver
endpoint.send(peer, Emote{2}, kuge::net::Channel::Reliable);                        // the sender
```

**Broadcast to everyone but the sender.** `endpoint.broadcast(msg, Channel::Reliable, /*except*/ from)`.

**Simulate a bad network in a test.** `LoopbackNetwork::Conditions{.loss = 0.2, .latency = 0.05, ...}` ([08](../08-the-network/README.md)).

**Know how long a round trip is.** `endpoint.rtt(connection)` (nothing before the first ack).

## Server

**Add a kind of room.** `server.addRoomType<MyRoom>("name", {.maxPlayers = 8})`; the client asks `join("name", playerName)` ([09](../09-the-server/README.md)).

**Make a room on the worker threads instead of its own thread.** `RoomTypeConfig{.policy = kuge::RunPolicy::Pooled}`.

**Kick a player.** `kick(networkId)` in a room; the player is told `RoomClosed{Kicked}`.

**End a game.** `finish(kuge::net::RoomEnd::GameOver)`.

**Read server statistics from a status thread.** `server.stats().rooms.load()` (atomics).

**Run a server for a test.** `Transport::Loopback` and `std::thread([&]{ server.run(); })`, then `server.stop()` ([09](../09-the-server/README.md)).

## Replication and prediction

**Replicate a new component.** Register it in the shared `makeRegistry()` with a mode and two lambdas; the entity must be
`track`ed ([10](../10-replication-and-prediction/README.md)).

**Give the client something the server does not send** (a sprite, a sound). In the `onSpawn` prefab.

**Send different things to different players.** `replication.setFilter(...)`.

**Predict something other than a ship.** Register its components with `predicted = true`, give `predictType`, build a
private world that contains it and the level, and share the input function between room and client.

**A bot.** A room system that writes the same input struct a client would send, and calls the same input function.

## Threads

**Run a scene on its own thread.** `ctx().spawn<MyScene>(kuge::RunPolicy::Dedicated, args...)` ([04](../04-threads-and-messages/README.md)).

**Send a message to another scene.** Keep its `SceneHandle`; `handle.send(MyMessage{...})`.

**Do slow work off the main thread.** `engine.pool().post([]{ ... })`, and hand the result back with a message or an atomic.

**Load a picture without a hiccup.** `client.textures().loadAsync(path)`.
