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
- `sendInterval` (default 2 ticks: 30 snapshots per second at 60 Hz), or `sendRate` in snapshots per second
  (30, 64...) with `tickRate` of the room: `{.sendRate = 64, .tickRate = 128}` is every 2 ticks. A rate that is not a
  fraction of the tick rate is rounded to a whole number of ticks (`replication.interval()` says which). Without
  `tickRate`, a `sendRate` throws.

How snapshots work, because it explains the behaviour you will see:

- Snapshots go on the **unreliable** channel. The client **acknowledges** each one it applied.
- The next snapshot is computed **against the last one the client acknowledged**, not against the last one sent. So
  a lost snapshot costs nothing more than a bigger next one, and a client that has nothing (new, or too far
  behind) is simply sent everything.
- A snapshot holds only what changed: an entity that appeared (all its components), a component that changed, an
  entity that went. The same world always gives the same bytes. A big snapshot comes in several packets.
- **A packet is a part of at most 1100 bytes of records** (`ReplicationServerConfig::maxPartBytes`, the payload of one
  datagram: with the headers of a part it stays under `net::DATAGRAM_PAYLOAD`, 1200). A UDP datagram over what a path
  carries in one piece is cut by IP, and one lost fragment loses it all: parts of 6000 bytes, the old default, were
  lost far more often than their share of the loss, and over a path that carries 1200 bytes at a time a busy world never
  arrived (`tests/replication/datagram_test.cpp`). Parts of 1100 cost 2.6 % more bytes (headers). A snapshot needs all
  its parts: with 45 parts and 3 % of loss, one in four arrives whole, and the next one is tried 2 ticks later. So the
  answer to a big snapshot is **fewer bytes** (send less often, fewer components, smaller ones: quantisation, ticket
  022), not more parts.
- A record (an entity's components) is never cut: one over `maxPartBytes` is not sent (`stats().oversized`, and a
  warning in the log). Raise `maxPartBytes` for a game whose entities weigh more, knowing what it costs.
- To see where the bytes go: `stats().componentBytes` (by number in the registry), `stats().maxParts` and
  `multipartSnapshots` on the server, and `stats().incomplete` on the client (snapshots given up for want of a part).
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

**Interpolation.** Interpolated components are placed **two snapshot intervals** behind the newest snapshot
(`interpolationIntervals`, 2.0), between the last two values received: smooth at any frame rate, though snapshots
are sparse and some are lost. The interval is the one the snapshots show (the smallest gap between two that came),
so the delay follows the server's rate: 67 ms at 30 snapshots a second, 31 ms at 64. Two is the least that rides
over one lost snapshot (the next one arrives exactly when the drawing reaches the last). The price is that *other*
players are drawn in the past. That is the standard trade-off, and it is what the next section fixes for your own
player. The knobs of `ReplicationClientConfig`:

| | |
|---|---|
| `interpolationIntervals` | The delay, in snapshot intervals (2.0). |
| `interpolationDelay` | The delay in seconds, when above 0: it replaces the intervals (a fixed delay). |
| `snapshotInterval` | Ticks between snapshots until the client has seen two (2). |
| `adaptiveDelay` | Off by default. On, each time the drawing runs past the newest snapshot (`stats().starved`: one came late or was lost) the delay grows by an interval, up to `maxInterpolationIntervals` (6), and comes back one interval in 5 s while the snapshots are steady. `delay()` says where it is. |
| `maxExtrapolation` | Off by default (0). In seconds: past the newest snapshot, an interpolated component goes on along the trend of its last two values (its lerp with `t` above 1) for at most this long, then waits. A few tens of ms (0.05) hide one lost snapshot of a shooter, and a bounded guess is never far wrong. Needs a lerp that can go past 1 (a plain lerp does; one that clamps just holds). Not for what a player owns and predicts. |

Without `maxExtrapolation`, nothing is invented: past its last value, an entity waits.

### Rates for a game

Measured over the loopback (`tests/replication/rates_test.cpp`): ten ships moving, every component changing, cost
**430 bytes per snapshot** (43 bytes a ship, 20 of them the `Transform2D`), before the headers of the packet (a few
tens of bytes).

| Game | Tick | Snapshots | Delay (2 intervals) | One client sees | Ten ships to ten clients |
|---|---|---|---|---|---|
| Slow, co-op (Starfall, BrainVor) | 60 Hz | 30 a second (`sendInterval` 2) | 67 ms | about 13 KiB/s | about 130 KiB/s |
| Shooter | 128 Hz | 64 a second (`sendRate` 64, `tickRate` 128) | 31 ms | about 27 KiB/s | about 269 KiB/s |

