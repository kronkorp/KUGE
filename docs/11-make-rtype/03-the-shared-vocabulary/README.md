# Step 3: The shared vocabulary

File: `example/rtype/common/RType.hpp`. Everything both sides must agree on goes here, and nothing else.

## Sizes and speeds

```cpp
namespace rtype {

constexpr float ARENA_W = 640.0f;
constexpr float ARENA_H = 360.0f;
constexpr float SHIP_SPEED = 150.0f;       // pixels per second
constexpr float BULLET_SPEED = 360.0f;
constexpr float ENEMY_SPEED = 55.0f;
constexpr kuge::Vec2 SHIP_SIZE{16.0f, 10.0f};
constexpr kuge::Vec2 BULLET_SIZE{6.0f, 3.0f};
constexpr kuge::Vec2 ENEMY_SIZE{14.0f, 14.0f};
constexpr double TICK_DT = 1.0 / 60.0;
```

Speeds are in **pixels per second** and multiplied by `dt` in the simulation, so changing the tick rate does not
change the game.

## What kinds of entities exist

```cpp
constexpr kuge::replication::EntityType SHIP = 1;
constexpr kuge::replication::EntityType BULLET = 2;
constexpr kuge::replication::EntityType ENEMY = 3;
```

An `EntityType` is a number the room attaches to each replicated entity. The client uses it to decide what to
build (step 6). The room and the client only need to agree on these numbers.

## What a player does: the input message

```cpp
struct Steer
{
    KUGE_MESSAGE(Steer, dx, dy, fire)
    std::int8_t dx = 0;
    std::int8_t dy = 0;
    bool        fire = false;

    bool operator==(const Steer&) const = default;
};
```

`Steer` is **all a client ever tells the server about its player**: a direction on each axis (-1, 0 or 1) and
whether it is firing. Not a position, not a speed. The server computes what happens. This is the rule of an
**authoritative server**: clients send *intentions*, never *results*, so a cheating client can only ask for
things the rules allow.

`KUGE_MESSAGE` makes `Steer` a network message (see [08](../../08-the-network/README.md)) and, being a plain
struct, also the input type of the prediction (see step 7). Keep it **small**: it is sent 60 times a second, four
copies at a time.

## Components

```cpp
struct Health { int points = 0; };                          // replicated when it changes
struct Score  { int points = 0; };                          // replicated when it changes
struct Slot   { std::uint8_t color = 0; };                  // replicated once: the colour of a ship

// The server alone knows these:
struct Gun     { std::uint32_t cooldown = 0; };
struct Owned   { std::uint32_t player = 0; };               // the player a ship or a bullet belongs to
struct Bullet  { kuge::Vec2 velocity; };
struct Enemy   { float baseY = 0.0f; float phase = 0.0f; float amplitude = 25.0f; };
```

Ask, for each component: **does the client need it?**

- `Health` and `Score`: yes, to show them. They change now and then: `OnChange`.
- `Slot`: yes, once, to choose a colour. It never changes: `OnSpawn`.
- `Gun`, `Owned`, `Bullet`, `Enemy`: no. They are the server's bookkeeping. A client never learns them, which is
  both less bandwidth and less for a cheater to read.

Position and movement (`Transform2D`, `Body`) come from the engine.

## Settings

```cpp
struct Settings
{
    std::uint32_t waveSize      = 30;    // enemies in a game
    std::uint32_t spawnEvery    = 45;    // ticks between two enemies
    int           shipHealth    = 5;
    int           enemyHealth   = 2;
    std::uint32_t fireCooldown  = 12;    // ticks between two shots
    float         enemySpeed    = ENEMY_SPEED;
};

inline Settings& settings() { static Settings value; return value; }
```

Game parameters in one place. The tests shorten a game by changing them (see step 9).

## Saying what is replicated

```cpp
inline kuge::replication::ReplicationRegistry makeRegistry()
{
    using namespace kuge::replication;
    ReplicationRegistry registry;

    registerTransform2D(registry, Replicate::Interpolated, /*predicted*/ true);
    registerBody(registry, Replicate::OnChange, /*predicted*/ true);
    registry.component<Health>("Health", Replicate::OnChange,
        [](kuge::ByteWriter& out, const Health& h) { out.write<std::int32_t>(h.points); },
        [](kuge::ByteReader& in) { return Health{in.read<std::int32_t>()}; });
    registry.component<Score>("Score", Replicate::OnChange, ... );
    registry.component<Slot>("Slot", Replicate::OnSpawn, ... );
    return registry;
}
```

Read it as a table of what the server tells the clients:

| Component | Mode | Why |
|---|---|---|
| `Transform2D` | `Interpolated`, predicted | Position moves all the time: the client draws it between snapshots. Your own ship is predicted. |
| `Body` | `OnChange`, predicted | The velocity (and what touches a wall) is part of the state your prediction must start from. |
| `Health`, `Score` | `OnChange` | Shown on screen |
| `Slot` | `OnSpawn` | A colour, told once |

**One function builds it, and both sides call it.** The order of the registrations is the number of a component
on the wire, and a hash of the whole list goes with every snapshot: if you add a component on the server and forget
the client, the client ignores the snapshots instead of reading garbage. That is the reason for one shared function.

Each component needs a way to be written and read. `registerTransform2D` and `registerBody` are ready-made; for
your own components you give two lambdas, using the same `ByteWriter` and `ByteReader` as everywhere else.

Next: [Step 4: The arena and the rules](../04-the-arena-and-the-rules/README.md).
