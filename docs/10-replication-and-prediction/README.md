# 10 Replication and prediction

In a multiplayer game the **truth lives on the server**. Each client only has a copy, and a copy takes time to
arrive. `kuge-replication` is how a room shows its world to the clients, and how a client makes its own player
feel instant anyway.

It needs the network and the physics, and knows neither the client nor the server: a room and a client both use it.

## The picture

```
   room (server)                                                     client
   World ── ReplicationServer ── snapshots (unreliable) ──▶ ReplicationClient ── World (a copy)
             ▲   what changed since the last one that                │ drawn a little in the past (interpolation)
             │   the client acknowledged                             ▼
   InputServer ◀── inputs (numbered, redundant) ── Prediction ── your own player's entity, at once
                      snapshot + "last input applied" ──▶ corrects it, replays the inputs the server has not seen
```

Three separate problems, three separate tools:

1. **Showing the world**: snapshots and the client's copy.
2. **Smoothness**: interpolation, because snapshots come 30 times a second, not 144.
3. **Feel**: prediction, because a round trip of 100 ms would make your own ship feel like driving through mud.

## 1. What is replicated

A `ReplicationRegistry` lists the components that travel, and how. **Build it with one function that the room
and the clients both call**:

```cpp
kuge::replication::ReplicationRegistry makeRegistry()
{
    using namespace kuge::replication;
    ReplicationRegistry registry;

    registerTransform2D(registry, Replicate::Interpolated, /*predicted*/ true);
    registerBody(registry, Replicate::OnChange, /*predicted*/ true);
    registry.component<Health>("Health", Replicate::OnChange,
        [](kuge::ByteWriter& out, const Health& h) { out.write<std::int32_t>(h.points); },
        [](kuge::ByteReader& in) { return Health{in.read<std::int32_t>()}; });
    registry.component<Slot>("Slot", Replicate::OnSpawn, ...);
    return registry;
}
```

The three modes:

| Mode | Behaviour | For |
|---|---|---|
| `Interpolated` | Changes all the time. The client draws it a moment in the past, between the last two values. Needs a `lerp`. | positions, angles |
| `OnChange` | Sent when it changes, applied at once | health, score |
| `OnSpawn` | Sent once, when the client learns of the entity | team, colour, kind |

The order of registrations is the number of a component on the wire (64 at most). A hash of the list (names,
modes, order) travels with each snapshot, and **a client ignores snapshots from a server whose list differs**: the
mismatch shows up at once instead of as corrupt data.

## 2. The room's side: `ReplicationServer`

```cpp
kuge::replication::ReplicationServer replication(world(), registry, endpoint());

// when something appears:
const NetworkId id = replication.track(entity, ShipType, /*owner*/ player.networkId);

// when a player joins / leaves:
replication.addClient(player.connection);
replication.removeClient(player.connection);

// each tick, in the Replication stage:
replication.update(tick);
```

- `track(entity, type, owner)` makes an entity replicated and gives it a **`NetworkId`**. `type` (an `EntityType`)
  says what it is, so the client can build the right thing. `owner` is the network id of the player it belongs
  to (0: nobody).
- To make an entity go away from the clients, just **remove it from the World** (or `untrack` it).
- `setFilter(...)` says what each client may see (interest management: only what is near, only your team).
- `sendInterval` (default 2 ticks: 30 snapshots per second at 60 Hz).

How snapshots work, because it explains the behaviour you will see:

- Snapshots go on the **unreliable** channel. The client **acknowledges** each one it applied.
- The next snapshot is computed **against the last one the client acknowledged**, not against the last one sent. So
  a lost snapshot costs nothing more than a bigger next one, and a client that has nothing (new, or too far
  behind) is simply sent everything.
- A snapshot holds only what changed: an entity that appeared (all its components), a component that changed, an
  entity that went. The same world always gives the same bytes. A big snapshot comes in several packets.
- The client builds each snapshot **from the one it acknowledged** (it keeps the last 32 states), so an entity that
  appeared and went between two snapshots, or a value that changed and came back, can never be left wrong.

## 3. The client's side: `ReplicationClient`

```cpp
kuge::replication::ReplicationClient replication(world, registry);

replication.onSpawn(ShipType, [](kw::World& world, kw::Entity e, const kuge::replication::SpawnInfo& info) {
    world.add<kuge::Sprite>(e, ...);           // what the server does not send: how it looks
});
replication.onDestroy(ShipType, [](kw::World& world, kw::Entity e) { ... });   // before it is removed
replication.setLocalPlayer(welcome.networkId);
replication.predictType(ShipType);             // see below
replication.attach(room);                      // the Endpoint from MatchmakingClient::onJoined
...
replication.update(frameSeconds);              // each frame: places the interpolated components
```

