# Step 15: One keypress, end to end

This last step has no new code. It follows one keypress through every step of this tutorial. Ana presses Right in
the R-Type client; her ship moves on her screen at once, and on Ben's screen a little later. Here is everything that
happens in between, in order, with the step that explains each part.

## On Ana's machine: the answer is immediate

| # | Where | What happens | Step |
|---|---|---|---|
| 1 | The OS, SDL | A key event waits in SDL's queue. | 8 |
| 2 | `Engine::step`, `ClientModule::beginFrame` | At the start of the next loop, the backend's `poll` hands the event to `InputMap::handle`: `m_keys[Right] = true`, the `Right` action's bit goes down, and `pressed` is latched. | 2, 6, 8 |
| 3 | `SceneLoop::run` | The accumulator owes a tick (or several, or none: then this loop only draws). | 2 |
| 4 | Fixed, `Input` stage: `SampleInput` | `sampleTick`: the `ActionState` of this tick has `Right` down. | 4, 8 |
| 5 | Fixed, `Simulation`: the game's system | It builds `Steer{dx = 1}` and calls `Prediction::tick`. | 14 |
| 6 | `Prediction::tick` | Input #N is numbered, applied to the private ship (`steerShip`: a velocity), and the private world steps (`stepArena`: the physics moves the ship, walls included). The result is copied to the ship that is drawn. **Ana's ship has moved, in this very tick.** | 9, 14 |
| 7 | `Prediction::send` → `Endpoint::send` | An `InputPacket` with inputs #N-3 … #N, unreliable: a `Data` packet with ack, ack bits, and the message. | 10, 14 |
| 8 | `SocketClient::send` → kronknet | One UDP datagram, sent whole (or dropped if the socket is full: the next three packets carry #N again). | 10 |
| 9 | Frame, `Render`: `SpriteRender` | The ship is drawn between its last two positions (`PreviousTransform2D`, `alpha`). | 8 |
| 10 | `ClientModule::endFrame` | `present()`: Ana sees her ship move. Worst case, one loop after the key: a few milliseconds. | 6 |

## Across the network

Half a round trip. Over UDP, the packet may be lost; the next ones carry the input again.

## In the room: the decision

The room is a scene on its own thread (a dedicated `SceneLoop`), in the server's engine.

| # | Where | What happens | Step |
|---|---|---|---|
| 11 | The room's loop wakes up | `runDedicated` slept until its next tick (`StopSignal::waitFor`). | 7 |
| 12 | Fixed, `Network`: `Net::poll` → `Endpoint::poll` | The transport reads every waiting datagram. `handlePacket` reads the ack (for the room's own reliable messages), finds an unreliable message, and queues its handler. At the end of `poll`, `InputServer::receive` puts #N in Ana's queue. | 10, 11 |
| 13 | Fixed, `Simulation`: `applyInputs` | `collect()`: the room keeps 2 inputs in reserve (`jitter`), so #N is applied about two ticks after it arrived. `steerShip` sets the ship's velocity, the same function the client used. `setInputAck(Ana, N)`. | 14 |
| 14 | Fixed, `Physics` | `stepArena`: the ship moves, in entity order, with sub-steps. It is exactly what Ana's client computed. | 9 |
| 15 | Fixed, `Late`: the rules | Bullets, enemies, hits. Things the client did not predict. | |
| 16 | Fixed, `Replication`: `ReplicationServer::update` | Every other tick: `captureState`, then for each client, a diff against what that client acknowledged. Ana's new position is a Transform2D in an Update record; `inputAck = N` goes with it. | 13 |
| 17 | `Endpoint::send`, unreliable | One or more `SnapshotPacket`s, to Ana and to Ben. | 10 |

## Back on the clients: confirmation and interpolation

| # | Where | What happens | Step |
|---|---|---|---|
| 18 | Ana, `Network` stage | `ReplicationClient::onPacket`: parts assembled, built from the acknowledged baseline, applied. The ship is hers and predicted, so its position goes to `Prediction::reconcile`: server state after #N, inputs after #N replayed. Same result as predicted: **nothing moves**. A `SnapshotAck` goes back. | 13, 14 |
| 19 | Ben, `Network` stage | The same snapshot, applied to Ben's World: Ana's ship gets a new position **sample**. | 13 |
| 20 | Ben, each frame | `ReplicationClient::update` places Ana's ship 0.1 s behind the newest snapshot, between two samples. Ben sees her move, smoothly, about half a round trip + the room's margin + half a round trip + 0.1 s after she pressed. | 13 |

## The budget

| Who | Sees the keypress after |
|---|---|
| Ana | at most one loop (plus the screen) |
| The room | half a round trip, plus up to a tick of waiting, plus the input margin (`jitter`, 2 ticks) |
| Ben | what the room needed, plus half a round trip, plus the interpolation delay (0.1 s) |

With 50 ms each way, Ben sees Ana's move about 0.25 s after it happened, and it looks smooth. Ana sees it at once,
and the room agrees with her afterwards, most of the time without changing a pixel. That is the whole of what KUGE's
network layers exist for.

## Where the guarantees come from

Every row of those tables relies on a rule from an earlier step:

- **One clock, driven from outside** (step 2): ticks are counted, never timed inside the simulation, so the room and
  the client compute the same thing from the same inputs.
- **Sorted walks** (steps 3 and 9): the ECS order never leaks into a result.
- **Nothing shared between scenes, only messages** (steps 5 and 7): the room and the lobby cannot race on data.
- **Modules lend, and outlive** (step 6): a scene may hold what the client module gave it.
- **Bytes are explicit** (step 10): little-endian, sized, and checked on reading, the same on every machine.
- **Handlers run when the endpoint is in order** (step 11): the game may do anything from a handler, even destroy the
  endpoint.
- **Diffs from what was acknowledged** (step 13): losing a snapshot costs bytes, never correctness.
- **Numbered inputs, applied in order** (step 14): the room and the client can agree on "the state after input #N".

## Where to go from here

- **Read the tests** of the part you care about. They are short, and each one states a guarantee: for example
  `tests/net/reliable_test.cpp`, `tests/replication/prediction_test.cpp`, `tests/core/spawn_test.cpp`.
- **Break things on purpose.** Register one more component on the client than on the room (its snapshots are then
  ignored, and `wrongSchema` counts them), remove the `std::sort` in `Physics2D::collect`, or set `jitter` to 0 in
  the R-Type room. Then run the tests, or the R-Type host with a lossy loopback, and watch what changes. It is the
  fastest way to see why each line is there.
- **Run the thread tests under ThreadSanitizer** (`-DKUGE_SANITIZE=thread`, see [12](../../12-testing-and-debugging/README.md)).
- The [pitfalls page](../../14-pitfalls/README.md) is the list of what went wrong while the engine was built. After
  this tutorial, each item on it should read as a consequence of something you now know.
