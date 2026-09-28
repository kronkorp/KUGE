# 02 How the engine works

This page is the big picture. The next ones zoom in.

## One idea: a fixed tick

A game has two clocks. **The screen** refreshes at whatever rate the machine manages: 60, 144, or
30 when it struggles. **The simulation** must not care. If a ship moves 2 pixels "per frame", it
moves faster on a fast screen, and a server with no screen has no frames at all.

So KUGE runs the simulation at a **fixed rate** (60 ticks per second by default), whatever the
speed of the machine. One tick always represents the same amount of game time (`dt`, 1/60 s).
Everything that must give the same result on every machine (movement, rules, physics, AI) runs in
ticks. Only drawing runs "once per loop".

This one decision is the base of everything that comes later: a server can run the same
simulation as a client, a client can predict what the server will do, and a test can play ten
seconds of game in a millisecond.

## The loop

Each turn of the loop (`Engine::step`), the engine does:

```
1. the scene changes that were asked are done          (a scene is never destroyed while it runs)
2. every module's beginFrame()                         (the client reads the window and the keys)
3. as many fixed ticks as the elapsed time asks for    (at most maxCatchUp)
4. one frame
5. every module's endFrame()                           (the client shows what was drawn)
6. the scene changes that were asked during the loop
```

`Engine::run()` calls `step()` with the real clock and sleeps until the next tick is due, so it
does not burn a core. Tests call `step(seconds)` themselves with a clock they made up: that is how
ten seconds are played instantly.

How many ticks per loop? The engine keeps an **accumulator**: each loop adds the real time that
passed, and every `1/tickRate` seconds in it becomes a tick. A slow machine that lost 100 ms owes
6 ticks; it runs at most `maxCatchUp` (5 by default) and forgets the rest, otherwise it would
spend so long catching up that it falls further behind ("the spiral of death").

```cpp
kuge::Engine engine({
    .mode       = kuge::Engine::Mode::Headless,   // or Windowed (needs the client module)
    .tickRate   = 60,                              // ticks per second
    .maxCatchUp = 5,                               // ticks run at most in one loop
    .maxFps     = 0,                               // loops per second; 0: one per tick
    .workers    = 0,                               // worker threads; 0: one per CPU
});
```

## Two schedules, and stages inside them

A **system** is a piece of code that the engine calls. It lives in one of two **schedules**:

- **Fixed**: called at each tick. Gameplay, physics, animations, AI: what must be reproducible.
- **Frame**: called once per loop. Drawing, and things that only matter to the eyes.

Inside a schedule, systems run in **stages**, in this order (`kuge::stage`):

| Stage | For |
|---|---|
| `Network` | receive from the network |
| `Input` | read inputs, apply received actions |
| `Simulation` | movement wishes, AI, game rules |
| `Physics` | things move and collide |
| `Late` | react to what the physics found: damage, animations |
| `Replication` | send the state to the clients |
| `Render` | draw (Frame schedule) |

Systems of the same stage run in the order they were added. The stages are a convention that gives
every game the same skeleton: the network delivers, inputs are read, the game decides, physics
moves things, the game reacts, the server tells the clients. When you wonder where some code goes,
this table answers.

## Time

Every scene's World has a `Time` resource:

| Field | Meaning |
|---|---|
| `dt` | Seconds per fixed tick (always the same) |
| `tickRate` | Ticks per second |
| `tick` | Number of the fixed tick that runs (counts from 0 in each scene) |
| `alpha` | In a Frame system: how far the frame is between two ticks, from 0 to 1 |
| `frameDt` | In a Frame system: real seconds since the last frame |

The screen shows frames, the simulation makes ticks. If an entity has a `PreviousTransform2D`, the
client draws it **between** its last two positions using `alpha`, so movement stays smooth on a
144 Hz screen although the simulation moves 60 times a second.

## Modules

The engine core knows nothing about windows, sockets or sound. A **module** adds a part of the
engine without the core knowing it:

```cpp
kuge::Engine engine({.mode = kuge::Engine::Mode::Windowed});
auto& client = engine.addModule<kuge::ClientModule>(kuge::makeSdlBackend({}));
```

A module gets hooks around each loop (`beginFrame`, `endFrame`) and puts **resources** in the World
of every scene that is entered (`inject`). The client module reads the window in `beginFrame`,
shows the picture in `endFrame`, and gives each scene a renderer, the input map, a camera, and more.
The engine owns its modules and destroys them after the scenes.

The engine, then, is: a loop, a stack of scenes, and a list of modules.

## Stopping

- `engine.stop()` can be called from any thread. Every spawned scene is left properly.
- Ctrl+C and SIGTERM are caught by `run()`: the scenes get their `onExit()` instead of the process
  being killed.
- `run()` also ends when no scene is left, or when a module asks (closing the window stops the
  engine).

## The rules the engine keeps

These are what the rest of the documentation relies on:

1. **The simulation is deterministic.** The same inputs give the same world, bit for bit, on every
   machine. (Entities are always handled in increasing order; nothing depends on how the ECS
   stores them; no wall clock is read in a tick.)
2. **Nothing is shared between scenes**, whatever thread they run on. Scenes talk through messages.
3. **Serialization is one thing.** The same little-endian `ByteWriter`/`ByteReader` serves saves,
   config files and network messages.
4. **A module only sees what it links.** The dependencies are one-way, and tests enforce it.

Next: [03 Scenes, systems and the ECS](../03-scenes-systems-and-the-ecs/README.md).