Entities appear in the World with a `Replicated` component (`id`, `type`, `owner`) and their replicated
components. The **prefab** (`onSpawn`) adds what the server never sends: the sprite, the sound, the particle.
That split is the point: **the server knows what things are, the client decides how they look**. `entity(networkId)`
finds a client entity from its network id.

**Interpolation.** Interpolated components are placed `interpolationDelay` (0.1 s by default) behind the newest
snapshot, between the last two values received: smooth at any frame rate, though snapshots are sparse and some
are lost. Nothing is invented: past its last value, an entity waits (no extrapolation). The price is that
*other* players are drawn about 100 ms in the past. That is the standard trade-off, and it is what the next
section fixes for your own player.

## 4. Prediction: your own player answers at once

Waiting for the server to tell you where your own ship is would give a 100 ms lag between key and movement.
Instead the client **simulates its own player itself**, right away, and treats the server's word as a correction.

### The server side: `InputServer`

```cpp
kuge::replication::InputServer<Steer> inputs(endpoint(), {.jitter = 2});

inputs.addClient(player.connection);

// each tick, before the simulation:
for (const auto& applied : inputs.collect()) {
    steer(world, shipOf(applied.connection), applied.input);        // the SAME function the client predicts with
    replication.setInputAck(applied.connection, applied.sequence);  // goes back in the snapshots
}
```

Inputs are numbered by the client and put in order here. `collect()` gives **one input per player per tick**:

- Each packet carries the last four inputs, so a lost one is usually in the next.
- If the next input is not there (lost, or late), **the last one is used again** for that tick (`repeated`), and
  an input that arrives after its turn is dropped: what is simulated is what the players did, not when it arrived.
- `jitter` holds a few inputs back to absorb timing jitter (two is a good default).

### The client side: `Prediction`

```cpp
kuge::replication::PredictionConfig<Steer> config;

config.build = [](kw::World& w) { buildArena(w); return buildShip(w); };   // a private world: the level and the player
config.apply = &steerShip;                                                   // input -> entity
config.step  = &stepArena;                                                   // one tick of the simulation

kuge::replication::Prediction<Steer> prediction(config, world, registry, replication, room);

// each fixed tick, with what the player is doing:
prediction.tick(steer);
```

**Each tick**, the input gets a number, is simulated on a **private world** that holds only the level and the
player, and the result is copied to the entity the game draws. The player answers at once, **walls included**. The
last few inputs go to the server.

**Each snapshot** carries the state of your entity **and the number of the last input that state includes**.
The client:

1. puts the private entity in that state;
2. simulates again the inputs after it (the ones the server has not seen yet);
3. that is the new prediction.

If the server simulated what the client simulated (same code, same inputs, deterministic physics), **nothing
changes**: with 100 ms of round trip and 10 % packet loss, R-Type's prediction is corrected about 3 times in 450
snapshots.

When the server *does* contradict the client (something the client cannot know: another player, a rule, a wall
only the server has), the ship is **not teleported**. It keeps being drawn where it was and **slides** to the
right place over `smoothing` seconds (0.1). Only a difference of more than `snapDistance` (a respawn, a
teleporter) is shown at once.

### What you must arrange

For this to work, three things must hold:

1. **The same code on both sides.** The room and the client's `apply`/`step` must do the same thing to the same
   state. Put that code in the shared folder and call it from both. In R-Type: `steerShip` and `stepArena`.
2. **Determinism.** The same inputs on the same state give the same result. The physics is deterministic; do not
   use random numbers, wall clock time or unordered iteration in what is predicted.
3. **Register the predicted components** (`predicted = true` in the registry) and tell the replication client
   which **type** is the predicted one: `predictType(ShipType)`.

### Predicted, or just owned?

A player owns its ship **and** its bullets (`owner` is the same network id). Only the ship is predicted: bullets
are ordinary entities, moved by the server and interpolated. That is why the client says which *type* it
predicts (`predictType`), not just "what I own". (This was a real bug found while building R-Type.)

### Shooting, and other things you do not predict

R-Type predicts **movement only**. A shot is sent as part of the input (`fire`), the server creates the bullet, and
it appears on your screen one round trip later. That is fine for a game where the bullet's start is not critical;
games that need instant muzzle flashes usually predict a cosmetic effect and let the server own the real bullet.

## How to know it works

The tests measure it. Look at `tests/replication/prediction_test.cpp`:

- *same inputs, same positions*: with no network trouble, the server never corrects the client;
- *answers at once*: with 100 ms of latency, the ship moves the very tick you press;
- *over a slow network*: the longest step drawn is never more than a step and a half, and there is no snap;
- *a wall it cannot see*: a server-only wall contradicts the client, the correction is spread over several ticks;
- *smoothing is needed*: the same test without smoothing shows jumps;
- *a whole run repeats*: same network, same inputs, same positions to the last bit.

You can build such tests for your own game with a `LoopbackNetwork` with `Conditions` and a clock you control.

Next: [11 Make R-Type](../11-make-rtype/README.md).
