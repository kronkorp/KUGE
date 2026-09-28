# Step 13: Replication

The room has a World; each client wants a copy of the part that matters. `kuge-replication` does it with snapshots
that are **differences**: each one only says what changed since a state the client is known to have. This step
reads the three pure functions everything rests on (capture, diff, apply), then what the server and the client do
with them at each tick.

## What is replicated: the registry

```cpp
// modules/replication/src/Replication.hpp, ReplicationRegistry::component<C> (shortened)
Entry entry;

entry.name = std::move(name);
entry.mode = mode;                                   // Interpolated, OnChange or OnSpawn
entry.predicted = predicted;
entry.has   = [](kw::World& world, kw::Entity e) { return world.has<C>(e); };
entry.write = [write](kw::World& world, kw::Entity e, ByteWriter& out) { write(out, world.get<C>(e)); };
entry.apply = [read](kw::World& world, kw::Entity e, std::span<const std::uint8_t> bytes) {
    ... read it (checked), and add or replace the component
};
... remove, and interpolate (for Interpolated: lerp between two values given as bytes)
m_entries.push_back(std::move(entry));               // its index is its number on the wire (64 at most)
```

A registry turns each component type into a few functions that work on **bytes**. From there on, the replication
never knows a component's C++ type: it compares bytes, sends bytes, applies bytes.

```cpp
// modules/replication/src/Replication.cpp, ReplicationRegistry::schema
for (const auto& entry : m_entries) {
    all += entry.name + ":" + std::to_string(static_cast<int>(entry.mode)) + ";";
}
return net::hashName(all);
```

The schema, a hash of the list, goes with every snapshot. A client whose list differs ignores the snapshots rather
than reading a Health as a position.

## Capture, diff, apply

```cpp
// modules/replication/src/WorldState.hpp
struct EntityRecord
{
    EntityType                       type  = 0;
    NetworkId                        owner = 0;
    std::map<std::uint8_t, Bytes>    components;     // by number in the registry
};
using WorldState = std::map<NetworkId, EntityRecord>;
```

