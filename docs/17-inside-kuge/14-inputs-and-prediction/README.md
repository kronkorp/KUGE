# Step 14: Inputs and prediction

Your own ship must answer your keys **now**, not a round trip later. The room must still decide everything. The two
meet on one idea: **inputs are numbered**. The client simulates each input at once and sends it with its number. The
room applies the inputs in order and says which number its state includes. The client then puts right what it
guessed wrong. This step reads the room's side (`InputServer`), then the client's (`Prediction`).

## The client sends numbered inputs, several times

```cpp
// modules/replication/src/Prediction.hpp, Prediction<Input>::tick (each fixed tick of the client)
if (!m_ready) {
    return;                                       // until the server has shown the player's entity
}
m_pending.push_back(Pending{++m_sequence, input}); // numbered, and kept until the server confirms it
simulate(input);                                  // on a private world: apply(), then step()
... the correction left over from the last snapshot fades (see below)
publish();                                        // the private entity's state, copied to the entity that is drawn
send();                                           // the last `redundancy` (4) inputs, in one InputPacket
```

`send()` puts the last four inputs in every packet, unreliable. A lost packet is not resent: its inputs are in the
next three.

## The room applies them, in order

```cpp
// modules/replication/src/Input.hpp, InputServer<Input>::receive (shortened)
for (std::uint32_t i = 0; i < packet.count; ++i) {
    ... decode input i
    const std::uint32_t sequence = packet.firstSequence + i;

    if (sequence < client.expected || client.queue.count(sequence)) {
        ++m_stats.duplicates;                     // already applied, already queued, or too late
        continue;
    }
    if (!client.started) {                        // the first input ever sets where the numbers begin
        client.started = true;
        client.expected = sequence;
    }
    client.queue.emplace(sequence, std::move(input));
}
```

```cpp
// InputServer<Input>::collect (once per tick of the room)
if (!client.queue.empty() && client.queue.size() > m_config.jitter && client.queue.begin()->first == client.expected) {
    // the next one is here: apply it
    applied.push_back(Applied{connection, client.expected, client.queue.begin()->second, false});
    client.last = client.queue.begin()->second;
    client.queue.erase(client.queue.begin());
    client.applied = client.expected++;
} else if (!client.queue.empty() && client.queue.size() > m_config.jitter) {
    // later ones are here, but not the next: it is lost. Its turn goes to the last input.
    applied.push_back(Applied{connection, client.expected, *client.last, true});
    client.applied = client.expected++;
} else {
    // nothing yet: the client is late. The last input goes on, and no number is used.
    applied.push_back(Applied{connection, client.applied, *client.last, true});
}
```

Three cases, one rule: **every tick, every player gets exactly one input**. The room never waits for a client, and
never applies two inputs in one tick. `jitter` (2 in the R-Type room) keeps that many inputs in reserve: the room
only uses one when more than two are queued, which absorbs the small differences between the client's and the
room's clocks. (The lab uses the default, 0.)

The room then calls `setInputAck(connection, applied.sequence)`, and that number travels in the next snapshot
(`SnapshotPacket::inputAck`).

## The client puts its guess right

When a snapshot shows the player's own entity, the replication client gives it to the prediction (`onOwned`):

```cpp
// modules/replication/src/Prediction.hpp, Prediction<Input>::reconcile (shortened)
const Vec2 before = position of the private entity;              // what we predicted

for (each predicted component the server sent) {
    m_registry.entry(index).apply(m_private, m_player, bytes);   // 1. the private entity: as the server has it
}
while (!m_pending.empty() && m_pending.front().sequence <= inputAck) {
    m_pending.pop_front();                                       // 2. what the server has seen: forgotten
}
for (const Pending& pending : m_pending) {
    simulate(pending.input);                                     // 3. what it has not seen yet: simulated again
    ++m_stats.replays;
}
const Vec2 after = position of the private entity;               // the new prediction
const Vec2 gap = (before + m_offset) - after;                    // where it is drawn, and where it should be

if (length(gap) > m_config.snapDistance) {
    m_offset = {};                                               // too far (a respawn): put it there
} else {
    m_offset = gap;                                              // otherwise: keep drawing it where it was...
}
publish();                                                       // ...and let the offset fade over `smoothing`
```

A worked example. The client is at tick 100: it has simulated inputs up to #100, and #95 to #100 are pending. A
snapshot arrives with the state after input #94 (`inputAck` 94, because the snapshot left the room a round trip
ago):

| | Private entity | Pending |
|---|---|---|
| Before | the prediction: after #100 | #95 … #100 |
| 1. Server's state | after #94, as the room computed it | #95 … #100 |
| 2. Drop confirmed | (unchanged) | #95 … #100 (#94 and older were gone already) |
| 3. Replay | after #100 again, starting from the server's #94 | #95 … #100 |

If the room computed the same thing as the client (the same code, the same inputs, deterministic physics), the
replayed position **equals** the one before, the gap is zero, and nothing moves on screen. `corrections` counts the
times it did not. Those are what the client could not know: another player in the way, a hit, a rule only the
server runs.

This is why the rest of the tutorial kept insisting on determinism. Sorted entity walks (steps 3 and 9), no clock
inside a tick (step 2), and inputs applied in order (above) are what make "replay and compare" work.

