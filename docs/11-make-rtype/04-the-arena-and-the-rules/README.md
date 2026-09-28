# Step 4: The arena and the rules

Files: `common/RType.hpp` (the arena, how a ship moves) and `common/Rules.cpp` (the rules).

**There is no network and no screen in this step**, and you can test everything you write here with plain
functions on a World. The best time to find a bug in game logic is before a network is involved.

## The arena: four walls

```cpp
inline void buildArena(kw::World& world)
{
    world.addResource<kuge::Physics2D>(kuge::PhysicsConfig{});
    const auto wall = [&world](float x, float y, float w, float h) {
        const kw::Entity e = world.create();

        world.add<kuge::Transform2D>(e, kuge::Transform2D{{x, y}});
        world.add<kuge::Collider>(e, kuge::Collider::box(w, h));       // a collider with no Body is a wall
    };

    wall(ARENA_W / 2, -10.0f, ARENA_W + 40.0f, 20.0f);              // top
    wall(ARENA_W / 2, ARENA_H + 10.0f, ARENA_W + 40.0f, 20.0f);     // bottom
    wall(-10.0f, ARENA_H / 2, 20.0f, ARENA_H + 40.0f);              // left
    wall(ARENA_W + 10.0f, ARENA_H / 2, 20.0f, ARENA_H + 40.0f);     // right
}
```

`buildArena` works on **any World**, not on a scene. That is deliberate: the room calls it on its scene's
World, and the client's prediction calls it on a private World (step 7). Both get the same walls.

It adds a `Physics2D` resource with the default configuration (`gravity` does not matter: ships are kinematic,
and nothing falls).

## The ship

```cpp
inline kw::Entity buildShip(kw::World& world, kuge::Vec2 at = {40.0f, ARENA_H / 2})
{
    const kw::Entity ship = world.create();
    kuge::Body body;

    body.type = kuge::Body::Type::Kinematic;                 // moves by its velocity only
    world.add<kuge::Transform2D>(ship, kuge::Transform2D{at});
    world.add<kuge::Body>(ship, body);
    world.add<kuge::Collider>(ship, kuge::Collider::box(SHIP_SIZE.x, SHIP_SIZE.y));
    return ship;
}
```

Only what the **movement** needs: position, a body, a collider. The health, the gun and the owner are added by
the room, because a client's private world should not have them.

## How a ship moves: two tiny functions

```cpp
inline void steerShip(kw::World& world, kw::Entity ship, const Steer& steer)
{
    world.get<kuge::Body>(ship).velocity = {steer.dx * SHIP_SPEED, steer.dy * SHIP_SPEED};
}

inline void stepArena(kw::World& world, double dt)
{
    world.getResource<kuge::Physics2D>().step(world, static_cast<float>(dt));
}
```

**These two functions are the heart of prediction.** The room calls them for every player each tick; the client
calls the very same ones to predict its ship. If they differ by one line, the client and the server disagree and
the ship jitters. Put them in `common/` and never copy them.

## The rules

`Rules.cpp` is **plain functions on a World**. First, what the server alone tracks:

```cpp
struct Match                                    // a resource of the room's World
{
    std::uint32_t tick = 0;
    std::uint32_t spawned = 0;                   // enemies made so far
    std::uint32_t killed = 0;
    std::mt19937  rng{1};                        // seeded: the same game every time
    // Called for each entity the rules make, to have it replicated (bullets, enemies)
    std::function<void(kw::Entity, kuge::replication::EntityType, std::uint32_t)> track;
};
```

`track` is how the rules ask "please tell the clients about this new entity" **without knowing what replication
is**: the room fills it in, and a test fills it with an empty lambda.

The rules themselves:

```cpp
void fire(kw::World& world, kw::Entity ship);      // a shot, if the gun is ready
void stepRules(kw::World& world, double dt);       // one tick: bullets fly, enemies come and move, things are hit
Outcome outcome(kw::World& world, bool anyShipEver);   // Playing, Won or Lost
std::optional<kw::Entity> shipOf(kw::World& world, std::uint32_t player);   // a player's ship, if it is alive
```

`stepRules` does, in this order:

1. **Guns cool down.**
2. **Bullets fly** and are removed when they leave the arena.
3. **An enemy is made** every `spawnEvery` ticks, until the wave is complete. Its height comes from the seeded
   generator.
4. **Enemies cross the screen**, waving up and down with a sine, and are removed when they leave.
5. **Bullets hit enemies.** A hit removes the bullet and takes a point of health; at zero the enemy dies and the
   shooter's `Score` goes up.
6. **Enemies hit ships.** The enemy is removed and the ship loses a point.
7. **Dead ships are removed.**

Two habits that matter in game logic:

- **Sort before you act.** Every loop that changes the World goes over entities in *increasing entity order*
  (a small `sorted<C>(world)` helper). A view's order depends on how the ECS happens to store things, and it
  changes when entities are removed. Sorted iteration is what makes the rules **deterministic**.
- **Seed your random numbers.** `rng` is seeded (in the room: with the room's id), so the same seed gives the
  same game. Never use `std::random_device` in a rule.

The game ends with:

```cpp
Outcome outcome(kw::World& world, bool anyShipEver)
{
    const auto& match = world.getResource<Match>();

    if (anyShipEver && ships(world).empty()) { return Outcome::Lost; }      // every ship is dead
    if (match.spawned >= settings().waveSize && sorted<Enemy>(world).empty()) { return Outcome::Won; }
    return Outcome::Playing;
}
```

## Test the rules right now

Here is the payoff of having no network in this step. In `tests/rtype/rtype_test.cpp`:

```cpp
Test(rtype_rules, a_bullet_kills)
{
    shortGames();
    settings().enemyHealth = 2;
    settings().enemySpeed = 0.0f;                  // a target that stays where it is
    Arena arena;                                   // a World with the arena and a Match
    const kw::Entity ship = arena.ship({100.0f, 100.0f}, 1);
    const kw::Entity enemy = arena.enemy({200.0f, 100.0f});

    fire(arena.world, ship);
    ...
    for (int t = 0; t < 60; ++t) {
        stepRules(arena.world, TICK_DT);
        fire(arena.world, ship);
    }
    Assert(!arena.world.has<Enemy>(enemy), "the enemy that was in the line of fire is dead");
    AssertEq(arena.world.get<Score>(ship).points, 1, "and the shooter has the point");
}
```

And the determinism test: play the same seeded game twice with the same inputs and compare a trace of every
tick. If a rule ever depends on the order the ECS stores entities, this test tells you.

There is no server, no client and no sleep in these tests: they run in a few milliseconds.

Next: [Step 5: The room](../05-the-room/README.md).