- **`captureState(world, registry)`** walks the entities that have a `Replicated` (the server's `track()` adds it),
  and writes each registered component to bytes. A `WorldState` is a `std::map`, ordered by network id, so the same
  World always gives the same state, whatever the ECS order.
- **`diffOps(registry, baseline, current)`** compares two states and makes one record per entity that appeared,
  changed or went:

| Record | Bytes |
|---|---|
| Spawn | `1`, network id (u32), type (u16), owner (u32), mask of the components (u64), then for each: size (u16) + bytes |
| Update | `2`, network id, mask of the changed ones (u64), mask of the removed ones (u64), then each changed: size + bytes |
| Destroy | `3`, network id |

  "Changed" means **the bytes differ**. `OnSpawn` components are only in Spawn records. With no baseline, everything
  is a Spawn.
- **`applyOps(registry, baseline, ops)`** is the reverse: it starts from a copy of the baseline and applies the
  records. It throws `SerializerError` on anything that does not make sense, such as an unknown record kind, a
  component number that is not registered, or an update of an entity that is not there.

## The server: one baseline per client

```cpp
// modules/replication/src/ReplicationServer.cpp, ReplicationServer::update (shortened)
... the clients whose last snapshot is at least sendInterval ticks old (2: 30 snapshots a second)
const auto current = std::make_shared<const WorldState>(captureState(m_world, m_registry));   // once

for (const net::ConnectionId connection : due) {
    ... (with a filter: a copy with only what this client may see)
    send(connection, client, tick, current);
}

// ReplicationServer::send
const std::vector<Bytes> ops = diffOps(m_registry, client.baseline.get(), *view);
... the records packed into parts of at most maxPartBytes (6000): a big snapshot takes several packets
... each part: SnapshotPacket{schema, tick, baseTick = client.baselineTick, inputAck, part, parts, ops}, unreliable
client.history[tick] = view;                          // what was sent, until the client says it has it
```

```cpp
// ReplicationServer::onAck: the client says it applied the snapshot of `tick`
if (tick == 0) {                                      // "I have nothing": start again from nothing
    client.baseline.reset();
    ...
    return;
}
const auto sent = client.history.find(tick);
if (sent == client.history.end() || tick <= client.baselineTick) {
    return;                                           // an old ack, or a snapshot that is not kept
}
client.baseline = sent->second;                       // the next snapshots are diffs from this one
client.history.erase(client.history.begin(), std::next(sent));
```

The **baseline** is the last state the client acknowledged, not the last one sent. So a lost snapshot costs nothing
but a slightly bigger next one: the next diff still starts from a state the client has. Snapshots can therefore go
unreliable, and never wait for a resend.

## The client: build from what you acknowledged

```cpp
// modules/replication/src/ReplicationClient.cpp, ReplicationClient::process (shortened)
const WorldState* baseline = &nothing;

if (baseTick != 0) {
    const auto found = m_states.find(baseTick);       // the client keeps the last 32 states it applied
    if (found == m_states.end()) {
        ++m_stats.missingBaseline;
        ack(0);                                       // "I cannot follow: send everything"
        return;
    }
    baseline = &found->second;
}
WorldState next = applyOps(m_registry, *baseline, ops);
applyToWorld(next, tick, inputAck);                   // make the World look like it
m_states[tick] = std::move(next);
ack(tick);                                            // SnapshotAck, unreliable
```

`applyToWorld` compares `next` with what the World shows:

- **gone** entities: the `onDestroy` hook, then `world.remove`;
- **new** ones: `world.create()`, a `Replicated{id, type, owner}`, the components, then the `onSpawn` hook, where the
  game adds what the server never sends (a `Sprite`);
- **changed** ones: `OnChange` components are applied at once; `Interpolated` ones are **not** applied, but kept as
  samples `(tick, bytes)`; the predicted components of your own entity are skipped and given to the prediction
  (step 14).

Snapshots are built from what the client **acknowledged**, and the client keeps the last 32 states. So an entity
that appeared and went between two snapshots the client received, or a value that changed and came back, cannot be
left wrong.

## Interpolation: drawing a little in the past

```cpp
// ReplicationClient::update(frameSeconds), each frame
const double delay = m_config.interpolationDelay * rate;             // 0.1 s, in ticks
m_sinceLatest += frameSeconds;
const double target = m_lastApplied - delay + m_sinceLatest * rate;  // where the "render clock" should be

m_renderTick += frameSeconds * rate;                                 // it moves with real time...
m_renderTick += (target - m_renderTick) * std::min(1.0, frameSeconds * 5.0);   // ...and eases towards the target
m_renderTick = std::min(m_renderTick, static_cast<double>(m_lastApplied));    // never past the last snapshot

... for each interpolated component: drop the samples older than m_renderTick,
... and lerp between the two that surround it (one sample left: it stays there, nothing is invented)
```

The client draws other entities at a moment 0.1 s behind the newest snapshot. With 30 snapshots a second there are
about three snapshots in that margin. A lost one only means interpolating across a wider gap, never a jump.

## Lab

The lab calls the three functions by hand on a World, with no network: exactly what the server and the client do
with each snapshot.

`example/lab/lab13_replication.cpp`:

```cpp
// Lab 13: what a snapshot is made of. The same functions the ReplicationServer and ReplicationClient use,
// called by hand on a World: capture, diff against what the client has, apply.
#include "CommonComponents.hpp"
#include "Logger.hpp"
#include "WorldState.hpp"
#include <cstdio>

namespace
{
    using namespace kuge::replication;

    struct Health { int points = 0; };

    void print(const char* title, const std::vector<Bytes>& ops)
    {
        static const char* kinds[] = {"?", "Spawn", "Update", "Destroy"};
        std::size_t total = 0;

        std::printf("%s\n", title);
        for (const Bytes& op : ops) {
            const std::uint32_t id = op[1] | op[2] << 8 | op[3] << 16 | static_cast<std::uint32_t>(op[4]) << 24;

            std::printf("  %-7s entity %u, %2zu bytes:", kinds[op[0] <= 3 ? op[0] : 0], id, op.size());
            for (std::size_t i = 0; i < op.size() && i < 24; ++i) {
                std::printf(" %02x", op[i]);
            }
            std::printf("%s\n", op.size() > 24 ? " ..." : "");
            total += op.size();
        }
        std::printf("  (%zu bytes in all)\n", total);
    }

    Bytes join(const std::vector<Bytes>& ops)
    {
        Bytes all;

        for (const Bytes& op : ops) {
            all.insert(all.end(), op.begin(), op.end());
        }
        return all;
    }
}

int main()
{
    Logger::logger().enable(false);
    ReplicationRegistry registry;

    registerTransform2D(registry, Replicate::Interpolated);                     // component number 0
    registry.component<Health>("Health", Replicate::OnChange,                   // component number 1
        [](kuge::ByteWriter& out, const Health& h) { out.write<std::int32_t>(h.points); },
        [](kuge::ByteReader& in) { return Health{in.read<std::int32_t>()}; });
    std::printf("schema (hash of the list) = %08x\n\n", registry.schema());

    kw::World world;
    const kw::Entity ship = world.create();
    const kw::Entity rock = world.create();

    world.add<kuge::Transform2D>(ship, kuge::Transform2D{{10.0f, 20.0f}});
    world.add<Health>(ship, Health{3});
    world.add<Replicated>(ship, Replicated{1, /*type*/ 1, /*owner*/ 0});        // what ReplicationServer::track() adds
    world.add<kuge::Transform2D>(rock, kuge::Transform2D{{50.0f, 60.0f}});
    world.add<Replicated>(rock, Replicated{2, 2, 0});

    const WorldState a = captureState(world, registry);
    print("A client that has nothing gets:", diffOps(registry, nullptr, a));

    world.get<kuge::Transform2D>(ship).position.x = 11.0f;
    const WorldState b = captureState(world, registry);
    print("\nThe ship moved; a client that has A gets:", diffOps(registry, &a, b));

    world.get<Health>(ship).points = 2;
    world.remove(rock);
    const WorldState c = captureState(world, registry);
    print("\nThe ship was hit and the rock is gone; a client that has B gets:", diffOps(registry, &b, c));

    // A client that acknowledged A (and missed B) gets the changes from A to C, and ends up with C
    const Bytes ops = join(diffOps(registry, &a, c));
    const WorldState rebuilt = applyOps(registry, a, ops);
    std::printf("\nFrom A, with %zu bytes, a client that missed B rebuilds C exactly: %s\n", ops.size(), rebuilt == c ? "yes" : "no");
}
```

What it prints (`./build/example/lab13_replication`):

```text
schema (hash of the list) = 96aa9468

A client that has nothing gets:
  Spawn   entity 1, 47 bytes: 01 01 00 00 00 01 00 00 00 00 00 03 00 00 00 00 00 00 00 14 00 00 00 20 ...
  Spawn   entity 2, 41 bytes: 01 02 00 00 00 02 00 00 00 00 00 01 00 00 00 00 00 00 00 14 00 00 00 48 ...
  (88 bytes in all)

The ship moved; a client that has A gets:
  Update  entity 1, 43 bytes: 02 01 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 14 00 00 ...
  (43 bytes in all)

The ship was hit and the rock is gone; a client that has B gets:
  Update  entity 1, 27 bytes: 02 01 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 04 00 02 ...
  Destroy entity 2,  5 bytes: 03 02 00 00 00
  (32 bytes in all)

From A, with 54 bytes, a client that missed B rebuilds C exactly: yes
```

## Reading the output

- **Spawn of the ship, 47 bytes**: `01` (Spawn), `01 00 00 00` (network id 1), `01 00` (type 1), `00 00 00 00` (no
  owner), `03 00 …` (mask: components 0 and 1), then `14 00` (20 bytes: the Transform2D, five floats, starting with
  10.0 = `00 00 20 41`), then 2 + 4 bytes of Health. 19 + 22 + 6 = 47.
- **The rock** has no Health: 41 bytes. A client that has nothing gets 88 bytes.
- **The ship moved**: one Update of 43 bytes. The changed mask is `01` (component 0), and the removed mask is empty.
  Health did not change, so it is not sent. The rock did not change, so it is not mentioned at all.
- **Hit and gone**: an Update with mask `02` (Health, now 2) and a Destroy of 5 bytes. The position is not resent:
  it did not change since B.
- **A client that missed B**, because the snapshot was lost, still has A. The server diffs from A: 54 bytes (the
  ship's two components, and the rock's Destroy). `applyOps` from A gives exactly C. That is the whole point of
  diffing from what was acknowledged.

Next: [Step 14: Inputs and prediction](../14-inputs-and-prediction/README.md).
