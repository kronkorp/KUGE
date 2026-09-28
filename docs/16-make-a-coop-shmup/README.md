# Make a co-op shmup with KUGE, from A to Z

This guide builds **Starfall**, a vertical co-op shoot'em up for one to four pilots, with KUGE. The result is three
programs: a dedicated server with no window, a client, and a host (a player whose game is also the server, which
friends join). It starts from an empty folder and ends with tests.

It is the second game tutorial of these docs. [Make R-Type](../11-make-rtype/README.md) introduces the modules one
step at a time; this guide goes faster over what R-Type explains, and further into the game:

- enemy bullets and a timeline of waves;
- a boss whose health scales with the team;
- power-ups, and lives shared by the team, with downed pilots who come back;
- a HUD, and a result that every screen shows before the next game starts.

It also steers around the traps of the engine as it is today (they are listed in step 10).

**Everything here was compiled and run.** It was built with GCC 13 against KUGE at commit `edfcaab`. The seven
tests of step 8 pass, a client plays against the dedicated server over real sockets, and the host starts and stops
cleanly. Every file is given whole: create them in order and it builds.

| Step | |
|---|---|
| [0 The game and the plan](#step-0-the-game-and-the-plan) | What we build, the five rules, the files |
| [1 The project](#step-1-the-project) | CMake: three libraries, three programs |
| [2 The shared vocabulary](#step-2-the-shared-vocabulary) | The input, what is replicated, how a ship moves |
| [3 The rules](#step-3-the-rules) | The whole game, with no network and no screen |
| [4 The room](#step-4-the-room) | The server side of one game, and the dedicated server |
| [5 The client](#step-5-the-client) | Joining, prediction, drawing, the HUD, the next game |
| [6 The host](#step-6-the-host) | Server and client in one process, for friends to join |
| [7 Build and play](#step-7-build-and-play) | Commands, ports, a server-only build |
| [8 Tests](#step-8-tests) | The rules alone, and two pilots against a real server |
| [9 Making it feel right](#step-9-making-it-feel-right) | What is special about the netcode of a shmup |
| [10 Traps](#step-10-traps) | What the engine does today that could bite, and how Starfall avoids it |
| [Where to go next](#where-to-go-next) | Ideas, and where each one fits |

## Step 0: The game and the plan

| | |
|---|---|
| Players | 1 to 4, co-op. A pilot can join a game in progress. |
| Controls | Move: arrows, WASD or the left stick. Fire: Space or pad A. Focus (move slowly and precisely): Left Shift or pad B. Esc quits. |
| Enemies | Drones fly straight down. Weavers wave. Turrets stop and aim at the nearest pilot. Then a boss fires rings of bullets, faster once it is half dead. |
| Your gun | Three levels. A power-up drops every 7 kills; a hit costs a level. |
| Lives | The team shares 4. A hit pilot is down for 1.5 s, comes back for one of the team's lives, and is shielded for 2 s. |
| The end | The boss dies (won), or every pilot is down with no life left (lost). The result stays on every screen for 2.5 s, then everyone is put in a new game. |

```
          CLIENT (a window)                                        SERVER (no window)

 keys --> PilotInput ------ inputs, 60/s, 4 at a time ------------> InputServer --> steerShip, fire
             |                                                             |
             | Prediction: steerShip + physics                             | rules: timeline, enemies,
             | on a private world, at once                                 | hits, lives, the result
             v                                                             v
      your ship, drawn now <------ snapshots, 30/s ----------------- ReplicationServer: what changed
      the rest, 0.1 s in the past                                     since what you acknowledged
      (interpolated)
```

Five rules make the whole design:

1. **The server decides everything.** A client only says what its pilot *wants*: a direction, fire, focus
   (`PilotInput`). It never sends a position, and never decides a hit.
2. **One shared file.** Everything both sides must agree on lives in `common/Starfall.hpp`: the input, the
   replicated components and their order, the sizes, and how a ship moves.
3. **The rules are plain functions on a World** (`common/Rules.cpp`). The room calls them; the tests call them
   without a network.
4. **Only your own ship's movement is predicted**, with the very functions the room uses. Everything else is drawn
   as the server says, a little in the past.
5. **Never keep the number of an entity whose end you do not decide.** KUGE's ECS gives the number of a removed
   entity to the next entity it makes, so a kept number can end up naming a stranger. Starfall never removes a ship
   while its pilot is in the room: a hit pilot is *down*, not destroyed. Everything else is looked up again at each
   tick.

The files:

```
example/starfall/
  CMakeLists.txt
  common/   Starfall.hpp (what both sides agree on), Rules.hpp, Rules.cpp (the game itself)
  server/   StarfallRoom.hpp, StarfallRoom.cpp (one game on the server), main.cpp (the dedicated server)
  client/   StarfallScene.hpp (the client scene), main.cpp (the client)
  host/     main.cpp (server and client in one process)
tests/starfall/
  starfall_test.cpp
```

## Step 1: The project

For now a game lives in `example/`: there is no `find_package(kuge)` yet.

`example/starfall/CMakeLists.txt`:

```cmake
# Starfall: a co-op shoot'em up, as a dedicated server, a client, and a host (both in one process).
# The three programs share common/, and none of them changes the engine.

# What both sides agree on, and the rules
add_library(starfall-common STATIC common/Rules.cpp)
target_include_directories(starfall-common PUBLIC common)
target_link_libraries(starfall-common PUBLIC kuge-replication)
target_compile_options(starfall-common PRIVATE -Wall -Wextra)

# A room: all that a server needs
add_library(starfall-room STATIC server/StarfallRoom.cpp)
target_include_directories(starfall-room PUBLIC server)
target_link_libraries(starfall-room PUBLIC starfall-common kuge-server)
target_compile_options(starfall-room PRIVATE -Wall -Wextra)

add_executable(starfall_server server/main.cpp)
target_link_libraries(starfall_server PRIVATE starfall-room)
target_compile_options(starfall_server PRIVATE -Wall -Wextra)

if(TARGET kuge-client)
    # The client scene is a header: the client and the host include it
    add_library(starfall-client INTERFACE)
    target_include_directories(starfall-client INTERFACE client)
    target_link_libraries(starfall-client INTERFACE starfall-common kuge-client)

    add_executable(starfall_client client/main.cpp)
    target_link_libraries(starfall_client PRIVATE starfall-client)
    target_compile_options(starfall_client PRIVATE -Wall -Wextra)

    add_executable(starfall_host host/main.cpp)
    target_link_libraries(starfall_host PRIVATE starfall-client starfall-room)
    target_compile_options(starfall_host PRIVATE -Wall -Wextra)
endif()
```

Then, in `example/CMakeLists.txt`, next to R-Type:

```cmake
# Starfall: a co-op shoot'em up, as a server, a client and a host
if(TARGET kuge-server AND TARGET kuge-replication)
    add_subdirectory(starfall)
endif()
```

What links what:

- `starfall-common` links `kuge-replication`, which brings the network, the physics and the core with it. It has
  no SDL and no server code.
- `starfall-room` adds `kuge-server`. **That is all the dedicated server links.** A configuration with
  `-DKUGE_BUILD_CLIENT=OFF` builds it on a machine with no SDL, and the client programs simply disappear
  (`if(TARGET kuge-client)`).
- Only the host links both sides.

## Step 2: The shared vocabulary

`example/starfall/common/Starfall.hpp`:

```cpp
#pragma once

// Starfall: a vertical co-op shoot'em up, for one to four pilots.
//
// What the server and the client share, and nothing else: the sizes, the input, the components that
// travel, what is replicated, and how a ship moves (the code the client predicts with).

#include "CommonComponents.hpp"
#include "Physics2D.hpp"
#include "PhysicsComponents.hpp"
#include "Replication.hpp"
#include "Transform2D.hpp"
#include "Wire.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>

namespace starfall
{

    using kuge::replication::EntityType;

    // -- The playfield --------------------------------------------------------------------------------
    constexpr float         ARENA_W   = 360.0f;
    constexpr float         ARENA_H   = 480.0f;
    constexpr std::uint32_t TICK_RATE = 60;
    constexpr double        TICK_DT   = 1.0 / TICK_RATE;

    constexpr float      SHIP_SPEED  = 170.0f;         // pixels per second
    constexpr float      FOCUS_SPEED = 75.0f;          // while Focus is held: slow and precise
    constexpr kuge::Vec2 SHIP_SIZE{14.0f, 16.0f};      // the picture, and the box the walls stop
    constexpr kuge::Vec2 SHIP_CORE{4.0f, 4.0f};        // what an enemy bullet must touch: small, so it is fair

    // -- The kinds of entities the clients hear about ------------------------------------------------
    constexpr EntityType SHIP       = 1;
    constexpr EntityType SHOT       = 2;    // a pilot's bullet
    constexpr EntityType DRONE      = 3;
    constexpr EntityType WEAVER     = 4;
    constexpr EntityType TURRET     = 5;
    constexpr EntityType BOSS       = 6;
    constexpr EntityType ENEMY_SHOT = 7;
    constexpr EntityType POWERUP    = 8;
    constexpr EntityType TEAM       = 9;    // no picture: the lives and the score of the team

    //! How big each kind is: the room collides with this box, the client draws it. One table, no lie.
    constexpr kuge::Vec2 sizeOf(EntityType type)
    {
        switch (type) {
            case SHIP:       return SHIP_SIZE;
            case SHOT:       return {4.0f, 10.0f};
            case DRONE:      return {12.0f, 12.0f};
            case WEAVER:     return {14.0f, 12.0f};
            case TURRET:     return {20.0f, 20.0f};
            case BOSS:       return {70.0f, 40.0f};
            case ENEMY_SHOT: return {6.0f, 6.0f};
            case POWERUP:    return {12.0f, 12.0f};
            default:         return {0.0f, 0.0f};
        }
    }

    // -- What a pilot does: the only thing a client ever sends -----------------------------------------
    struct PilotInput
    {
        KUGE_MESSAGE(PilotInput, dx, dy, fire, focus)
        std::int8_t dx = 0;       // -1, 0 or 1
        std::int8_t dy = 0;
        bool        fire = false;
        bool        focus = false;
    };

    // -- Components that travel ------------------------------------------------------------------------
    struct PilotState
    {
        enum Mode : std::uint8_t { Flying, Down, Shielded };
        std::uint8_t mode = Flying;
    };
    struct Power     { std::uint8_t level = 1; };                  // 1 to 3
    struct Score     { std::uint32_t points = 0; };
    struct Slot      { std::uint8_t index = 0; };                  // 0 to 3: the colour of a pilot
    struct Health    { std::int32_t points = 1; std::int32_t max = 1; };

    enum class Outcome : std::uint8_t { Playing, Won, Lost };

    struct TeamState
    {
        std::uint8_t  lives = 0;
        std::uint32_t score = 0;
        Outcome       result = Outcome::Playing;
    };

    //! The list of what is replicated, and how. The room and the clients both call it: same list, same order.
    inline kuge::replication::ReplicationRegistry makeRegistry(void)
    {
        using namespace kuge::replication;
        ReplicationRegistry registry;

        registerTransform2D(registry, Replicate::Interpolated, /*predicted*/ true);
        registerBody(registry, Replicate::OnChange, /*predicted*/ true);
        registry.component<PilotState>("PilotState", Replicate::OnChange,
            [](kuge::ByteWriter& out, const PilotState& s) { out.write(s.mode); },
            [](kuge::ByteReader& in) { return PilotState{in.read<std::uint8_t>()}; },
            {}, /*predicted*/ true);
        registry.component<Power>("Power", Replicate::OnChange,
            [](kuge::ByteWriter& out, const Power& p) { out.write(p.level); },
            [](kuge::ByteReader& in) { return Power{in.read<std::uint8_t>()}; });
        registry.component<Score>("Score", Replicate::OnChange,
            [](kuge::ByteWriter& out, const Score& s) { out.write(s.points); },
            [](kuge::ByteReader& in) { return Score{in.read<std::uint32_t>()}; });
        registry.component<Health>("Health", Replicate::OnChange,
            [](kuge::ByteWriter& out, const Health& h) { out.write(h.points); out.write(h.max); },
            [](kuge::ByteReader& in) {
                Health h;

                h.points = in.read<std::int32_t>();
                h.max = in.read<std::int32_t>();
                return h;
            });
        registry.component<Slot>("Slot", Replicate::OnSpawn,
            [](kuge::ByteWriter& out, const Slot& s) { out.write(s.index); },
            [](kuge::ByteReader& in) { return Slot{in.read<std::uint8_t>()}; });
        registry.component<TeamState>("TeamState", Replicate::OnChange,
            [](kuge::ByteWriter& out, const TeamState& t) { out.write(t.lives); out.write(t.score); out.write(t.result); },
            [](kuge::ByteReader& in) {
                TeamState t;

                t.lives = in.read<std::uint8_t>();
                t.score = in.read<std::uint32_t>();
                t.result = in.read<Outcome>();
                return t;
            });
        return registry;
    }

    // -- The arena and the ships: what the room simulates, and what a client predicts ------------------
    inline void buildArena(kw::World& world)
    {
        world.addResource<kuge::Physics2D>(kuge::PhysicsConfig{.gravity = {0.0f, 0.0f}});
        const auto wall = [&world](float x, float y, float w, float h) {
            const kw::Entity e = world.create();

            world.add<kuge::Transform2D>(e, kuge::Transform2D{{x, y}});
            world.add<kuge::Collider>(e, kuge::Collider::box(w, h));
        };

        wall(ARENA_W / 2, -10.0f, ARENA_W + 40.0f, 20.0f);              // top
        wall(ARENA_W / 2, ARENA_H + 10.0f, ARENA_W + 40.0f, 20.0f);     // bottom
        wall(-10.0f, ARENA_H / 2, 20.0f, ARENA_H + 40.0f);              // left
        wall(ARENA_W + 10.0f, ARENA_H / 2, 20.0f, ARENA_H + 40.0f);     // right
    }

    inline kw::Entity buildShip(kw::World& world, kuge::Vec2 at = {ARENA_W / 2, ARENA_H - 40.0f})
    {
        const kw::Entity ship = world.create();
        kuge::Body body;

        body.type = kuge::Body::Type::Kinematic;
        world.add<kuge::Transform2D>(ship, kuge::Transform2D{at});
        world.add<kuge::Body>(ship, body);
        world.add<kuge::Collider>(ship, kuge::Collider::box(SHIP_SIZE.x, SHIP_SIZE.y));
        world.add<PilotState>(ship, PilotState{});
        return ship;
    }

    //! An input into a ship. The client predicts with it, the room applies it: the same function.
    inline void steerShip(kw::World& world, kw::Entity ship, const PilotInput& input)
    {
        auto& body = world.get<kuge::Body>(ship);

        if (world.get<PilotState>(ship).mode == PilotState::Down) {
            body.velocity = {};
            return;
        }
        // Never trust a client: a direction is -1, 0 or 1, whatever it sent
        const auto dx = static_cast<float>(std::clamp<int>(input.dx, -1, 1));
        const auto dy = static_cast<float>(std::clamp<int>(input.dy, -1, 1));
        const float diagonal = (dx != 0.0f && dy != 0.0f) ? 0.70710678f : 1.0f;   // not faster on a diagonal

        body.velocity = kuge::Vec2{dx, dy} * (diagonal * (input.focus ? FOCUS_SPEED : SHIP_SPEED));
    }

    inline void stepArena(kw::World& world, double dt)
    {
        world.getResource<kuge::Physics2D>().step(world, static_cast<float>(dt));
    }

    //! A system made of a function (both sides add a few)
    class Run final : public kw::ISystem
    {
        public:
            explicit Run(std::function<void(kw::World&)> work) : m_work(std::move(work)) {}
            bool handle(kw::World& world) override { m_work(world); return true; }

        private:
            std::function<void(kw::World&)> m_work;
    };

}
```

**The input.** `PilotInput` is 4 bytes. `KUGE_MESSAGE` makes it a network message. It is sent 60 times a second, in
packets that carry the last 4 inputs (so a lost packet costs nothing), so keep it small. `steerShip` clamps `dx` and
`dy`: a modified client could send `dx = 100`, and the server accepts nothing beyond what the rules allow.

**What travels.** Read `makeRegistry` as a table:

| Component | Mode | Predicted | Why |
|---|---|---|---|
| `Transform2D` | `Interpolated` | yes | Where things are. Drawn between two snapshots; your own ship is predicted. |
| `Body` | `OnChange` | yes | The velocity your prediction starts from. |
| `PilotState` | `OnChange` | yes | Flying, down or shielded. Predicted because `steerShip` reads it: a downed ship does not move, on the server *and* in your prediction. |
| `Power`, `Score` | `OnChange` | | For the HUD. |
| `Health` | `OnChange` | | The boss's bar (and any hit flash you add). |
| `Slot` | `OnSpawn` | | A pilot's colour, told once. |
| `TeamState` | `OnChange` | | Lives, score and result, on an entity of its own (`TEAM`) that has no picture. |

`TeamState` is the way to replicate **global state**. The replication sends entities, so the team's lives, score and
result live on an entity of their own, and every client gets them like anything else.

The order of registration is each component's number on the wire. A hash of the whole list travels with every
snapshot, so a client whose list differs ignores that server instead of misreading it. That is why one function
builds the list, and both sides call it.

**Sizes.** `sizeOf()` is a single table: the room uses it to collide, the client to draw. What you see is what can
hit you.

**The arena and the ship.** Four walls, and a kinematic body with no gravity. The physics stops the ship at the
walls with the same code on the server and in the prediction.

## Step 3: The rules

`example/starfall/common/Rules.hpp`:

```cpp
#pragma once

// The rules of Starfall: plain functions on a World. The room calls them; the tests call them with no
// network at all. Nothing here knows about players, connections or screens.

#include "Starfall.hpp"
#include <vector>

namespace starfall
{

    // -- Components that only the server knows ---------------------------------------------------------
    struct Owner      { std::uint32_t player = 0; };    // the network id of the pilot a ship or a shot belongs to
    struct Gun        { std::uint32_t cooldown = 0; };
    struct Respawn    { std::uint32_t timer = 0; };     // ticks left: Down -> back, Shielded -> Flying
    struct Motion     { kuge::Vec2 velocity; };         // what the rules move (the physics only moves ships)
    struct Hitbox     { kuge::Vec2 size; };
    struct Enemy      { EntityType kind = DRONE; std::uint32_t age = 0; float x0 = 0.0f; };
    struct PlayerShot {};
    struct EnemyShot  {};
    struct Pickup     {};

    //! One line of the timeline: at tick `at`, `count` enemies of a kind come in at x, one every `every` ticks
    struct Wave
    {
        std::uint32_t at;
        EntityType    kind;
        float         x;
        std::uint32_t count = 1;
        std::uint32_t every = 20;
    };

    std::vector<Wave> defaultWaves(void);

    //! What makes a game long or hard (the tests make it short)
    struct Settings
    {
        std::vector<Wave> waves        = defaultWaves();
        std::uint32_t     bossAt       = 2100;    // ticks: the boss comes after 35 s
        std::int32_t      bossHealth   = 90;      // per pilot in the room
        std::uint8_t      teamLives    = 4;       // shared by the whole team
        std::uint32_t     fireCooldown = 6;       // ticks between two volleys
        std::uint32_t     downTicks    = 90;      // a downed pilot comes back after 1.5 s...
        std::uint32_t     shieldTicks  = 120;     // ...and cannot be hit for 2 s
        std::uint32_t     dropEvery    = 7;       // a power-up every 7 kills
        std::uint32_t     endDelay     = 150;     // ticks the result stays on screen before the room closes
    };

    Settings& settings(void);

    //! A resource of the room's World
    struct Match
    {
        std::uint32_t tick = 0;
        std::uint32_t kills = 0;
        bool          bossCame = false;
        bool          bossDown = false;
        //! Called for each entity the rules make, so that the room replicates it
        std::function<void(kw::Entity, EntityType, std::uint32_t owner)> track;
    };

    //! The Match resource must be there. Makes the team (lives, score), which is replicated too.
    void       startMatch(kw::World& world);
    //! A pilot's ship. It is never removed while its pilot is in the room: removePilot() is the only way.
    kw::Entity addPilot(kw::World& world, std::uint32_t networkId, std::uint8_t slot);
    void       removePilot(kw::World& world, kw::Entity ship);
    //! Fires a volley, if the gun is ready and the pilot is not down
    void       fire(kw::World& world, kw::Entity ship);
    kw::Entity spawnEnemy(kw::World& world, EntityType kind, kuge::Vec2 at);
    //! One tick of the rules: timers, the timeline, movement, shooting, hits, pick-ups, the result
    void       stepRules(kw::World& world);
    Outcome    outcome(kw::World& world);

    std::vector<kw::Entity> ships(kw::World& world);   // by increasing entity
    TeamState&              team(kw::World& world);

}
```

`example/starfall/common/Rules.cpp`:

```cpp
#include "Rules.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace starfall
{

    namespace
    {
        constexpr float DT = static_cast<float>(TICK_DT);

        struct Profile
        {
            std::int32_t  health;
            std::uint32_t score;
        };

        Profile profileOf(EntityType kind)
        {
            switch (kind) {
                case WEAVER: return {2, 20};
                case TURRET: return {8, 60};
                case BOSS:   return {settings().bossHealth, 1000};
                default:     return {1, 10};
            }
        }

        // The ECS visits entities in an order that changes as they come and go: anything that must be
        // the same on every run walks them sorted
        template<typename C>
        std::vector<kw::Entity> sorted(kw::World& world)
        {
            std::vector<kw::Entity> entities;
            auto view = world.view<C>();

            for (kw::Entity e : view) {
                entities.push_back(e);
            }
            std::sort(entities.begin(), entities.end());
            return entities;
        }

        void track(kw::World& world, kw::Entity e, EntityType type, std::uint32_t owner)
        {
            const Match& match = world.getResource<Match>();

            if (match.track) {
                match.track(e, type, owner);
            }
        }

        kuge::Vec2& position(kw::World& world, kw::Entity e) { return world.get<kuge::Transform2D>(e).position; }

        bool overlap(kuge::Vec2 a, kuge::Vec2 sizeA, kuge::Vec2 b, kuge::Vec2 sizeB)
        {
            return std::fabs(a.x - b.x) * 2.0f < sizeA.x + sizeB.x && std::fabs(a.y - b.y) * 2.0f < sizeA.y + sizeB.y;
        }

        kuge::Vec2 spawnPoint(std::uint8_t slot)
        {
            return {ARENA_W * static_cast<float>(slot % 4 + 1) / 5.0f, ARENA_H - 40.0f};
        }

        // Something the rules move in a straight line (shots, power-ups)
        kw::Entity projectile(kw::World& world, EntityType type, kuge::Vec2 at, kuge::Vec2 velocity)
        {
            const kw::Entity e = world.create();

            world.add<kuge::Transform2D>(e, kuge::Transform2D{at});
            world.add<Motion>(e, Motion{velocity});
            world.add<Hitbox>(e, Hitbox{sizeOf(type)});
            return e;
        }

        void enemyShot(kw::World& world, kuge::Vec2 from, kuge::Vec2 velocity)
        {
            const kw::Entity e = projectile(world, ENEMY_SHOT, from, velocity);

            world.add<EnemyShot>(e, EnemyShot{});
            track(world, e, ENEMY_SHOT, 0);
        }

        // Towards the nearest pilot that is not down (straight down if there is none)
        kuge::Vec2 aimFrom(kw::World& world, kuge::Vec2 from)
        {
            kuge::Vec2 target{from.x, ARENA_H};
            float best = -1.0f;

            for (kw::Entity ship : ships(world)) {
                if (world.get<PilotState>(ship).mode == PilotState::Down) {
                    continue;
                }
                const float distance = (position(world, ship) - from).lengthSquared();

                if (best < 0.0f || distance < best) {
                    best = distance;
                    target = position(world, ship);
                }
            }
            return (target - from).normalized();
        }

        void moveEnemy(kw::World& world, kw::Entity e)
        {
            Enemy& enemy = world.get<Enemy>(e);
            kuge::Vec2& at = position(world, e);
            const float age = static_cast<float>(enemy.age++);

            switch (enemy.kind) {
                case DRONE:
                    at.y += 80.0f * DT;
                    break;
                case WEAVER:
                    at.y += 55.0f * DT;
                    at.x = enemy.x0 + 50.0f * std::sin(age * 0.05f);
                    break;
                case TURRET:
                    // Comes down, stays a while and shoots at the pilots, then leaves
                    if (enemy.age < 110 || enemy.age > 480) {
                        at.y += 60.0f * DT;
                    } else if (enemy.age % 70 == 0) {
                        enemyShot(world, at, aimFrom(world, at) * 120.0f);
                    }
                    break;
                case BOSS: {
                    const Health& health = world.get<Health>(e);

                    if (enemy.age < 165) {
                        at.y += 40.0f * DT;     // comes in
                        break;
                    }
                    at.x = ARENA_W / 2 + 110.0f * std::sin((age - 165.0f) * 0.012f);
                    // Rings of bullets, faster once it is half dead
                    const std::uint32_t every = health.points * 2 > health.max ? 50 : 28;

                    if (enemy.age % every == 0) {
                        for (int i = 0; i < 14; ++i) {
                            const float angle = age * 0.07f + static_cast<float>(i) * (6.2831853f / 14.0f);

                            enemyShot(world, at, kuge::Vec2{std::cos(angle), std::sin(angle)} * 95.0f);
                        }
                    }
                    break;
                }
                default:
                    break;
            }
        }

        void knockDown(kw::World& world, kw::Entity ship)
        {
            Power& power = world.get<Power>(ship);

            world.get<PilotState>(ship).mode = PilotState::Down;
            world.get<Respawn>(ship).timer = settings().downTicks;
            world.get<kuge::Body>(ship).velocity = {};
            power.level = static_cast<std::uint8_t>(std::max(1, power.level - 1));   // a hit costs a level
        }

        void award(kw::World& world, std::uint32_t owner, std::uint32_t points)
        {
            team(world).score += points;
            for (kw::Entity ship : ships(world)) {
                if (world.get<Owner>(ship).player == owner) {
                    world.get<Score>(ship).points += points;
                }
            }
        }

        void damage(kw::World& world, kw::Entity enemy, std::uint32_t owner)
        {
            Match& match = world.getResource<Match>();
            Health& health = world.get<Health>(enemy);

            if (--health.points > 0) {
                return;
            }
            const EntityType kind = world.get<Enemy>(enemy).kind;
            const kuge::Vec2 at = position(world, enemy);

            world.remove(enemy);
            ++match.kills;
            award(world, owner, profileOf(kind).score);
            if (kind == BOSS) {
                match.bossDown = true;
            } else if (match.kills % settings().dropEvery == 0) {
                const kw::Entity pickup = projectile(world, POWERUP, at, {0.0f, 45.0f});

                world.add<Pickup>(pickup, Pickup{});
                track(world, pickup, POWERUP, 0);
            }
        }

        void updatePilots(kw::World& world)
        {
            for (kw::Entity ship : ships(world)) {
                Gun& gun = world.get<Gun>(ship);
                PilotState& state = world.get<PilotState>(ship);
                Respawn& respawn = world.get<Respawn>(ship);

                gun.cooldown -= gun.cooldown > 0 ? 1 : 0;
                if (respawn.timer == 0 || --respawn.timer > 0) {
                    continue;
                }
                if (state.mode == PilotState::Shielded) {
                    state.mode = PilotState::Flying;
                } else if (state.mode == PilotState::Down && team(world).lives > 0) {
                    // Back in the game, at the bottom, for one of the team's lives
                    --team(world).lives;
                    state.mode = PilotState::Shielded;
                    respawn.timer = settings().shieldTicks;
                    position(world, ship) = spawnPoint(world.get<Slot>(ship).index);
                }
                // (Down with no life left: out of this game, the timer stays at 0)
            }
        }

        void followTimeline(kw::World& world)
        {
            Match& match = world.getResource<Match>();

            for (const Wave& wave : settings().waves) {
                const std::uint32_t every = std::max<std::uint32_t>(wave.every, 1);

                if (match.tick < wave.at) {
                    continue;
                }
                const std::uint32_t since = match.tick - wave.at;

                if (since % every == 0 && since / every < wave.count) {
                    spawnEnemy(world, wave.kind, {wave.x, -20.0f});
                }
            }
            if (!match.bossCame && match.tick >= settings().bossAt) {
                match.bossCame = true;
                spawnEnemy(world, BOSS, {ARENA_W / 2, -30.0f});
            }
        }

        void moveEverything(kw::World& world)
        {
            for (kw::Entity e : sorted<Motion>(world)) {
                kuge::Vec2& at = position(world, e);

                at += world.get<Motion>(e).velocity * DT;
                if (at.x < -30.0f || at.x > ARENA_W + 30.0f || at.y < -30.0f || at.y > ARENA_H + 30.0f) {
                    world.remove(e);
                }
            }
            for (kw::Entity e : sorted<Enemy>(world)) {
                moveEnemy(world, e);
                if (position(world, e).y > ARENA_H + 40.0f) {
                    world.remove(e);    // it got away
                }
            }
        }

        void resolveHits(kw::World& world)
        {
            // NOTE: kronkworld gives the number of a removed entity to the next one it makes. A list taken
            // before a removal may name something else afterwards: check what it is before using it.

            // Pilots' shots hit enemies
            const auto enemies = sorted<Enemy>(world);

            for (kw::Entity shot : sorted<PlayerShot>(world)) {
                if (!world.has<PlayerShot>(shot)) {
                    continue;
                }
                for (kw::Entity enemy : enemies) {
                    if (!world.has<Enemy>(enemy)
                        || !overlap(position(world, shot), world.get<Hitbox>(shot).size, position(world, enemy), world.get<Hitbox>(enemy).size)) {
                        continue;
                    }
                    const std::uint32_t owner = world.get<Owner>(shot).player;

                    world.remove(shot);
                    damage(world, enemy, owner);
                    break;
                }
            }
            // Enemy bullets and enemies hit the pilots that fly (a shield protects, a downed pilot is not there)
            const auto bullets = sorted<EnemyShot>(world);

            for (kw::Entity ship : ships(world)) {
                if (world.get<PilotState>(ship).mode != PilotState::Flying) {
                    continue;
                }
                const kuge::Vec2 at = position(world, ship);
                bool hit = false;

                for (kw::Entity bullet : bullets) {
                    if (world.has<EnemyShot>(bullet) && overlap(at, SHIP_CORE, position(world, bullet), world.get<Hitbox>(bullet).size)) {
                        world.remove(bullet);
                        hit = true;
                        break;
                    }
                }
                for (kw::Entity enemy : sorted<Enemy>(world)) {
                    if (hit) {
                        break;
                    }
                    if (overlap(at, SHIP_CORE, position(world, enemy), world.get<Hitbox>(enemy).size)) {
                        const EntityType kind = world.get<Enemy>(enemy).kind;

                        if (kind == DRONE || kind == WEAVER) {
                            world.remove(enemy);    // small ones break on the ship
                        }
                        hit = true;
                    }
                }
                if (hit) {
                    knockDown(world, ship);
                }
            }
            // Power-ups
            for (kw::Entity pickup : sorted<Pickup>(world)) {
                for (kw::Entity ship : ships(world)) {
                    if (world.get<PilotState>(ship).mode == PilotState::Down
                        || !overlap(position(world, ship), SHIP_SIZE, position(world, pickup), world.get<Hitbox>(pickup).size)) {
                        continue;
                    }
                    Power& power = world.get<Power>(ship);

                    power.level = static_cast<std::uint8_t>(std::min(3, power.level + 1));
                    award(world, world.get<Owner>(ship).player, 50);
                    world.remove(pickup);
                    break;
                }
            }
        }
    }

    std::vector<Wave> defaultWaves(void)
    {
        return {
            {60, DRONE, 90.0f, 5, 18},      {60, DRONE, 270.0f, 5, 18},
            {300, WEAVER, 180.0f, 6, 25},
            {520, DRONE, 60.0f, 6, 15},     {520, DRONE, 300.0f, 6, 15},
            {760, TURRET, 100.0f},          {760, TURRET, 260.0f},
            {900, WEAVER, 120.0f, 6, 20},   {900, WEAVER, 240.0f, 6, 20},
            {1200, DRONE, 180.0f, 10, 12},
            {1400, TURRET, 180.0f},
            {1450, WEAVER, 80.0f, 5, 20},   {1450, WEAVER, 280.0f, 5, 20},
            {1700, DRONE, 60.0f, 8, 14},    {1700, DRONE, 180.0f, 8, 14},   {1700, DRONE, 300.0f, 8, 14},
        };
    }

    Settings& settings(void)
    {
        static Settings value;

        return value;
    }

    std::vector<kw::Entity> ships(kw::World& world)
    {
        return sorted<Gun>(world);
    }

    TeamState& team(kw::World& world)
    {
        auto view = world.view<TeamState>();

        for (kw::Entity e : view) {
            return world.get<TeamState>(e);
        }
        throw std::logic_error("starfall: no team (call startMatch() first)");
    }

    void startMatch(kw::World& world)
    {
        const kw::Entity e = world.create();

        world.add<TeamState>(e, TeamState{settings().teamLives, 0, Outcome::Playing});
        track(world, e, TEAM, 0);
    }

    kw::Entity addPilot(kw::World& world, std::uint32_t networkId, std::uint8_t slot)
    {
        const kw::Entity ship = buildShip(world, spawnPoint(slot));

        world.get<PilotState>(ship).mode = PilotState::Shielded;
        world.add<Respawn>(ship, Respawn{settings().shieldTicks});
        world.add<Owner>(ship, Owner{networkId});
        world.add<Gun>(ship, Gun{});
        world.add<Power>(ship, Power{});
        world.add<Score>(ship, Score{});
        world.add<Slot>(ship, Slot{slot});
        track(world, ship, SHIP, networkId);
        return ship;
    }

    void removePilot(kw::World& world, kw::Entity ship)
    {
        world.remove(ship);
    }

    void fire(kw::World& world, kw::Entity ship)
    {
        Gun& gun = world.get<Gun>(ship);

        if (gun.cooldown > 0 || world.get<PilotState>(ship).mode == PilotState::Down) {
            return;
        }
        gun.cooldown = settings().fireCooldown;
        const kuge::Vec2 at = position(world, ship);
        const std::uint32_t owner = world.get<Owner>(ship).player;
        const auto shot = [&](kuge::Vec2 offset, kuge::Vec2 velocity) {
            const kw::Entity e = projectile(world, SHOT, at + offset, velocity);

            world.add<PlayerShot>(e, PlayerShot{});
            world.add<Owner>(e, Owner{owner});
            track(world, e, SHOT, owner);
        };

        switch (world.get<Power>(ship).level) {
            case 1:
                shot({0.0f, -10.0f}, {0.0f, -480.0f});
                break;
            case 2:
                shot({-5.0f, -8.0f}, {0.0f, -480.0f});
                shot({5.0f, -8.0f}, {0.0f, -480.0f});
                break;
            default:
                shot({-5.0f, -8.0f}, {0.0f, -480.0f});
                shot({5.0f, -8.0f}, {0.0f, -480.0f});
                shot({-7.0f, -4.0f}, {-110.0f, -450.0f});
                shot({7.0f, -4.0f}, {110.0f, -450.0f});
                break;
        }
    }

    kw::Entity spawnEnemy(kw::World& world, EntityType kind, kuge::Vec2 at)
    {
        const kw::Entity e = world.create();
        std::int32_t health = profileOf(kind).health;

        if (kind == BOSS) {
            health *= static_cast<std::int32_t>(std::max<std::size_t>(1, ships(world).size()));   // co-op: harder with more pilots
        }
        world.add<kuge::Transform2D>(e, kuge::Transform2D{at});
        world.add<Enemy>(e, Enemy{kind, 0, at.x});
        world.add<Health>(e, Health{health, health});
        world.add<Hitbox>(e, Hitbox{sizeOf(kind)});
        track(world, e, kind, 0);
        return e;
    }

    void stepRules(kw::World& world)
    {
        ++world.getResource<Match>().tick;
        updatePilots(world);
        followTimeline(world);
        moveEverything(world);
        resolveHits(world);
        team(world).result = outcome(world);
    }

    Outcome outcome(kw::World& world)
    {
        if (world.getResource<Match>().bossDown) {
            return Outcome::Won;
        }
        const auto all = ships(world);

        if (all.empty()) {
            return Outcome::Playing;    // nobody here (yet): the room decides what to do
        }
        for (kw::Entity ship : all) {
            if (world.get<PilotState>(ship).mode != PilotState::Down || world.get<Respawn>(ship).timer > 0) {
                return Outcome::Playing;
            }
        }
        return Outcome::Lost;           // everyone is down for good: no life left
    }

}
```

What to notice:

- **Server-only components** (`Owner`, `Gun`, `Respawn`, `Motion`, `Hitbox`, `Enemy`...) are not in the registry,
  so clients never learn them. That means less bandwidth, and less for a cheater to read.
- **A hit pilot is down, not removed.** `knockDown` changes the ship's `PilotState`; `updatePilots` brings it back
  for a team life, and leaves it down for good when there is none left. The ship entity lives as long as its pilot is
  in the room. So the room can keep its number in a map (step 4), and the client's prediction never loses its entity.
  Removing the ship would break both: `Prediction` has no way to learn that its entity is gone, and would keep
  writing into whatever entity gets that number next.
- **Sorted walks.** `world.view<...>()` visits entities in storage order, which changes as entities come and go.
  Every loop that decides something walks them sorted by entity, so the same inputs give the same game (a test checks
  it).
- **Lists go stale.** `resolveHits` takes lists, then removes things while walking them. A removed number can be
  reused within the same tick: a power-up dropped by a dead enemy may get the number of the shot that killed it. So
  each entity is checked before it is used: `world.has<PlayerShot>(shot)`, `world.has<Enemy>(enemy)`.
- **The timeline is data** (`Settings::waves`), so a test can replace it with three drones. Times are in ticks: at
  60 ticks a second, `bossAt = 2100` is 35 s.
- **Co-op scaling.** The boss has `bossHealth` points for each pilot present when it arrives.
- **The result is replicated.** `stepRules` writes `outcome()` into `TeamState`, so every screen can show it (step 4
  explains why that matters).
- **No randomness is needed**: the timeline and the patterns are deterministic. If you add some, keep a seeded
  generator in `Match` (a resource of the room's World), never `std::random_device` in the rules.

## Step 4: The room

`example/starfall/server/StarfallRoom.hpp`:

```cpp
#pragma once

#include "Input.hpp"
#include "ReplicationServer.hpp"
#include "RoomScene.hpp"
#include "Rules.hpp"
#include <map>
#include <memory>

namespace starfall
{

    // One game of Starfall on the server: the inputs of the pilots, the rules, and the replication of the
    // world to the clients. Nothing in it is about a window.
    class StarfallRoom : public kuge::server::RoomScene
    {
        public:
            using RoomScene::RoomScene;

        protected:
            void onRoomEnter(void) override;
            void onPlayerJoined(const Player& player) override;
            void onPlayerLeft(const Player& player, kuge::net::DisconnectReason reason) override;

        private:
            void          applyInputs(kw::World& world);
            void          afterPhysics(kw::World& world);
            std::uint8_t  freeSlot(void);

            kuge::replication::ReplicationRegistry                       m_registry;   // first: the others use it
            std::unique_ptr<kuge::replication::ReplicationServer>        m_replication;
            std::unique_ptr<kuge::replication::InputServer<PilotInput>>  m_inputs;
            std::map<kuge::net::ConnectionId, kw::Entity>                m_ships;      // valid while the pilot is here
            std::uint32_t                                                m_overFor = 0;
    };

}
```

`example/starfall/server/StarfallRoom.cpp`:

```cpp
#include "StarfallRoom.hpp"
#include "Stage.hpp"
#include "Time.hpp"

namespace starfall
{

    void StarfallRoom::onRoomEnter(void)
    {
        m_registry = makeRegistry();
        buildArena(world());
        world().addResource<Match>();
        m_replication = std::make_unique<kuge::replication::ReplicationServer>(world(), m_registry, endpoint());
        // Two inputs of margin: the clocks of a client and of the room never quite agree
        m_inputs = std::make_unique<kuge::replication::InputServer<PilotInput>>(endpoint(), kuge::replication::InputServerConfig{.jitter = 2});
        world().getResource<Match>().track = [this](kw::Entity e, EntityType type, std::uint32_t owner) {
            m_replication->track(e, type, owner);
        };
        startMatch(world());

        addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Run>([this](kw::World& w) { applyInputs(w); }));
        addSystem(kw::Schedule::Fixed, kuge::stage::Physics, std::make_unique<Run>([](kw::World& w) { stepArena(w, TICK_DT); }));
        addSystem(kw::Schedule::Fixed, kuge::stage::Late, std::make_unique<Run>([this](kw::World& w) { afterPhysics(w); }));
        addSystem(kw::Schedule::Fixed, kuge::stage::Replication, std::make_unique<Run>([this](kw::World& w) {
            // (+1: on the wire, tick 0 means "no snapshot")
            m_replication->update(static_cast<std::uint32_t>(w.getResource<kuge::Time>().tick + 1));
        }));
    }

    std::uint8_t StarfallRoom::freeSlot(void)
    {
        for (std::uint8_t slot = 0; slot < 4; ++slot) {
            bool taken = false;

            for (const auto& [connection, ship] : m_ships) {
                taken = taken || world().get<Slot>(ship).index == slot;
            }
            if (!taken) {
                return slot;
            }
        }
        return 0;
    }

    void StarfallRoom::onPlayerJoined(const Player& player)
    {
        m_ships[player.connection] = addPilot(world(), player.networkId, freeSlot());
        m_replication->addClient(player.connection);
        m_inputs->addClient(player.connection);
    }

    void StarfallRoom::onPlayerLeft(const Player& player, kuge::net::DisconnectReason)
    {
        const auto found = m_ships.find(player.connection);

        if (found != m_ships.end()) {
            removePilot(world(), found->second);   // the only place a ship is removed: m_ships never names a stranger
            m_ships.erase(found);
        }
        m_replication->removeClient(player.connection);
        m_inputs->removeClient(player.connection);
    }

    void StarfallRoom::applyInputs(kw::World& world)
    {
        for (const auto& applied : m_inputs->collect()) {
            const auto found = m_ships.find(applied.connection);

            if (found == m_ships.end()) {
                continue;
            }
            steerShip(world, found->second, applied.input);
            if (applied.input.fire && !applied.repeated) {
                fire(world, found->second);
            }
            m_replication->setInputAck(applied.connection, applied.sequence);
        }
    }

    void StarfallRoom::afterPhysics(kw::World& world)
    {
        stepRules(world);
        // The result stays on the screens a moment (it travels in TeamState), then the room closes
        if (finishing() || players().empty() || outcome(world) == Outcome::Playing) {
            m_overFor = 0;
            return;
        }
        if (++m_overFor >= settings().endDelay) {
            finish(kuge::net::RoomEnd::GameOver);
        }
    }

}
```

`example/starfall/server/main.cpp`:

```cpp
// The Starfall server: a lobby, and a room for each game. No window.
//
//     starfall_server [port]      (4242 by default; the rooms take the UDP ports after it)
//
// Ctrl+C stops it: every room is closed properly.

#include "GameServer.hpp"
#include "StarfallRoom.hpp"
#include <cstdlib>

int main(int argc, char** argv)
{
    kuge::server::ServerConfig config;

    config.tickRate = starfall::TICK_RATE;
    if (argc > 1) {
        config.lobbyPort = static_cast<std::uint16_t>(std::atoi(argv[1]));
        config.roomPortFirst = static_cast<std::uint16_t>(config.lobbyPort + 1);
    }
    kuge::server::GameServer server(config);

    server.addRoomType<starfall::StarfallRoom>("starfall", {.maxPlayers = 4, .idleTimeout = 30.0});
    return server.run();
}
```

One tick of a room, in stage order:

| Stage | What runs |
|---|---|
| `Network` | The endpoint is polled (`RoomScene` installs it): hellos, inputs, acknowledgements. |
| `Input` | The room's own housekeeping: tokens, silent connections, idle rooms. |
| `Simulation` | `applyInputs`: one input per pilot, `steerShip`, `fire`, and `setInputAck`. |
| `Physics` | `stepArena`: ships move and stop at the walls. |
| `Late` | `stepRules`, then the end of the game. |
| `Replication` | `update(tick + 1)`: a snapshot for each client that is due (every 2 ticks). |

- **`setInputAck`** tells a client the number of the last of its inputs that the state includes. Its prediction
  replays the inputs after that one.
- **`jitter = 2`**: the room waits until two inputs of a pilot are queued before it uses one. The clocks of a
  client and of the room never quite agree, and without that margin the room would keep repeating an input the
  client did not simulate.
- **`tick + 1`**: on the wire, snapshot tick 0 means "no snapshot".
- **Joining and leaving.** `onPlayerJoined` makes a ship in the lowest free colour slot, and registers the client
  with the replication and the inputs. `onPlayerLeft` is the only place a ship is removed, which is what makes
  `m_ships` safe to keep.
- **Why the room waits before `finish()`** (`endDelay`). `finish()` sends `RoomClosed` at once on the reliable
  channel, and the client drops its replication as soon as it hears it. The snapshots that carry the result would
  arrive after it and be ignored. So the room keeps playing for `endDelay` ticks with the result in `TeamState`, and
  only then closes.
- `main` is the whole dedicated server. `ServerConfig::tickRate` must be the game's `TICK_RATE`: the rules and the
  prediction both step by `TICK_DT`.

## Step 5: The client

`example/starfall/client/StarfallScene.hpp`:

```cpp
#pragma once

// The client of Starfall: a window on a game that runs in a room. It predicts its own ship, draws the
// rest between two snapshots, and shows what the team has left. It never decides anything.

#include "ClientModule.hpp"
#include "ClientScene.hpp"
#include "Engine.hpp"
#include "Logger.hpp"
#include "Matchmaking.hpp"
#include "Net.hpp"
#include "Prediction.hpp"
#include "ReplicationClient.hpp"
#include "Starfall.hpp"
#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace starfall
{

    enum class Action : std::uint8_t { Left, Right, Up, Down, Fire, Focus, Quit };

    inline void bindDefaults(kuge::InputMap& input)
    {
        using kuge::Binding;
        using kuge::GamepadAxis;
        using kuge::GamepadButton;
        using kuge::Key;

        input.declare(Action::Left, "left");
        input.declare(Action::Right, "right");
        input.declare(Action::Up, "up");
        input.declare(Action::Down, "down");
        input.declare(Action::Fire, "fire");
        input.declare(Action::Focus, "focus");
        input.declare(Action::Quit, "quit");
        input.bind(Action::Left, Key::Left);
        input.bind(Action::Left, Key::A);
        input.bind(Action::Left, Binding::axis(GamepadAxis::LeftX, -1));
        input.bind(Action::Right, Key::Right);
        input.bind(Action::Right, Key::D);
        input.bind(Action::Right, Binding::axis(GamepadAxis::LeftX, 1));
        input.bind(Action::Up, Key::Up);
        input.bind(Action::Up, Key::W);
        input.bind(Action::Up, Binding::axis(GamepadAxis::LeftY, -1));
        input.bind(Action::Down, Key::Down);
        input.bind(Action::Down, Key::S);
        input.bind(Action::Down, Binding::axis(GamepadAxis::LeftY, 1));
        input.bind(Action::Fire, Key::Space);
        input.bind(Action::Fire, GamepadButton::A);
        input.bind(Action::Focus, Key::LShift);
        input.bind(Action::Focus, GamepadButton::B);
        input.bind(Action::Quit, Key::Escape);
    }

    //! Where the server is, and who plays
    struct ClientOptions
    {
        bool                        sockets = true;           //!< false: a server of this process, by name
        std::string                 host    = "127.0.0.1";
        std::uint16_t               port    = 4242;
        std::string                 lobby   = "lobby";        //!< Loopback: the name of the lobby
        kuge::net::LoopbackNetwork* network = nullptr;        //!< Loopback: null is the process-wide one
        std::string                 name    = "pilot";
        std::string                 font;                     //!< A .ttf to write the score with (optional)
    };

    //! What the scene saw, for a test or a script to check
    struct ClientReport
    {
        bool          joined = false;
        std::uint64_t games = 0, gamesEnded = 0;
        std::uint32_t networkId = 0, roomId = 0;
        std::size_t   ships = 0, enemies = 0, shots = 0, enemyShots = 0;
        std::size_t   mostShips = 0, mostEnemies = 0, mostShots = 0;
        TeamState     team;
        std::uint8_t  mode = PilotState::Flying;   //!< Of the player's own ship
        std::uint8_t  power = 0;
        std::uint32_t score = 0;
        kuge::Vec2    shipAt{};
        kuge::replication::PredictionStats        prediction;
        kuge::replication::ReplicationClientStats replication;
    };

    class StarfallScene : public kuge::ClientScene
    {
        public:
            StarfallScene(ClientOptions options, std::shared_ptr<ClientReport> report)
                : m_options(std::move(options)), m_report(std::move(report)), m_registry(makeRegistry()) {}

            void onEnter(void) override
            {
                installClientSystems();                  // first: the keys are read before the game's systems run
                kuge::net::installNet(setup());
                makeStars();
                makeHud();
                loadFont();
                m_matchmaking = std::make_unique<kuge::net::MatchmakingClient>(world().getResource<kuge::net::Net>());
                m_matchmaking->onJoined([this](kuge::net::Endpoint& room, const kuge::net::Welcome& welcome) { joined(room, welcome); });
                m_matchmaking->onRoomClosed([this](kuge::net::RoomEnd) { roomClosed(); });
                connect();
                addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Run>([this](kw::World& w) { tick(w); }));
                addSystem(kw::Schedule::Frame, kuge::stage::Late, std::make_unique<Run>([this](kw::World& w) { frame(w); }));
                addSystem(kw::Schedule::Frame, kuge::stage::Render, std::make_unique<Run>([this](kw::World& w) { drawText(w); }));
            }

        private:
            struct Star { float speed; };

            // HUD pieces: entities of this scene only, made once and never removed, so keeping their numbers is safe
            struct Hud
            {
                std::vector<kw::Entity> lives, power;
                kw::Entity              bossBack{}, bossFill{}, banner{};
            };

            // -- Joining, playing, leaving ----------------------------------------------------------------
            void connect(void)
            {
                if (m_options.sockets) {
                    m_matchmaking->connectLobby(kuge::net::Protocol::Tcp, m_options.host, m_options.port);
                } else {
                    m_matchmaking->connectLobby(m_options.lobby, m_options.network ? *m_options.network : kuge::net::LoopbackNetwork::global());
                }
                m_matchmaking->join("starfall", m_options.name);
            }

            void joined(kuge::net::Endpoint& room, const kuge::net::Welcome& welcome)
            {
                using namespace kuge::replication;

                m_prediction.reset();
                m_replication.reset();
                clearLastGame();
                m_networkId = welcome.networkId;
                m_replication = std::make_unique<ReplicationClient>(world(), m_registry, ReplicationClientConfig{.tickRate = welcome.tickRate});
                m_replication->setLocalPlayer(welcome.networkId);
                m_replication->predictType(SHIP);            // our ship is predicted; our shots are not
                for (const EntityType type : {SHIP, SHOT, DRONE, WEAVER, TURRET, BOSS, ENEMY_SHOT, POWERUP}) {
                    m_replication->onSpawn(type, [this](kw::World&, kw::Entity e, const SpawnInfo& info) { dress(e, info); });
                }
                m_replication->attach(room);

                PredictionConfig<PilotInput> config;

                config.build = [](kw::World& w) { buildArena(w); return buildShip(w); };   // the arena and one ship
                config.apply = &steerShip;                                                  // the room's own code
                config.step = &stepArena;
                config.dt = TICK_DT;
                m_prediction = std::make_unique<Prediction<PilotInput>>(config, world(), m_registry, *m_replication, room);

                m_report->joined = true;
                m_report->networkId = welcome.networkId;
                m_report->roomId = welcome.roomId;
                ++m_report->games;
            }

            // The room is over, or lost. Whatever held its endpoint goes now; the picture stays until the next game.
            void roomClosed(void)
            {
                ++m_report->gamesEnded;
                m_prediction.reset();
                m_replication.reset();
                m_rejoinIn = TICK_RATE;       // a second, then another game
            }

            // The entities of a finished game: nobody will remove them but us
            void clearLastGame(void)
            {
                std::vector<kw::Entity> old;
                auto view = world().view<kuge::replication::Replicated>();

                for (kw::Entity e : view) {
                    old.push_back(e);
                }
                for (kw::Entity e : old) {
                    world().remove(e);
                }
                m_team = TeamState{};
            }

            // Each fixed tick: the keys become an input, predicted at once and sent to the room
            void tick(kw::World& world)
            {
                const auto& actions = world.getResource<kuge::ActionState>();
                PilotInput input;

                if (actions.isDown(Action::Quit)) {
                    ctx().engine().stop();
                }
                input.dx = static_cast<std::int8_t>(actions.isDown(Action::Right) - actions.isDown(Action::Left));
                input.dy = static_cast<std::int8_t>(actions.isDown(Action::Down) - actions.isDown(Action::Up));
                input.fire = actions.isDown(Action::Fire);
                input.focus = actions.isDown(Action::Focus);
                if (m_prediction) {
                    m_prediction->tick(input);
                }
                if (m_rejoinIn > 0 && --m_rejoinIn == 0) {
                    m_matchmaking->join("starfall", m_options.name);
                }
                // No server (yet, or any more): try again every second
                if (m_matchmaking->state() == kuge::net::MatchmakingClient::State::Failed) {
                    if (++m_waited >= TICK_RATE) {
                        m_waited = 0;
                        connect();
                    }
                } else {
                    m_waited = 0;
                }
            }

            // -- What things look like: the server never says it -------------------------------------------
            static kuge::Color pilotColor(std::uint8_t slot)
            {
                static const kuge::Color colors[4] = {{90, 200, 255, 255}, {120, 230, 110, 255}, {255, 170, 60, 255}, {230, 110, 220, 255}};

                return colors[slot % 4];
            }

            void dress(kw::Entity e, const kuge::replication::SpawnInfo& info)
            {
                kuge::Sprite sprite;

                sprite.size = sizeOf(info.type);
                switch (info.type) {
                    case SHIP:
                        sprite.tint = info.owner == m_networkId ? kuge::colors::White
                            : pilotColor(world().has<Slot>(e) ? world().get<Slot>(e).index : 0);
                        sprite.layer = 4;
                        break;
                    case SHOT:       sprite.tint = {255, 240, 150, 255}; sprite.layer = 3; break;
                    case DRONE:      sprite.tint = {230, 70, 70, 255};   sprite.layer = 2; break;
                    case WEAVER:     sprite.tint = {240, 140, 60, 255};  sprite.layer = 2; break;
                    case TURRET:     sprite.tint = {170, 90, 230, 255};  sprite.layer = 2; break;
                    case BOSS:       sprite.tint = {200, 40, 70, 255};   sprite.layer = 1; break;
                    case ENEMY_SHOT: sprite.tint = {255, 120, 200, 255}; sprite.layer = 5; break;   // on top: must be seen
                    case POWERUP:    sprite.tint = {90, 230, 120, 255};  sprite.layer = 3; break;
                    default: break;
                }
                world().add<kuge::Sprite>(e, sprite);
                if (info.type == SHIP && info.owner == m_networkId) {
                    // Our ship moves at each tick (prediction): on a fast screen, draw it between two ticks
                    world().add<kuge::PreviousTransform2D>(e, kuge::PreviousTransform2D{world().get<kuge::Transform2D>(e)});
                }
            }

            void makeStars(void)
            {
                std::mt19937 rng(7);

                for (int i = 0; i < 80; ++i) {
                    const kw::Entity star = world().create();
                    const float speed = 15.0f + static_cast<float>(rng() % 90);
                    kuge::Sprite sprite;

                    sprite.size = speed > 70.0f ? kuge::Vec2{2.0f, 2.0f} : kuge::Vec2{1.0f, 1.0f};
                    sprite.tint = kuge::Color{static_cast<std::uint8_t>(80 + speed), static_cast<std::uint8_t>(80 + speed), static_cast<std::uint8_t>(130 + speed), 255};
                    sprite.layer = -10;
                    world().add<kuge::Transform2D>(star, kuge::Transform2D{{static_cast<float>(rng() % 360), static_cast<float>(rng() % 480)}});
                    world().add<kuge::Sprite>(star, sprite);
                    world().add<Star>(star, Star{speed});
                }
            }

            kw::Entity hudPiece(kuge::Vec2 at, kuge::Vec2 size, kuge::Color tint, kuge::Vec2 pivot = {0.5f, 0.5f})
            {
                const kw::Entity e = world().create();
                kuge::Sprite sprite;

                sprite.size = size;
                sprite.tint = tint;
                sprite.pivot = pivot;
                sprite.layer = 20;
                sprite.visible = false;
                world().add<kuge::Transform2D>(e, kuge::Transform2D{at});
                world().add<kuge::Sprite>(e, sprite);
                return e;
            }

            void makeHud(void)
            {
                for (int i = 0; i < 8; ++i) {
                    m_hud.lives.push_back(hudPiece({10.0f + 11.0f * static_cast<float>(i), 10.0f}, {7.0f, 8.0f}, {230, 230, 255, 255}));
                }
                for (int i = 0; i < 3; ++i) {
                    m_hud.power.push_back(hudPiece({ARENA_W - 10.0f - 9.0f * static_cast<float>(i), 10.0f}, {6.0f, 9.0f}, {90, 230, 120, 255}));
                }
                m_hud.bossBack = hudPiece({ARENA_W / 2, 24.0f}, {204.0f, 8.0f}, {60, 20, 30, 255});
                m_hud.bossFill = hudPiece({ARENA_W / 2 - 100.0f, 24.0f}, {200.0f, 4.0f}, {240, 60, 80, 255}, {0.0f, 0.5f});
                world().get<kuge::Sprite>(m_hud.bossFill).z = 1.0f;
                m_hud.banner = hudPiece({ARENA_W / 2, ARENA_H / 2}, {ARENA_W, 36.0f}, {0, 0, 0, 255});
            }

            void loadFont(void)
            {
                if (m_options.font.empty()) {
                    return;
                }
                try {
                    m_font = ctx().engine().module<kuge::ClientModule>()->loadFont(m_options.font, 16);
                } catch (const std::exception& error) {
                    Logger::logger().warn("starfall: no text: {}", error.what());
                }
            }

            // -- Each frame ------------------------------------------------------------------------------
            void frame(kw::World& world)
            {
                const double frameDt = world.getResource<kuge::Time>().frameDt;
                const kuge::Vec2 screen = world.getResource<kuge::Ref<kuge::IRenderer2D>>()->outputSize();
                auto& camera = world.getResource<kuge::Camera2D>();

                camera.position = {ARENA_W / 2, ARENA_H / 2};
                camera.zoom = std::min(screen.x / ARENA_W, screen.y / ARENA_H);
                if (m_replication) {
                    m_replication->update(frameDt);          // the others, a little in the past, between two snapshots
                }
                ++m_frames;
                std::vector<kw::Entity> stars;
                auto view = world.view<Star>();

                for (kw::Entity e : view) {
                    stars.push_back(e);
                }
                for (kw::Entity e : stars) {
                    auto& at = world.get<kuge::Transform2D>(e).position;

                    at.y += world.get<Star>(e).speed * static_cast<float>(frameDt);
                    at.y -= at.y > ARENA_H ? ARENA_H : 0.0f;
                }
                showWorld(world);
                showHud(world);
            }

            void showWorld(kw::World& world)
            {
                ClientReport& report = *m_report;
                std::size_t ships = 0, enemies = 0, shots = 0, enemyShots = 0;
                auto view = world.view<kuge::replication::Replicated>();

                m_bossLeft = -1.0f;
                for (kw::Entity e : view) {
                    const auto& info = world.get<kuge::replication::Replicated>(e);

                    switch (info.type) {
                        case TEAM:
                            m_team = world.has<TeamState>(e) ? world.get<TeamState>(e) : m_team;
                            break;
                        case SHIP: {
                            const std::uint8_t mode = world.has<PilotState>(e) ? world.get<PilotState>(e).mode : std::uint8_t{PilotState::Flying};

                            ++ships;
                            // Down: not there. Shielded: blinks.
                            world.get<kuge::Sprite>(e).visible = mode != PilotState::Down && !(mode == PilotState::Shielded && (m_frames / 4) % 2 == 1);
                            if (info.owner == m_networkId) {
                                report.mode = mode;
                                report.power = world.has<Power>(e) ? world.get<Power>(e).level : 0;
                                report.score = world.has<Score>(e) ? world.get<Score>(e).points : 0;
                                report.shipAt = world.get<kuge::Transform2D>(e).position;
                            }
                            break;
                        }
                        case SHOT:       ++shots; break;
                        case ENEMY_SHOT: ++enemyShots; break;
                        case POWERUP:    break;
                        default:
                            ++enemies;
                            if (info.type == BOSS && world.has<Health>(e)) {
                                const Health& health = world.get<Health>(e);

                                m_bossLeft = static_cast<float>(health.points) / static_cast<float>(std::max(1, health.max));
                            }
                            break;
                    }
                }
                report.ships = ships;
                report.enemies = enemies;
                report.shots = shots;
                report.enemyShots = enemyShots;
                report.mostShips = std::max(report.mostShips, ships);
                report.mostEnemies = std::max(report.mostEnemies, enemies);
                report.mostShots = std::max(report.mostShots, shots);
                report.team = m_team;
                if (m_prediction) {
                    report.prediction = m_prediction->stats();
                }
                if (m_replication) {
                    report.replication = m_replication->stats();
                }
            }

            void showHud(kw::World& world)
            {
                const bool playing = m_replication != nullptr;
                const auto show = [&world](kw::Entity e, bool visible) { world.get<kuge::Sprite>(e).visible = visible; };

                for (std::size_t i = 0; i < m_hud.lives.size(); ++i) {
                    show(m_hud.lives[i], playing && i < m_team.lives);
                }
                for (std::size_t i = 0; i < m_hud.power.size(); ++i) {
                    show(m_hud.power[i], playing && i < m_report->power);
                }
                show(m_hud.bossBack, m_bossLeft >= 0.0f);
                show(m_hud.bossFill, m_bossLeft >= 0.0f);
                world.get<kuge::Sprite>(m_hud.bossFill).size.x = 200.0f * std::max(0.0f, m_bossLeft);
                show(m_hud.banner, m_team.result != Outcome::Playing);
                world.get<kuge::Sprite>(m_hud.banner).tint = m_team.result == Outcome::Won ? kuge::Color{40, 150, 80, 220} : kuge::Color{170, 40, 50, 220};
            }

            // Text needs a font: without one, the HUD above says it all with shapes
            void drawText(kw::World& world)
            {
                if (!m_font) {
                    return;
                }
                auto& text = *world.getResource<kuge::Ref<kuge::TextRenderer>>();
                const kuge::Vec2 screen = world.getResource<kuge::Ref<kuge::IRenderer2D>>()->outputSize();

                text.draw(*m_font, "SCORE " + std::to_string(m_team.score), {12.0f, 34.0f}, kuge::Color{230, 230, 255, 255});
                if (m_team.result != Outcome::Playing) {
                    const std::string label = m_team.result == Outcome::Won ? "STAGE CLEAR" : "GAME OVER";
                    const kuge::Vec2 size = m_font->measure(label);

                    text.draw(*m_font, label, {(screen.x - size.x) / 2, (screen.y - size.y) / 2}, kuge::colors::White);
                }
            }

            ClientOptions                                                 m_options;
            std::shared_ptr<ClientReport>                                 m_report;
            kuge::replication::ReplicationRegistry                        m_registry;      // before what uses it
            std::unique_ptr<kuge::net::MatchmakingClient>                 m_matchmaking;
            std::unique_ptr<kuge::replication::ReplicationClient>         m_replication;
            std::unique_ptr<kuge::replication::Prediction<PilotInput>>    m_prediction;    // after the replication: goes first
            std::shared_ptr<kuge::IFont>                                  m_font;
            Hud                                                           m_hud;
            TeamState                                                     m_team;
            float                                                         m_bossLeft = -1.0f;
            std::uint32_t                                                 m_networkId = 0;
            std::uint32_t                                                 m_waited = 0;
            std::uint32_t                                                 m_rejoinIn = 0;
            std::uint64_t                                                 m_frames = 0;
    };

}
```

`example/starfall/client/main.cpp`:

```cpp
// The Starfall client: a window on a game that runs on a server.
//
//     starfall_client [--host H] [--port P] [--name N] [--font F.ttf]
//         arrows / WASD / left stick: move, Space / pad A: fire, Left Shift / pad B: focus, Esc: quit

#include "StarfallScene.hpp"
#include "backend/SdlBackend.hpp"
#include "Engine.hpp"
#include "Logger.hpp"
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv)
{
    starfall::ClientOptions options;

    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--host") && i + 1 < argc) {
            options.host = argv[++i];
        } else if (!std::strcmp(argv[i], "--port") && i + 1 < argc) {
            options.port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        } else if (!std::strcmp(argv[i], "--name") && i + 1 < argc) {
            options.name = argv[++i];
        } else if (!std::strcmp(argv[i], "--font") && i + 1 < argc) {
            options.font = argv[++i];
        } else {
            Logger::logger().error("usage: starfall_client [--host H] [--port P] [--name N] [--font F.ttf]");
            return 2;
        }
    }
    try {
        kuge::WindowConfig window;

        window.title = "Starfall";
        window.width = 540;
        window.height = 720;
        kuge::Engine engine({.mode = kuge::Engine::Mode::Windowed, .tickRate = starfall::TICK_RATE});
        auto& client = engine.addModule<kuge::ClientModule>(kuge::makeSdlBackend(window), kuge::ClientConfig{kuge::Color{4, 4, 14, 255}});

        starfall::bindDefaults(client.input());
        client.input().loadBindings("starfall-keys.cfg");   // the player's own keys, if the file is there
        engine.scenes().change<starfall::StarfallScene>(options, std::make_shared<starfall::ClientReport>());
        return engine.run();
    } catch (const std::exception& error) {
        Logger::logger().error("{}", error.what());
        return 1;
    }
}
```

The life of a client:

1. `connect()` reaches the lobby over TCP and asks for a `"starfall"` room. The lobby answers with the room's
   address and a one-time token. `MatchmakingClient` connects to the room over UDP, says hello with the token, and
   calls `joined()` once the room welcomes it.
2. `joined()` makes a `ReplicationClient` and a `Prediction` on the room's endpoint. The prediction's private world
   holds the arena and one ship, built by the functions the room uses.
3. At each tick, `tick()` turns the keys into a `PilotInput` and gives it to the prediction. The prediction moves
   your ship at once and sends the last 4 inputs.
4. At each frame, `frame()` lets the replication place everything else between two snapshots, scrolls the stars,
   and updates the HUD.
5. `roomClosed()` drops the replication and the prediction at once: both hold the room's endpoint, which is going
   away. The last picture stays on screen, and the client asks for a new game a second later. `joined()` then clears
   the old game's entities, because nobody else will. (The repository's R-Type client does not do this, so after its
   first game the entities of the previous one stay on screen, frozen. Step 8 has a test that catches exactly that.)

Details that matter:

- **`predictType(SHIP)`**: only your *ship* is predicted. Your shots are yours too (`owner`), but the server moves
  them.
- **`dress()` is the prefab.** The server says *what* an entity is (its `EntityType`); the client decides what it
  looks like. Your own ship is white and gets a `PreviousTransform2D`: it moves at each tick, so on a 144 Hz screen
  it is drawn between two ticks.
- **The HUD uses shapes**: lives, power, the boss's bar and a result banner. It needs no file at all. Give
  `--font some.ttf` and the score and the result are also written.
- **Downed ships are hidden, and shielded ones blink**, all from `PilotState`, which the server replicates.
- **Players can rebind keys** in `starfall-keys.cfg` (for example `input.fire = Space, Pad.A`). Do not put a
  comment at the end of a line there (see step 10).
- **Start order does not matter.** With no server, the client tries again every second.

## Step 6: The host

`example/starfall/host/main.cpp`:

```cpp
// Starfall, hosted: the server and your window in one process. Your friends join with
//
//     starfall_client --host <your address> --port <port>
//
//     starfall_host [--port P] [--name N] [--font F.ttf]      (4242 by default: TCP for the lobby, UDP after it)

#include "GameServer.hpp"
#include "StarfallRoom.hpp"
#include "StarfallScene.hpp"
#include "backend/SdlBackend.hpp"
#include "Engine.hpp"
#include "Logger.hpp"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

int main(int argc, char** argv)
{
    starfall::ClientOptions options;

    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--port") && i + 1 < argc) {
            options.port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        } else if (!std::strcmp(argv[i], "--name") && i + 1 < argc) {
            options.name = argv[++i];
        } else if (!std::strcmp(argv[i], "--font") && i + 1 < argc) {
            options.font = argv[++i];
        } else {
            Logger::logger().error("usage: starfall_host [--port P] [--name N] [--font F.ttf]");
            return 2;
        }
    }
    try {
        // The server: a GameServer of its own, with no window, on a thread of its own
        kuge::server::ServerConfig config;

        config.tickRate = starfall::TICK_RATE;
        config.lobbyPort = options.port;
        config.roomPortFirst = static_cast<std::uint16_t>(options.port + 1);
        kuge::server::GameServer server(config);

        server.addRoomType<starfall::StarfallRoom>("starfall", {.maxPlayers = 4, .idleTimeout = 30.0});
        std::thread serverThread([&server] {
            try {
                server.run();
            } catch (const std::exception& error) {
                Logger::logger().error("the server stopped: {}", error.what());
            }
        });
        // (The client would retry until the lobby is up: this only saves it a second)
        for (int i = 0; i < 2000 && !server.stats().lobbyOpen; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        {
            // The client: exactly the one of starfall_client, pointed at this machine
            kuge::WindowConfig window;

            window.title = "Starfall (host)";
            window.width = 540;
            window.height = 720;
            kuge::Engine engine({.mode = kuge::Engine::Mode::Windowed, .tickRate = starfall::TICK_RATE});
            auto& client = engine.addModule<kuge::ClientModule>(kuge::makeSdlBackend(window), kuge::ClientConfig{kuge::Color{4, 4, 14, 255}});

            options.host = "127.0.0.1";
            starfall::bindDefaults(client.input());
            client.input().loadBindings("starfall-keys.cfg");
            engine.scenes().change<starfall::StarfallScene>(options, std::make_shared<starfall::ClientReport>());
            engine.run();
        }
        server.stop();
        serverThread.join();
        return 0;
    } catch (const std::exception& error) {
        Logger::logger().error("{}", error.what());
        return 1;
    }
}
```

The host is the room of step 4 and the client of step 5, unchanged, in one process. A `GameServer` runs on a thread
of its own, with its own engine and no window. Your window runs on the main thread and connects to `127.0.0.1` like
any other client. Your friends reach the same lobby with `starfall_client --host <your address> --port <port>`.

For a solo game that opens no port at all, give both sides a loopback network instead of sockets:

```cpp
kuge::net::LoopbackNetwork network;                       // outlives the server and the client

config.transport = kuge::server::Transport::Loopback;     // ServerConfig
config.loopback = &network;
options.sockets = false;                                  // starfall::ClientOptions
options.network = &network;
```

## Step 7: Build and play

```sh
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build -j --target starfall_server starfall_client starfall_host

./build/example/starfall/starfall_server 4242                                  # a dedicated server
./build/example/starfall/starfall_client --host 127.0.0.1 --port 4242 --name Ana
./build/example/starfall/starfall_host --port 4242                             # or host and play
```

- **Ports.** The lobby is TCP on the given port. Rooms are UDP on the ports after it: one per room running at the
  same time (`roomPortCount`, 64 by default). A firewall must let both through.
- **Behind a NAT**, set `ServerConfig::roomAddress` to the public address. It is what the lobby tells clients to use
  for rooms; left empty, clients use the address they reached the lobby with.
- **A server-only build**, on a machine with no SDL:
  `cmake -S . -B build-server -DKUGE_BUILD_CLIENT=OFF -DBUILD_EXAMPLES=ON`, then
  `cmake --build build-server --target starfall_server`.
- **No screen** (CI, a container): run with `SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy`.

## Step 8: Tests

`tests/starfall/starfall_test.cpp`:

```cpp
extern "C" {
    #include "kronklab/kronklab.h"
}
#include "GameServer.hpp"
#include "StarfallRoom.hpp"
#include "StarfallScene.hpp"
#include "backend/dummy/DummyBackend.hpp"
#include <chrono>
#include <functional>
#include <thread>

// NOTE: kronklab test names are limited to 31 characters.

using namespace starfall;

namespace
{
    using Clock = std::chrono::steady_clock;
    constexpr std::uint32_t NEVER = 1u << 30;

    // A game of seconds: three drones, then a weak boss
    void quickGame(void)
    {
        settings() = Settings{};
        settings().waves = {{30, DRONE, 180.0f, 3, 20}};
        settings().bossAt = 200;
        settings().bossHealth = 3;
    }

    // The rules in a World of their own: no network, no screen
    struct Table
    {
        kw::World world;

        Table(void)
        {
            buildArena(world);
            world.addResource<Match>();
            startMatch(world);
        }

        kw::Entity pilot(kuge::Vec2 at, std::uint32_t id = 1)
        {
            const kw::Entity ship = addPilot(world, id, 0);

            world.get<kuge::Transform2D>(ship).position = at;
            world.get<PilotState>(ship).mode = PilotState::Flying;   // no shield: the tests want hits
            world.get<Respawn>(ship).timer = 0;
            return ship;
        }

        void ticks(std::uint32_t count)
        {
            for (std::uint32_t i = 0; i < count; ++i) {
                stepArena(world, TICK_DT);
                stepRules(world);
            }
        }

        int mode(kw::Entity ship) { return world.get<PilotState>(ship).mode; }
        kuge::Vec2 at(kw::Entity e) { return world.get<kuge::Transform2D>(e).position; }
    };
}

// -- The rules alone ------------------------------------------------------------------------------
Test(starfall_rules, a_shot_kills_a_drone)
{
    quickGame();
    settings().waves.clear();
    settings().bossAt = NEVER;
    Table table;
    const kw::Entity ship = table.pilot({180.0f, 400.0f});

    spawnEnemy(table.world, DRONE, {180.0f, 200.0f});
    for (int i = 0; i < 60; ++i) {
        fire(table.world, ship);
        table.ticks(1);
    }
    AssertEq(table.world.getResource<Match>().kills, 1u, "the drone in the line of fire is dead");
    Assert(team(table.world).score > 0 && table.world.get<Score>(ship).points > 0, "the team and the shooter scored");
}

Test(starfall_rules, a_hit_pilot_comes_back)
{
    quickGame();
    settings().waves.clear();
    settings().bossAt = NEVER;
    Table table;
    const kw::Entity ship = table.pilot({180.0f, 400.0f});
    const int lives = team(table.world).lives;

    spawnEnemy(table.world, DRONE, {180.0f, 400.0f});
    table.ticks(1);
    AssertEq(table.mode(ship), PilotState::Down, "hit: the pilot is down");
    table.ticks(settings().downTicks);
    AssertEq(table.mode(ship), PilotState::Shielded, "back, with a shield");
    AssertEq(team(table.world).lives, lives - 1, "for one of the team's lives");
    table.ticks(settings().shieldTicks);
    AssertEq(table.mode(ship), PilotState::Flying, "the shield is gone");
    Assert(table.world.has<Owner>(ship), "and it was the same entity all along: the room may keep its number");
}

Test(starfall_rules, no_life_left_is_game_over)
{
    quickGame();
    settings().waves.clear();
    settings().bossAt = NEVER;
    settings().teamLives = 0;
    Table table;
    const kw::Entity ship = table.pilot({180.0f, 400.0f});

    spawnEnemy(table.world, DRONE, table.at(ship));
    table.ticks(1);
    Assert(outcome(table.world) == Outcome::Playing, "down, not out yet");
    table.ticks(settings().downTicks);
    Assert(outcome(table.world) == Outcome::Lost, "no life to come back with: lost");
    Assert(team(table.world).result == Outcome::Lost, "and the team says so (the clients will see it)");
}

Test(starfall_rules, the_boss_ends_the_game)
{
    quickGame();
    settings().waves.clear();
    settings().bossAt = 1;
    Table table;
    const kw::Entity ship = table.pilot({180.0f, 440.0f});

    for (int i = 0; i < 900 && outcome(table.world) == Outcome::Playing; ++i) {
        fire(table.world, ship);
        table.ticks(1);
    }
    Assert(outcome(table.world) == Outcome::Won, "the boss is down: won");
}

Test(starfall_rules, same_inputs_same_game)
{
    const auto play = [] {
        quickGame();
        settings().waves = defaultWaves();
        Table table;
        const kw::Entity a = table.pilot({120.0f, 420.0f}, 1);
        const kw::Entity b = table.pilot({240.0f, 420.0f}, 2);
        std::vector<float> trace;

        for (int t = 0; t < 900; ++t) {
            steerShip(table.world, a, PilotInput{static_cast<std::int8_t>((t / 50) % 2 ? 1 : -1), 0, true, false});
            steerShip(table.world, b, PilotInput{0, static_cast<std::int8_t>((t / 70) % 2 ? 1 : -1), true, t % 200 < 100});
            fire(table.world, a);
            fire(table.world, b);
            table.ticks(1);
            trace.push_back(table.at(a).x);
            trace.push_back(table.at(b).y);
            trace.push_back(static_cast<float>(team(table.world).score));
            trace.push_back(static_cast<float>(table.world.getResource<Match>().kills));
        }
        return trace;
    };

    Assert(play() == play(), "the same inputs give the same game, tick for tick");
}

// -- A server and two pilots in this process -------------------------------------------------------
namespace
{
    // A client with no screen: the real scene, on the dummy backend
    struct Pilot
    {
        kuge::DummyBackend              dummy;
        kuge::Engine                    engine;
        kuge::ClientModule*             client;
        std::shared_ptr<ClientReport>   report = std::make_shared<ClientReport>();

        explicit Pilot(ClientOptions options)
            : dummy(kuge::makeDummyBackend({540.0f, 720.0f})),
              engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed, .tickRate = TICK_RATE}),
              client(&engine.addModule<kuge::ClientModule>(std::move(dummy.backend)))
        {
            bindDefaults(client->input());
            engine.scenes().change<StarfallScene>(std::move(options), report);
        }

        void press(kuge::Key key, bool down) { client->input().handle(kuge::KeyEvent{key, down}); }
    };

    // The pilots play at 60 Hz, in real time (the server keeps its own clock), until `done` or the time is up
    bool flyUntil(const std::vector<Pilot*>& pilots, const std::function<bool(void)>& done, double seconds)
    {
        const auto end = Clock::now() + std::chrono::duration<double>(seconds);
        auto next = Clock::now();

        while (!done() && Clock::now() < end) {
            for (Pilot* pilot : pilots) {
                pilot->engine.step(1.0 / 60.0);
            }
            next += std::chrono::microseconds(16667);    // an absolute schedule: no drift from the server's clock
            std::this_thread::sleep_until(next);
        }
        return done();
    }

    // A GameServer on a thread, reached through a loopback network of this process
    struct Server
    {
        kuge::net::LoopbackNetwork                network;
        std::unique_ptr<kuge::server::GameServer> server;
        std::thread                               thread;

        Server(void)
        {
            kuge::server::ServerConfig config;

            config.transport = kuge::server::Transport::Loopback;
            config.loopback = &network;
            config.tickRate = TICK_RATE;
            server = std::make_unique<kuge::server::GameServer>(config);
            server->addRoomType<StarfallRoom>("starfall", {.maxPlayers = 4, .idleTimeout = 30.0});
            thread = std::thread([this] { server->run(); });
            while (!server->stats().lobbyOpen) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        ~Server(void)
        {
            server->stop();
            thread.join();
        }

        ClientOptions options(const char* name)
        {
            ClientOptions options;

            options.sockets = false;
            options.network = &network;
            options.name = name;
            return options;
        }
    };
}

Test(starfall_net, two_pilots_play_together)
{
    quickGame();
    settings().bossAt = NEVER;
    settings().waves = {{60, DRONE, 180.0f, 100, 30}};     // a stream of drones to shoot at
    Server server;
    Pilot ana(server.options("Ana"));
    Pilot ben(server.options("Ben"));

    Assert(flyUntil({&ana, &ben}, [&] { return ana.report->ships == 2 && ben.report->ships == 2; }, 20.0),
        "both pilots are in the same game, and see both ships");
    const float before = ana.report->shipAt.x;

    ana.press(kuge::Key::Right, true);
    Assert(flyUntil({&ana, &ben}, [&] { return ana.report->shipAt.x > before + 60.0f; }, 5.0), "Ana's ship answers her keys");
    ana.press(kuge::Key::Right, false);
    ana.press(kuge::Key::Space, true);
    ben.press(kuge::Key::Space, true);
    Assert(flyUntil({&ana, &ben}, [&] { return ana.report->mostShots > 0 && ben.report->mostShots > 0 && ana.report->mostEnemies > 0; }, 10.0),
        "shots and enemies, on both screens");
    flyUntil({&ana, &ben}, [] { return false; }, 2.0);     // two more seconds of play, to judge the prediction
    const auto& stats = ana.report->prediction;

    Assert(stats.reconciliations > 60, "Ana's prediction was checked by the server: %llu times", static_cast<unsigned long long>(stats.reconciliations));
    Assert(stats.corrections * 5 <= stats.reconciliations, "and nearly never contradicted: %llu corrections in %llu",
        static_cast<unsigned long long>(stats.corrections), static_cast<unsigned long long>(stats.reconciliations));
}

Test(starfall_net, a_game_ends_and_restarts)
{
    quickGame();
    settings().endDelay = 30;
    Server server;
    Pilot ana(server.options("Ana"));
    Pilot ben(server.options("Ben"));

    Assert(flyUntil({&ana, &ben}, [&] { return ana.report->ships == 2 && ben.report->ships == 2; }, 20.0), "in the game");
    ana.press(kuge::Key::Space, true);
    ben.press(kuge::Key::Space, true);
    Assert(flyUntil({&ana, &ben}, [&] { return ana.report->team.result == Outcome::Won && ben.report->team.result == Outcome::Won; }, 40.0),
        "they beat the boss, and both screens say so");
    Assert(flyUntil({&ana, &ben}, [&] { return ana.report->games == 2 && ben.report->games == 2; }, 20.0), "then they are in a new game");
    Assert(flyUntil({&ana, &ben}, [&] { return ana.report->ships == 2 && ana.report->team.result == Outcome::Playing; }, 5.0),
        "with two ships on screen: nothing is left of the last game (%zu)", ana.report->ships);
}
```

kronklab (the test library) is only fetched after `example/` is read, so a game's tests go in `tests/`, like
R-Type's. Add this to `tests/CMakeLists.txt`:

```cmake
# Starfall: the rules alone, and two pilots with a server in the same process
if(TARGET kuge-client AND TARGET starfall-room)
    add_executable(starfall_tests starfall/starfall_test.cpp)
    target_link_libraries(starfall_tests PRIVATE starfall-client starfall-room kronklab-static)
    target_compile_options(starfall_tests PRIVATE -Wall -Wextra)
    kuge_add_test(starfall_tests starfall_tests)
endif()
```

Then run `ctest --test-dir build -R starfall --output-on-failure`. It takes about ten seconds, because the network
tests run in real time.

| Test | What it proves |
|---|---|
| `a_shot_kills_a_drone` | Shooting, damage and score. |
| `a_hit_pilot_comes_back` | Down, back for a team life, the shield, and that the ship keeps its entity throughout. |
| `no_life_left_is_game_over` | The losing end, and that it reaches `TeamState`. |
| `the_boss_ends_the_game` | The winning end. |
| `same_inputs_same_game` | Determinism, which prediction and replays rest on. |
| `two_pilots_play_together` | Lobby, room, and replication both ways. A pilot's ship answers her keys, and the server almost never contradicts her prediction (at most one correction in five). |
| `a_game_ends_and_restarts` | Both screens see the result, a new game starts, and nothing of the old one is left. |

The last two use the real client scene on the dummy backend, and a real `GameServer` on a thread, over a loopback
network. The pilots step at 60 Hz on an absolute schedule, because a client that runs faster than the room piles
inputs up. Two ways to go further:

- **A bad network.** Build the `Server` helper's network with `LoopbackNetwork(Conditions{.loss = 0.1, .latency =
  0.05, .jitter = 0.02})`, which is 100 ms of round trip with 10 % loss, and check that the prediction stats stay
  sane.
- **The real programs.** Give the client a scripted `--frames` mode that saves a screenshot, as
  `example/rtype/client/main.cpp` does, and run it from a script like `cmake/RunRType.sh`.

## Step 9: Making it feel right

[R-Type's step 7](../11-make-rtype/07-making-it-feel-right/README.md) explains what prediction and interpolation
do. This step covers what is special about a shmup.

**Enemy bullets are seen late.** Your ship is drawn *now*, because it is predicted. Enemy bullets are drawn where
the server had them about `interpolationDelay` plus half a round trip ago. Take a bullet flying at you at 120 px/s,
with 0.1 s of interpolation and 50 ms of one-way latency: it is drawn about 18 px behind where the server has it,
which is far more than the 4 px core. It can hit you while it still seems some way off. Starfall limits the damage
with a small core, slow enemy bullets (95 to 120 px/s), and bullets drawn larger (6 px) than the core. The knobs:

| Knob | Where | Effect |
|---|---|---|
| `ReplicationClientConfig::interpolationDelay` | the client, in `joined()` | 0.1 s by default. 0.05 s with a snapshot every tick halves the lag. |
| `ReplicationServerConfig::sendInterval` | the room | 2 by default (30 snapshots a second); 1 sends 60, at twice the bandwidth. |
| Enemy bullet speed | the rules | Slower bullets, smaller error. |
| `SHIP_CORE` | the shared file | The budget the error must fit in. |

The real cure is **lag compensation**: the server checks enemy bullets against each pilot where *that pilot* saw
them, rewound by its delay. The room needs a short history of bullet positions, and each client's delay:
`endpoint().rtt(connection)` gives the round trip, to which you add the interpolation delay. It is the first idea in
"Where to go next".

**Your shots also appear late.** The server makes them, so they show up about a round trip plus the interpolation
delay after you fire. A cheap fix is a client-only muzzle flash on your ship the moment you press Fire.

**Respawns snap.** When the server puts your ship back at the bottom, the prediction is more than `snapDistance`
(64 px) off, so it moves the ship there at once instead of sliding it. That is intended.

**Watch the numbers.** `ClientReport::prediction.corrections` should stay near zero while nothing happens to your
ship. The test allows at most 20 %.

## Step 10: Traps

These are real behaviours of the engine at commit `edfcaab`, some of them found while reviewing it. Starfall avoids
every one of them, as the right-hand column shows.

| Trap | What happens | What Starfall does |
|---|---|---|
| **Entity numbers are reused** (there is no generation counter) | After a removal, a kept `kw::Entity` can name another entity. | Ships are never removed while their pilot is in the room. Lists are re-checked with `has<>()` after a removal. HUD entities are never removed. |
| **`Prediction` never learns that its entity is gone** | It keeps writing into the next entity that gets that number. | A hit ship is *down*, not destroyed. |
| **A new `ReplicationClient` does not know the previous game's entities** | They stay on screen forever. | `clearLastGame()` in `joined()`. |
| **`RoomClosed` overtakes the last snapshots** | The result of the game never shows. | The room waits `endDelay` ticks with the result replicated. |
| **`Endpoint::disconnect()` on an endpoint that a `MatchmakingClient` watches, called outside a poll** | The endpoint is destroyed while it is still running (use after free). | Connections are only ended through `MatchmakingClient::leave()` or `disconnect()`. |
| **A comment at the end of a line in a `.cfg` file** | It becomes part of the value: `Pad.A   # fire` is not a key. | Comments go on their own line. |
| **A full UDP send buffer** | kronknet may glue several datagrams into one, and a reliable message can be lost. | Snapshots stay small (4 pilots, around a hundred entities). Test under load before sending 60 snapshots a second with hundreds of bullets. |
| **Clocks** | A client that ticks faster than its room piles its inputs up. | `Engine::run()`, and absolute schedules in tests; `jitter = 2` in the room. |
| **Snapshot tick 0** | It means "no snapshot". | `update(tick + 1)`. |
| **`warning: writing 1 byte into a region of size 0`** from `Wire.hpp` | A GCC 13 false positive, in the engine's `decode` of a byte vector. | Nothing to do. |

Also from [the pitfalls page](../14-pitfalls/README.md): one registry for both sides; the same movement code on both
sides; no `std::random_device`, wall clock or unordered iteration in predicted code; test names of at most 31
characters.

## Where to go next

| Idea | How it fits |
|---|---|
| **Lag-compensated hits** | In the room, keep about 0.3 s of enemy bullet positions. In `resolveHits`, test each pilot against the positions *that pilot* saw (half its round trip plus the interpolation delay ago). |
| **A bomb that clears the bullets** | A new field in `PilotInput` (both sides share the struct, so nothing else changes), a `Bombs` component replicated `OnChange` for the HUD, and the effect in the rules. |
| **Art and sound** | `client.textures().load("ship.png")` in `dress()`. For sounds, use `Ref<Audio>` in the replication's `onSpawn` and `onDestroy` hooks (the server never plays a sound). |
| **Stages from files** | Load `Settings::waves` from a text file, with `ConfigFile` or your own format. |
| **Menus, a name, "waiting for players"** | `installUi`, as the platformer's `MenuScene` does. Only call `join()` once the player presses Start. |
| **High scores** | `SaveSlots`: on the client, or on the server when a room finishes. |
| **Bigger games** | Raise `maxPlayers`. If the arena scrolls and grows, use `ReplicationServer::setFilter` so each client only receives what it can see. |
| **A smoke test of the real programs** | A scripted `--frames` mode and a screenshot, like `kuge_rtype_client` and `cmake/RunRType.sh`. |
