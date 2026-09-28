# 04 Threads and messages

A server hosts many games at once. A game that hosts its own server runs a window and a room together.
Both need **several scenes running at the same time**, on different threads, without the bugs that
shared memory brings. KUGE's answer is simple: **scenes share nothing**, and talk by sending messages.

## Spawning scenes

The scenes of the main stack (`engine.scenes()`) run on the thread that calls `run()`. Other scenes are
**spawned**, with a policy that says where they run:

| `RunPolicy` | Runs |
|---|---|
| `Main` | On the main thread, in the loop of the engine, next to the main scenes (a HUD, the client of a host) |
| `Dedicated` | On a thread of its own, with its own loop at the tick rate (a server room, a lobby) |
| `Pooled` | On the worker threads: a driver works out when each scene is due and hands its tick to a worker. Many scenes on few threads. |

```cpp
// From a scene:
kuge::SceneHandle room = ctx().spawn<RoomScene>(kuge::RunPolicy::Dedicated, settings);

// From outside any scene (main(), a test):
kuge::SceneHandle room = engine.spawn<RoomScene>(kuge::RunPolicy::Pooled, settings);
```

The scene is **built** by the caller with the arguments, and **entered, ticked and left** by the thread that
runs it (so `onEnter` and `onExit` are on the same thread). `ctx().spawn` makes the caller the **parent**:
in the spawned scene, `ctx().parent()` is a handle to send answers to. `engine.spawn` gives no parent.

When to use which:

- **`Dedicated`**: a handful of long-lived scenes that each need their own steady clock. A room of a game
  server. One OS thread each, so a dozen is fine and thousands are not.
- **`Pooled`**: many scenes that do little work each. They share the engine's worker threads
  (`Engine::Config::workers`). A scene never runs two ticks at once, and a scene that is late does not pile
  up work: it catches up at most `maxCatchUp` ticks.
- **`Main`**: a scene that must touch the window (a client next to a hosted server).

## Talking: handles and messages

You never get a pointer to another scene. You get a `SceneHandle`, which can only send messages:

```cpp
struct StartMatch { int map; };

room.send(StartMatch{3});         // any thread; returns false if the scene is gone or its mailbox is full
room.stop();                      // asks the scene to end: it is popped at the start of its next loop
room.alive();

// In the room:
void onMessage(const kuge::Message& message) override
{
    if (const auto* start = message.as<StartMatch>()) {
        begin(start->map);
    }
}
```

- A handle can be copied and given to anyone, and it **outlives** the scene it points to (then `send`
  returns `false`). `ctx().self()` is a scene's own handle, to give to others.
- A message is **any value**, moved in, so it may be a type that cannot be copied. The receiver reads it
  back by type with `as<T>()`, which returns a pointer, or `nullptr` for another type.
- Messages arrive in **`onMessage`, at the start of each loop of the receiver, before its ticks**, in the
  order they were sent. A message sent before another arrives before it.
- The mailbox is **bounded** (4096 messages). When it is full, or the scene is gone, `send` returns `false`
  instead of growing without end.
- A paused scene (another one is over it on its stack) hears its messages when it runs again.

(These are *scene* messages, inside a process. Messages that go over a network are a different thing:
typed structs with `KUGE_MESSAGE`, see [08 The network](../08-the-network/README.md).)

## Modules and threads

A module's `inject()` gives resources to the scenes it is entered in. The client's window, renderer and
sound device belong to the **main thread**, so a module only injects into scenes of other threads if it says
`sharedAcrossThreads() == true` (the default is no). A server room does not get the client's renderer, and
cannot touch it by mistake.

## Ending

When `run()` ends, or the engine is destroyed:

1. every spawned scene is left (`onExit` included) **on the thread that ran it**;
2. those threads are joined (waiting threads are woken at once, they do not sleep out their tick);
3. then the main scenes are left.

A spawned scene that **throws** is logged and stopped; the others go on.

`engine.spawned()` counts the spawned scenes that still run: useful in tests.

## The worker pool

`engine.pool()` is a pool of worker threads (kronkpool), made on first use.

```cpp
engine.pool().post([] { /* runs on a worker; must not throw */ });
engine.pool().waitIdle();
```

## Loading in the background

Reading and decoding a picture takes time, and stalls a frame if it is done in the middle of the game.
`AssetManager<T>::loadAsync(path)` gives a **ticket** at once and reads the file on a worker; the asset
itself is made by the thread that owns it, when it calls `pump()` (the client does, at the start of each loop).

```cpp
auto ticket = client.textures().loadAsync("boss.png");
...
if (auto texture = ticket->asset()) { sprite.texture = texture; }        // ready
else if (ticket->state() == AssetManager<Texture>::Ticket::State::Failed) { log(ticket->error()); }
```

Why two halves? A texture belongs to the renderer's thread, but its picture can be decoded anywhere. The generic
form is `enableAsync(pool, prepare, finish)`: `prepare` (file to data) runs on a worker, `finish` (data to
asset) on the thread that calls `pump()`.

## The logger

`Logger::logger().info("...{}", x)` can be used from any thread: it is made once whatever the number of
threads, and a line is never cut by another one.

## Rules for writing threaded code with KUGE

1. **Put the state in the scene.** Not in a global, not in a static: a scene is the unit that owns its data.
2. **Talk with messages**, and keep them small and copyable (or movable).
3. **Anything a scene shares must be read-only** (a configuration built before the server starts) or atomic.
   The game server's `ServerStats` is an example of atomics that any thread may read.
4. **An endpoint or a client object belongs to one thread**, the one that polls it (the scene's).
5. Test with **ThreadSanitizer**. See [12 Testing](../12-testing-and-debugging/README.md).

Next: [05 The client](../05-the-client/README.md).