**When the entity goes away.** The prediction writes to the drawn entity at every tick, and the World reuses the id
of a removed entity (step 3): a stale one would write into whoever gets that id next. So the prediction lets go in
two cases. The replication client calls `onOwnedGone` just before it removes the player's entity (the server stopped
sending it). And each tick the prediction checks that its entity still has the `Replicated` it was told about, with
the same network id: that catches a game that removed it itself. In both cases the prediction forgets its private
world (`build` makes all of it again), and starts over when the server gives the player a new entity.

## Lab

The lab plays the room's side with a script: packets arrive, or get lost, or come late, and at each tick the
`InputServer` says what it applied.

`example/lab/lab14_inputs.cpp`:

```cpp
// Lab 14: the room's side of inputs. Packets of numbered inputs arrive (or not), and at each tick the
// InputServer gives one input per player, in order, whatever the network did.
#include "Endpoint.hpp"
#include "Input.hpp"
#include "Logger.hpp"
#include "Loopback.hpp"
#include <cstdio>
#include <vector>

namespace
{
    // The game's input: here, just a number to recognise it by
    struct Move
    {
        KUGE_MESSAGE(Move, dx)
        std::int8_t dx = 0;
    };

    // What Prediction sends: the first number, then each input as its length (2 bytes) and its bytes
    kuge::replication::InputPacket packet(std::uint32_t first, const std::vector<int>& inputs)
    {
        kuge::replication::InputPacket p;

        p.firstSequence = first;
        p.count = static_cast<std::uint8_t>(inputs.size());
        for (const int value : inputs) {
            kuge::ByteWriter one;

            kuge::net::encode(one, Move{static_cast<std::int8_t>(value)});
            p.data.push_back(static_cast<std::uint8_t>(one.size() & 0xFF));
            p.data.push_back(static_cast<std::uint8_t>(one.size() >> 8));
            p.data.insert(p.data.end(), one.bytes().begin(), one.bytes().end());
        }
        return p;
    }
}

int main()
{
    using namespace kuge::net;
    Logger::logger().enable(false);
    double now = 0.0;
    LoopbackNetwork network({}, [&now] { return now; });
    EndpointConfig config;

    config.clock = [&now] { return now; };
    Endpoint room(network.listen("room"), Role::Server, config);
    Endpoint player(network.connect("room"), Role::Client, config);
    kuge::replication::InputServer<Move> inputs(room);

    while (!player.connected() || !room.connected()) {
        now += 0.01;
        room.poll();
        player.poll();
    }
    inputs.addClient(room.connections().front());

    struct Tick { const char* what; std::uint32_t first; std::vector<int> inputs; };
    const Tick script[] = {
        {"input 1 arrives",                     1, {1}},
        {"inputs 1 and 2 (1 again: redundancy)", 1, {1, 2}},
        {"the packet with 3 is lost",            0, {}},
        {"inputs 3 and 4 arrive",                3, {3, 4}},
        {"input 6 arrives, not 5",               6, {6}},
        {"nothing arrives",                      0, {}},
        {"input 5 arrives, after its turn",      5, {5}},
    };
    int tick = 0;

    for (const Tick& step : script) {
        if (step.first != 0) {
            player.send(CLIENT_CONNECTION, packet(step.first, step.inputs), Channel::Unreliable);
        }
        now += 1.0 / 60.0;
        room.poll();
        for (const auto& applied : inputs.collect()) {
            std::printf("tick %d  %-38s -> applied input #%u (the one with dx %d)%s\n", ++tick, step.what, applied.sequence,
                applied.input.dx, applied.repeated ? ", the last one repeated" : "");
        }
    }
    const auto& stats = inputs.stats();

    std::printf("received %llu, copies or too late %llu, ticks filled by a repeat %llu\n",
        static_cast<unsigned long long>(stats.received), static_cast<unsigned long long>(stats.duplicates), static_cast<unsigned long long>(stats.filled));
}
```

What it prints (`./build/example/lab14_inputs`):

```text
tick 1  input 1 arrives                        -> applied input #1 (the one with dx 1)
tick 2  inputs 1 and 2 (1 again: redundancy)   -> applied input #2 (the one with dx 2)
tick 3  the packet with 3 is lost              -> applied input #2 (the one with dx 2), the last one repeated
tick 4  inputs 3 and 4 arrive                  -> applied input #3 (the one with dx 3)
tick 5  input 6 arrives, not 5                 -> applied input #4 (the one with dx 4)
tick 6  nothing arrives                        -> applied input #5 (the one with dx 4), the last one repeated
tick 7  input 5 arrives, after its turn        -> applied input #6 (the one with dx 6)
received 5, copies or too late 2, ticks filled by a repeat 2
```

## Reading the output

- **Ticks 1 and 2**: the normal case. Input 1 arrives twice (the second copy is the redundancy of the next packet)
  and is applied once.
- **Tick 3**: nothing arrived, so the client is late. Input 2 is used again, and the number stays 2: the room tells
  the client "my state includes up to #2", which is true.
- **Tick 4**: 3 and 4 arrive together, and only 3 is applied. One input per tick; 4 waits for the next.
- **Tick 5**: 4 is applied. 6 arrived, but not 5.
- **Tick 6**: 6 is there and 5 is not, so 5 is considered **lost**. Its turn is played with the last input (dx 4),
  under number 5. The client, told "#5 is included", replays from there, and the difference between its real #5
  and the repeated #4 becomes a small correction.
- **Tick 7**: 5 arrives, after its turn: dropped. 6 is applied. What the players did is what the room simulated, in
  order, never what arrived when.

Next: [Step 15: One keypress, end to end](../15-one-keypress/README.md).