That is the bandwidth of *every entity changing every snapshot*; the real figure is lower, as only what changed is
sent. The server's cost: `ReplicationServer::update` makes about 290 allocations per snapshot for those ten ships (the
state is captured into fresh maps); that is counted by the test so that it cannot grow unseen, and is not zero.
Pick the rate for what the game needs, then check the figure with your own entities (`stats().bytesSent`).

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
games that need instant muzzle flashes predict a cosmetic effect and let the server own the real bullet. KUGE gives two things for that,
and nothing about weapons (ammunition, cooldown, recoil, damage are the game's):

- **A predicted entity that the real one replaces.** At the click the client makes the smoke, the tracer, the grenade, and gives it a
  `PredictionKey{player, inputSequence, index}` with `PredictedSpawns::add()`; the room, which knows the input it applies (`Applied::sequence`),
  makes the real entity with the same key (the component `PredictionKey`, registered with `registerPredictionKey(registry)`, is sent once
  when the entity spawns). When the real one appears on the client, `PredictedSpawns::update()` removes the predicted one (or calls your
  `onConfirmed(predicted, real)` so that you can hand its sprite over), and one that the room never confirms (it refused the action) is
  removed after `timeout`. No double, and no gap.
- **A random number that agrees.** The spread and the recoil pattern of a shot must be the same on both sides, and the distributions of
  `<random>` are not the same from one standard library to another. `kuge::Random random(kuge::seedFor(player, tick))` gives the same
  numbers on every machine (`uniform()`, `range(a, b)`, `below(n)`, `chance(p)`).

The **weapon state** (ammo, cooldown, reload) is part of what the client predicts like the movement: put it in a component that the
registry marks `predicted = true`, apply the input to it in the same function as the room (`apply`), and the reconciliation copies it back and
replays it; show the corrected ammo without a jump. **What not to predict**: the hit, the damage, the kill.

## Looking at the world as a player saw it

A player sees the others about 100 ms in the past (the interpolation) plus its own delay to the server. A hitscan shot must be judged against
**the positions that the shooter saw**, not those of now. `WorldHistory` (`WorldHistory.hpp`) is the memory for that:

```cpp
WorldHistory history({.windowTicks = 15});                       // 250 ms at 60 ticks a second
history.keep<Transform3D>([](const Transform3D& a, const Transform3D& b, float t) { return lerp(a, b, t); });
history.keep<Hitboxes>();
...
history.record(world, tick);                                     // each tick, after the simulation

// the client puts replication.renderTick() in the input of the shot: the tick it was drawing (between two snapshots: a fraction)
const auto limit = history.limit(request.renderTick, tick, rtt, tickRate, replicationClient interpolation delay);
if (limit.verdict != WorldHistory::Limit::Verdict::TooOld && limit.verdict != WorldHistory::Limit::Verdict::Future) {
    history.rewound(world, limit.tick, [&](const WorldHistory::Rewound& past) {
        hit = physics.raycastHitboxes(world, origin, direction, range, mask, [&](kw::Entity e) { return !past.existed(e) || e == shooter; });
    });                                                          // everything is back as it was after the call, even if it throws
}
```

`limit()` is the guard against a client that lies about the tick: a request older than the window is **refused**, one in the future too, and
one further back than the delay of the interpolation plus the round trip of that player explains is **brought to that limit** (log a warning
with the player: a client that lies is worth knowing). Only the components that were `keep`t are moved; a tick between two recorded ones is
blended when there is a lerp. What is *not* rewound: the shooter itself and its own movement (leave it out with `select`, or ignore it), static
walls (they do not move). Name the entities with something that is never reused (`identity`, the network id) if some are removed during the
window: a World gives the number of a removed entity to the next one.

## Sending only what can be seen

An interest filter is also the anti-cheat of a game where seeing through a wall is cheating: an enemy that cannot be seen must not be in the
snapshots, or a hacked client draws it. `VisibilityFilter` does the work every such game would write again, and takes the rules as functions:

```cpp
VisibilityConfig config;
config.viewDistance = 120.0f;
config.viewer   = [&](ConnectionId c) -> optional<Viewer> { return eyesOf(c); };               // where the eyes are
config.position = [&](NetworkId id) -> optional<Placed>   { return positionOf(id); };          // none: not a thing of the world, always sent
config.clear    = [&](const Vec3& from, const Vec3& to)   { return !physics.raycast(from, to - from, 1.0f, WALLS); };   // the line of sight
config.always   = [&](ConnectionId c, NetworkId id, const EntityRecord&) { return sameTeam(c, id); };   // allies: the game's rule
VisibilityFilter visibility(config);
server.setFilter(visibility.filter());
...
visibility.newTick(tick);      // each tick before server.update(tick)
```

The way from the eye to a few points of the target (head, chest, feet) is tested **once per tick** per pair, and only for what is within
`viewDistance`. Eyes and target are also tried where they will be in `lookAhead` seconds, so a player who comes round a corner is sent a few
tens of milliseconds **before** it can be seen; and one that was visible stays for `graceTicks` after it is hidden. It protects what the
snapshots hold (positions, states), **not** the sound or anything else the game sends outside the replication. It looks at every entity for every
client: for hundreds of entities narrow them down first (a grid) in `position`. `server.setRate()` refreshes far entities only every nth
snapshot, and `server.stats()` counts what was sent, filtered and skipped.

## Recording a match, and replaying it

The replication already gives what a replay needs: snapshots numbered by tick, the same world always the same bytes. `Recorder` follows what a
client applies and keeps a whole snapshot (a **keyframe**) every few seconds with the changes between them, and the **events** the game writes down
(`recorder.event(tick, type, bytes)`: a kill, the angles of a player); with `ringSeconds` it keeps only the last seconds, a killcam, and `save()` or
`bytes()` writes them. `Replayer` gives the recording back to a `ReplicationClient` on a World of its own, with the hooks of a game (sprites, models):

```cpp
Recorder recorder(registry, {.tickRate = 60, .protocol = 3, .info = "dust", .ringSeconds = 8.0});
recorder.attach(replicationClient);                              // it follows every snapshot the client applies
...
recorder.save("kill.replay");

Replayer replay(registry, readFile("kill.replay"), replayClient, {.protocol = 3});     // @throw ReplayError
replay.update(frameSeconds);   // each frame INSTEAD of replayClient.update(): by the replay's time
replay.setSpeed(0.5);  replay.pause(true);  replay.seek(tick);       // seek lands on the keyframe before the tick
```

A replay of another protocol or of other components (an older version of the game) is refused with a message that says so. It is the world
as **this player** was sent it: with a visibility filter, that is what it could see. What it shows (a free camera, the killer's point of view,
a slow motion) is the game's.

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
