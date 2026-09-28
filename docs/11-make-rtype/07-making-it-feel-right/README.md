# Step 7: Making it feel right

At the end of step 6 the game works, but **your own ship lags**. You press a key, the input travels to the server
(say 50 ms), the server moves the ship, the snapshot travels back (50 ms more), and only then does your ship move.
A tenth of a second between key and reaction feels like steering through mud.

Three tools fix three different things:

| Problem | Tool |
|---|---|
| Your ship answers late | **Prediction** |
| Other ships move in steps of 33 ms | **Interpolation** |
| Server and client tick at slightly different times | **A jitter buffer**, and care about clocks |

## Interpolation (already there)

`ReplicationClient::update(frameDt)` places every *interpolated* component (the `Transform2D` of other ships,
bullets and enemies) **a little in the past**, `interpolationDelay` = 0.1 s behind the newest snapshot, between the
last two values received. Snapshots come 30 times a second, so there are always two to draw between. The other
ships move smoothly on any screen, at the price of being drawn ~100 ms late, which nobody can notice on an object
they do not control.

Nothing to write: you called `update(frameDt)` in step 6.

## Prediction

Only **your own ship** is predicted. In `RTypeScene::joined`:

```cpp
kuge::replication::PredictionConfig<Steer> config;

config.build = [](kw::World& w) { buildArena(w); return buildShip(w); };   // a private world: arena + ship
config.apply = &steerShip;                                                  // input -> the ship's velocity
config.step  = &stepArena;                                                  // one tick of physics
config.dt = TICK_DT;
m_prediction = std::make_unique<kuge::replication::Prediction<Steer>>(config, world(), m_registry, *m_replication, room);
```

And in `tick()`, which you already have: `m_prediction->tick(steer)`.

Look at what you supplied: `buildArena`, `buildShip`, `steerShip`, `stepArena`, all from `common/`, all written in
step 4 before any network existed. **The client predicts by running the game's own movement code on a private
copy of the world that contains only the arena and your ship.**

What happens each tick:

1. The input `Steer` gets a number and is simulated on the private world. Your ship (the entity that is drawn) is
   set to the result: it moves **this very tick**, walls included.
2. The last four inputs are sent to the server.

What happens each snapshot (about 30 a second): the server sends the state of your ship **and the number of the
last input it applied**. The client puts its private ship in that state and simulates again the inputs the server
has not seen yet. If the server agrees with the client, nothing changes on screen. If not, the correction is not
shown as a jump: it is spread over about 0.1 s.

## What you must make sure of

For this to work:

1. **The same code on both sides.** `steerShip` and `stepArena` are shared. Done, by construction.
2. **Determinism.** `stepArena` uses the physics, which is deterministic. There is no `random_device` and no
   clock in what is predicted.
3. **The room reports which input it applied** (`setInputAck`, step 5). Without it the client cannot know which
   inputs to simulate again.
4. **The type is declared**: `replication.predictType(SHIP)`. Only ships owned by you are predicted. Your bullets
   are also "owned" by you, but they are moved by the server and only interpolated. (Before `predictType`
   existed, a bug made a player's own bullets freeze on its screen: a good example of why "owned" and "predicted"
   are different ideas.)

## Reading the numbers

The scene's `ClientReport` holds `prediction.corrections` and `prediction.reconciliations`. On a good network they
should be very close to zero corrections for hundreds of snapshots. That is your proof that the client and the
server simulate the same thing. When corrections climb, one of these is true:

- the client and the room run **different code** for movement;
- something in the predicted state is **not deterministic**;
- the room's clock and the client's clock **drift** (see below);
- something on the server touches your ship that the client cannot know (a real correction, and a legitimate one).

## The clocks

This is the subtle one, and it was learned the hard way while building this game.

The client sends **one input per tick**. The room consumes **one input per tick**. If the client ticks even 2 %
faster than the room, the room's queue of inputs grows without end, the confirmation of your inputs lags more and
more, and the prediction constantly disagrees. It happened with the scripted client: it slept 16 ms between frames
instead of 1/60 s (16.667 ms), so it ran 4 % faster than the server.

What to do about it:

- **Run on a real clock.** `Engine::run()` counts real elapsed time and runs exactly the ticks that time owes:
  no drift. The scripted runs (`--frames`) sleep to an **absolute schedule** (`next += 16667µs; sleep_until(next)`)
  instead of sleeping "a bit" each frame.
- **Give the room some margin.** `InputServerConfig{.jitter = 2}` makes the room wait until two inputs are
  queued before it uses one, so a late packet does not force a "repeat the last input" that the client did not
  simulate. It adds about 2 ticks (33 ms) of input delay on the server, which the prediction hides.
- **Drain the sockets.** A poll of a socket transport reads until the socket is empty. (A version that read one
  datagram per poll made the room hear its inputs later and later: 174 corrections in 429 snapshots instead of 1.)

## Tuning

| What | Where | Trade-off |
|---|---|---|
| Snapshots per second | `ReplicationServerConfig::sendInterval` (default 2 ticks) | fewer: less bandwidth, choppier others |
| How far in the past others are drawn | `ReplicationClientConfig::interpolationDelay` (0.1 s) | less: more responsive, but a lost snapshot shows |
| How long a correction takes | `PredictionConfig::smoothing` (0.1 s) | longer: smoother, but the ship is "wrong" for longer |
| When to snap instead of smooth | `PredictionConfig::snapDistance` (64 px) | |
| Room's input margin | `InputServerConfig::jitter` | more: safer against jitter, more delay |
| Inputs per packet | `PredictionConfig::redundancy` (4) | more: survives more loss, bigger packets |

## Try it under bad conditions

The loopback can lose, delay and reorder packets (see [08](../../08-the-network/README.md)). The prediction tests
run over `Conditions{.loss = 0.1, .latency = 0.05, .jitter = 0.02}`, which is 100 ms of round trip with 10 % loss,
and the ship still never jumps by more than a step and a half. Copy that test setup for your own game
(`tests/replication/prediction_fixture.hpp`) and you have a lab for prediction.

Next: [Step 8: Three ways to run it](../08-three-ways-to-run-it/README.md).
