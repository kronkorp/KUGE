# 20 Scripts and mods in Lua

A **mod** is a folder that changes a game without rebuilding it. A mod can change **data** (a JSON of numbers, a sprite, a
sound) and, if the game allows it, add a **behaviour** with a **Lua script**: an enemy that dodges, a weapon with an effect
that exists nowhere in the game's C++.

KUGE does this with three small pieces that you already know, plus one new module:

| Piece | Role |
|---|---|
| `kuge::Mods` (core) | Finds the mods, puts them in order, lays their files over the game's, lists their scripts, hashes everything for the multiplayer check |
| `kuge::ComponentRegistry` (core) | The list of the components **the game chose to show**, by name, with their fields, types and limits |
| `kuge::ScriptEngine` (`kuge-script`) | One sandboxed Lua state, with one scope of globals per mod. Gives the scripts a single door to the game: the `world` table |
| `kuge::ScriptSystem` (`kuge-script`) | A system that calls the `update(dt)` of every mod at each tick, and stops a mod that fails (alone) |

**The idea in one sentence:** the game decides what is moddable by *describing* components to the registry; a script can read and
write exactly those, by name, inside a sandbox that has no files, no clock and a budget.

The working example is [`example/modding`](../../example/modding/README.md) (a game of 250 lines with a mod next to it): read
it with this chapter at hand.

- [1 The big picture](#1-the-big-picture)
- [2 Writing a mod](#2-writing-a-mod) (for the person who makes the mod)
- [3 Making your game moddable](#3-making-your-game-moddable) (for the person who makes the game)
- [4 How it works inside](#4-how-it-works-inside)
- [5 Limits](#5-limits-what-is-not-there-and-what-the-sandbox-does-not-stop)
- [6 When something goes wrong](#6-when-something-goes-wrong)

---

## 1 The big picture

```
 mods/golden/                                         the game (C++)
   mod.json                                             registry.describe<Faller>("Faller", {...})
   data/catch.json   ─┐                                 registry.describe<...>(...)          ← what a script may see
   scripts/golden.lua ┼─► Mods::scan("mods") ──┐
                      │                        │
                      │      Mods::apply(roots)│       data: AssetRoots finds the mod's file before the game's
                      │      Mods::scripts() ──┴─►  ScriptEngine::load(file, mod)   (one Lua state, each mod its own globals)
                                                            │
                          every tick (Fixed schedule)       ▼
                          ScriptSystem ───────────►  update(dt)  in Lua
                                                            │   world:view{...}  world:get(...)  world:set(...)  world:spawn{...}
                                                            ▼
                                                    ComponentRegistry  ──►  the real components of the real World
```

Three things to keep in mind:

1. **A script never sees C++.** It sees names (`"Health"`, `"points"`) and plain Lua values (numbers, strings, tables). The
   registry turns them into the game's real fields, checks the type and the limits, and refuses anything else.
2. **The game stays in charge.** A component that is not described does not exist for a script. The game draws, plays sounds,
   moves the player, and may *dress* what a script creates (in Catch, a script makes a `Faller`; the game gives it a sprite).
3. **Nothing a script does can crash or freeze the game.** A mistake stops *that script* and is logged; an infinite loop is cut
   by a budget; memory is capped.

---

## 2 Writing a mod

### 2.1 The folder

```
mods/golden/
    mod.json                  who it is
    data/catch.json           replaces the game's data/catch.json
    resources/sprites/x.png   replaces or adds a resource (same layout as the game's own folders)
    scripts/golden.lua        a script (every scripts/*.lua is loaded)
    scripts/more/extra.lua    sub-folders are fine
```

`mod.json`:

```json
{ "name": "golden", "version": "1.0", "kuge": "0.3", "depends": ["base_pack"] }
```

| Key | Meaning |
|---|---|
| `name` | The mod's name. Used in messages, in the order and in `depends` |
| `version` | Free text. It is part of the fingerprint: changing it changes the fingerprint |
| `kuge` | The KUGE version the mod was made for. The game may refuse a mod made for another one (it is checked only if the game asks: `Mods::scan(folder, version)`) |
| `depends` | Mods that must be loaded **before** this one. A cycle or a missing dependency is refused, naming the mod |

A folder in `mods/` without a `mod.json` is not a mod and is ignored.

**Load order.** By `depends` first (a mod comes after what it depends on), then by name. It is the same on every machine. The
**last** mod loaded wins: if two mods replace the same file, the later one's file is the one the game reads. Scripts are different:
every mod's `update` runs, in this order (see [2.6](#26-several-scripts-and-several-mods)).

### 2.2 Data: change numbers and files, with no code

A file in a mod that has the same path as one in the game **replaces it as a whole** (it is not merged). In Catch:

```
data/catch.json                  { "playerSpeed": 300, "spawnEvery": 1.0 }      the game's
mods/golden/data/catch.json      { "playerSpeed": 450 }                         the mod's: faster
```

Catch reads a missing key with its default, so the mod only writes what it changes: `spawnEvery` stays 1.0. That is the game's
choice, not the engine's: if a game reads the file with no defaults, the mod must write every key.

The game logs what a mod replaced when it starts: `[modding] golden replaces data/catch.json of game`.

### 2.3 Your first script

A script is a Lua file that **defines functions**. The game calls the global `update(dt)` at each tick (60 times a second by
default); `dt` is the time of a tick in seconds (1/60), always the same.

```lua
-- scripts/hello.lua
local timer = 0.0                       -- `local`: private to this file

function update(dt)                     -- global: the game calls it
    timer = timer + dt
    if timer >= 5 then
        timer = timer - 5
        print("five seconds went by")   -- goes to the game's log, category [script]
    end
end
```

The top of the file (the lines outside any function) runs **once**, when the script is loaded. `world` is **not available
there**: it is only usable inside a call (`update`, or any function the game calls). Using it at the top level stops the load with
`the world can only be used inside a call`.

### 2.4 The `world` table: the only door

Everything a script does to the game goes through `world`. Methods are called with a **colon** (`world:get(...)`, not `world.get(...)`).
An entity is a **whole number** (`0`, `1`, `42`).

| Call | What it does |
|---|---|
| `world:view{"A", "B"}` | A **list** of the entities that have **all** the components named, by ascending number. The list is taken at that moment: removing entities inside the loop is safe |
| `world:get(e, "A")` | The component as a table of fields, `{points = 80, regen = 0.5}`. `nil` if `e` does not have `A` |
| `world:get(e, "A", "field")` | One field's value (an error if the field does not exist) |
| `world:set(e, "A", "field", v)` | Writes one field. **Adds the component** if the entity has none |
| `world:set(e, "A", {f1 = v1, f2 = v2})` | Writes several fields. **All or nothing**: if one is wrong, nothing changes, not even the valid ones |
| `world:has(e, "A")` | `true` or `false` |
| `world:add(e, "A")` / `world:add(e, "A", {f = v})` | Adds the component with the game's defaults (and sets these fields) |
| `world:drop(e, "A")` | Takes the component off (the entity stays) |
| `world:spawn{A = {...}, B = {...}}` | Makes an entity with these components, returns its number. At least one component. All or nothing |
| `world:remove(e)` | Removes the entity and all its components |
| `world:components(e)` | The list of names of the described components that `e` has |

```lua
function update(dt)
    for _, e in ipairs(world:view{"Transform2D", "Enemy"}) do
        local position = world:get(e, "Transform2D", "position")        -- {x, y}
        local x = position[1] - world:get(e, "Enemy", "speed") * dt
        if x < -10 then
            world:remove(e)                                              -- out of the screen
        else
            world:set(e, "Transform2D", "position", {x, position[2]})
        end
    end
end
```

Things that surprise people:

- **`world:view` gives a list, not an iterator.** Write `for _, e in ipairs(world:view{...})`, not `pairs`.
- **`remove` is for entities that exist.** An entity that has none of the game's described components is taken for already
  gone: `world:remove(e)` twice stops the script with `the entity 0 has no component: it is gone already` (removing twice
  would hand the same number to two new entities). For the same reason `world:spawn{}` with no component is refused.
- **An entity's number can be given to a new entity** once the first one is removed. Do not keep an entity number in a
  variable for many ticks without checking that it is still *yours*: `if world:get(e, "Enemy") then ... end`.
- **State belongs in components, not in Lua variables,** when it must survive a hot reload or be seen by the game (see
  [2.9](#29-hot-reload-edit-while-it-runs)). A `local timer` is fine for a countdown that may restart.

### 2.5 Values: what Lua sends and receives

A value is what the registry writes in JSON. The game's field type decides what is accepted:

| Field of the game | In Lua | Notes |
|---|---|---|
| `bool` | `true` / `false` | |
| integers | whole number (`5`, `3.0`) | `1.5` is refused: `Health.points is a whole number`. Out of range is refused |
| `float`, `double` | any number | A whole value comes **back** as an integer (`2.0` reads as `2`): `math.type` tells |
| `std::string` | string | |
| an `enum` | its **name**, a string | `"Kinematic"`: the game gave the names. An unknown name is refused |
| `Vec2` | `{x, y}` | A list of 2 numbers. `position[1]` is x |
| `Vec3` / `Vec4` | `{x, y, z}` / `{x, y, z, w}` | Vec4 can be a quaternion or a colour |
| an entity (`entityField`) | its number | |

A table you give is **a list** (`{1, 2, 3}`) **or a record with names** (`{points = 5}`), never a mix: `{1, a = 2}` is refused
with `a table of the game is a list (1, 2, 3...) or has names for its keys, not a mix`. Tables nested deeper than 8 levels (or
that contain themselves) are refused. Functions, userdata and `nil` in a field are refused.

Limits written by the game are enforced: `{.min = 0, .max = 1000}` on `points` means `world:set(e, "Health", "points", 5000)`
stops with `Health.points: 5000 is outside 0.000000 to 1000.000000`. Floats in components are 32 bits: `0.1` reads back as
`0.10000000149011612`. Compare with a margin, not with `==`.

### 2.6 Several scripts and several mods

**Every mod has its own globals, and every mod's `update` runs.** You write `function update(dt)` and you do not have to know what other
mods are installed: nothing you define can replace what another mod defined, and the engine calls the `update` of **each** mod, one
after the other, in load order (the mods by `depends`, then by name).

```lua
-- mods/golden/scripts/golden.lua            -- mods/waves/scripts/waves.lua
timer = 0                                    timer = 100          -- the same name: no clash, each mod's `timer` is its own
function update(dt) ... end                  function update(dt) ... end        -- both run, golden first
```

What belongs to whom:

| | Visible to |
|---|---|
| a `local` | the file that wrote it |
| a global (`timer = 0`, `function update`, `function helper`) | **all the files of the same mod**, and nobody else |
| `_G` | the mod's own globals (`_G.x = 1` stays in the mod) |
| `world`, `print`, `math`, `string`, `table`, `utf8`, `coroutine`, `pairs`... | everyone has **its own copy**: `math.floor = nil` or `world.get = nil` stays in the mod |

So the files of one mod (`scripts/a.lua`, `scripts/more/b.lua`) share their globals in load order (files by path name): `a.lua` can define
a `helper` that `b.lua` calls, and a global `update` defined in two files of the same mod is replaced by the later file's. Two mods can
never do that to each other.

**A failure stays in its mod.** If `golden`'s `update` fails (an error, the budget), the game logs it and stops calling **`golden`**; the other
mods go on.

What mods **cannot** do is call each other's functions, or read each other's variables: they meet in the components of the game, which is
the point. A mod that wants to give something to another writes it in a component the game described (a `Config` entity, a `Hit` marker), and
the other reads it with `world:get`. The standard tables are not shared either: each mod has its own copy of `math`, `string`, `table`, `utf8`, `coroutine` and `world`, so a mod that writes `math.floor = nil` breaks it for itself only.

### 2.7 Time, randomness and "the same every time"

The game can run the same tick twice (prediction, a replay) and expects the same result. So scripts have **no clock**: there
is no `os.time`, no `os.clock`. You count time with `dt`:

```lua
local timer = 0.0
function update(dt)
    timer = timer + dt              -- 1/60 each tick: 5 seconds is 300 ticks
    if timer >= 5 then timer = timer - 5 ... end
end
```

`math.random` is the engine's generator (PCG32), not Lua's. The game seeds it from the tick number before each call, so the
same tick rolls the same numbers on every machine:

```lua
math.random()          -- a number in [0, 1)
math.random(6)         -- a whole number from 1 to 6
math.random(-5, 5)     -- a whole number from -5 to 5      (the interval must fit in 32 bits, and m <= n)
```

Do not call `math.randomseed` unless you want your own sequence (the game reseeds before the next call anyway).

One more rule for "the same every time": the order of `pairs(t)` over a table with **string keys** is the same in every run of
the program, but it is Lua's choice. If the order changes a result (who gets hit first), **sort the keys**. `world:view` and
`world:components` are already in a fixed order.

### 2.8 What a script cannot do

| Not there | Why |
|---|---|
| `io`, `os`, `package`, `debug` | No files, no processes, no environment, no loading of other code |
| `dofile`, `loadfile`, `load`, `require`, `collectgarbage` | A script cannot run code it did not ship with, or fiddle with the collector |
| bytecode | Only text is loaded: `string.dump` output cannot be run |
| a clock | Replays must be exact |
| the game's C++ | It only sees what the game described |
| the network | Not a thing in the sandbox. What a **client** sends the room is its inputs, nothing a script wrote |

You do have `string`, `table`, `math`, `utf8`, `coroutine`, and the base functions (`pairs`, `ipairs`, `pcall`, `error`, `assert`,
`type`, `tostring`, `tonumber`, `select`, `setmetatable`...). Coroutines work; they count against the same budget.

**The budget.** One load, or one call of `update`, may run **1,000,000 Lua instructions** (the game can change it). A loop
of a few hundred thousand simple iterations is enough to reach it. Over it, the script is stopped:

```
mods/golden/scripts/golden.lua:14: the script ran over its budget of 1000000 instructions
```

`pcall` does not catch it, and neither does a coroutine. **Memory** is capped too, for each mod (16 MiB by default) and for all of them (64 MiB): a script that builds a
huge table or `string.rep("x", 1e9)` fails with Lua's `not enough memory`, and only its mod is stopped.

### 2.9 Hot reload: edit while it runs

If the game turns it on (Catch does not; see [3.4](#34-hot-reload-and-the---dev-switch)), the engine looks at the **date and size** of
every script file a few times a second. When one changes, **all** the scripts are loaded again, in order, in a **new** Lua state:

- Variables kept by the scripts are **lost** (`local timer` restarts at 0). Keep what must survive in components.
- If the new version does not load (a syntax error), **nothing changes**: the old scripts keep running and the error is logged
  once. Fix the file, save, and it loads.
- If the old scripts had been stopped by an error, a successful reload makes them run again.

Only the files that were loaded at the start are watched. A **new** script file needs a restart, and so does a `data/` file
(it is read once at the start).

### 2.10 Testing a mod without the game

A mod can be tested with no window, the way the engine's own tests do it (see [3.6](#36-testing-a-mod-with-no-window)): load the script, build a
world, call `update` for a few seconds, look at the world.

### 2.11 Recipes

**Do something every N seconds** (a `local` timer, subtract rather than reset to 0, or the time left over is lost):

```lua
local EVERY, timer = 2.0, 0.0
function update(dt)
    timer = timer + dt
    if timer >= EVERY then
        timer = timer - EVERY
        world:spawn{ Transform2D = {position = {10, math.random(-5, 5)}}, Enemy = {speed = 2 + math.random()} }
    end
end
```

**Change every entity that has two components** (a damage over time, one point per tick):

```lua
for _, e in ipairs(world:view{"Health", "Burning"}) do
    local points = world:get(e, "Health", "points") - 1
    if points <= 0 then world:remove(e) else world:set(e, "Health", "points", points) end
end
```

**Add a component only if it is not there:** `world:add(e, "Burning")` does nothing if the entity already has it.

**Read a mod's setting from a component** instead of a global: put the setting in a component the game described (a `Config` entity),
read it with `world:get`. It survives a hot reload and the game can show it.

---

## 3 Making your game moddable

### 3.1 Describe what a mod may touch

A script sees **only** the components you describe. Describe a component once, with its fields:

```cpp
struct Health
{
    int         points = 100;                      // components must be default-constructible aggregates
    float       regen = 0.5f;
    Mood        mood = Mood::Calm;                 // an enum
    kuge::Vec2  home{0.0f, 0.0f};
    kw::Entity  target = 0;                        // an entity
};

kuge::ComponentRegistry registry;                  // an object of the game, not a global
kuge::describeCore(registry);                      // Transform2D, Transform3D, Collider3D, Body3D, Character3D
registry.describe<Health>("Health", {
    kuge::field<Health>("points", &Health::points, {.min = 0, .max = 1000, .help = "Hit points"}),
    kuge::field<Health>("regen", &Health::regen),
    kuge::field<Health>("mood", &Health::mood, {.names = {"Calm", "Angry"}}),       // the names of the enum, from 0
    kuge::field<Health>("home", &Health::home),
    kuge::entityField<Health>("target", &Health::target),
});
```

- A field is a `bool`, an integer, a `float`/`double`, a `std::string`, an enum (**say its names**: C++ cannot), a `Vec2`, a `Vec3`
  or a `Vec4`; `entityField` for an entity. Anything else is a compile error.
- `FieldOptions`: `min` and `max` for numbers (refused outside), `help` (for an editor), `names` for an enum.
- The name is what the script writes: `"Health"`, `"points"`. Keep them stable, a mod depends on them.
- `describeCore` gives `Transform2D` (`position`, `rotation`, `scale`), `Transform3D` (`translation`, `rotation`, `scale`), `Collider3D`,
  `Body3D` and `Character3D` with their usual fields and limits.

**What to describe, and what not.** Describing is **granting**. A script that can write `Health.points` can kill the player.
Do not describe what must not be touched: the score, the player's account, anything that decides who wins. Describe the
*ingredients* of behaviours (enemies, projectiles, weapons) and let the C++ keep the rules that matter. Use `min`/`max`
so that a mod cannot write absurd values.

**Dress, don't expose.** Catch does not describe `Sprite`. A script creates a `Faller`, and a game system (`Dress`) gives every
`Faller` without a `Sprite` its look. The mod author needs no knowledge of the renderer, and cannot break it.

### 3.2 Load the mods and run the scripts

```cpp
const kuge::Mods mods = kuge::Mods::scan("mods");                 // @throw ModError: a cycle, a missing dependency, a bad mod.json (names the mod)
kuge::AssetRoots roots("."); mods.apply(roots);                    // data and resources: the mods on top of the game
auto registry = std::make_shared<kuge::ComponentRegistry>();       // ...described as in 3.1
auto scripts = std::make_shared<kuge::ScriptEngine>(*registry);    // the registry must outlive the engine
for (const auto& script : mods.scripts()) {                        // scripts/*.lua of each mod, in load order
    scripts->load(script.file, script.mod);                        // the mod's name is its scope (its own globals); @throw ScriptError: "mods/golden/scripts/golden.lua:14: ..."
}
// in the scene, on the Fixed schedule:
addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<kuge::ScriptSystem>(scripts));
```

Do the `scan` and the `load`s **before opening the window**: a mod with a mistake is then said with its file and line, and the
game does not start half way (Catch does exactly this in `main()`).

`ScriptSystem` is the part that runs every tick:

1. reads the clock (`kuge::Time`): `dt` is the fixed tick; without a `Time` in the world it uses 1/60;
2. seeds `math.random` with `seedFor(0, tick, seedSalt)`;
3. calls the `update(dt)` (or `ScriptSystemOptions::function`) of **every mod that defines it**, in load order;
4. if one mod's call fails, **logs once** (`the script of 'golden' is stopped, the game goes on: ...`) and stops calling **that mod**, until
   the scripts are reloaded or `restart()`. The other mods go on. `stopped()` is true when no mod is left to call.

A game with no mods that loads its own scripts gives them no scope (`scripts->load(file)`): they are then one scope, and share their globals.

Which **stage**? Scripts that decide things (spawn, move, hurt) go in `kuge::stage::Simulation`, **after** the game's own systems that
they build on and **before** the ones that react to them (in Catch: the game's `Rain`, then the scripts, then `Fall`). A script that
reacts to what physics found goes in `kuge::stage::Late`. You can add **several** `ScriptSystem`s on the same engine with different
function names, in different stages:

```cpp
addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<kuge::ScriptSystem>(scripts));
addSystem(kw::Schedule::Fixed, kuge::stage::Late, std::make_unique<kuge::ScriptSystem>(scripts, kuge::ScriptSystemOptions{.function = "late_update"}));
```

Use the Fixed schedule: `dt` is the fixed tick. Not the Frame schedule.

### 3.3 Who runs which script: the server, the client

KUGE gives no flag: **each side builds its own engine with its own registry**, and that is the whole rule.

- The **room** (the authority) runs the scripts that change the rules, with a registry of every component the game is willing to expose.
- A **client** that runs scripts (for the interface and the effects) gets a registry of the components it **owns**, the ones that
  are not replicated. A script cannot name what it was not given. A replicated component written on a client would be overwritten by the next snapshot anyway.
- A client sends the room its **inputs**. Never what a script wrote.

Do not run the same gameplay script on the room and on a predicting client unless you mean to predict it: both would have to
see the same world and the same tick, which is what the seeding is for, but a script is as deterministic as the data it reads.

### 3.4 Hot reload and the `--dev` switch

```cpp
kuge::ScriptSystemOptions{.reloadEvery = dev ? 30 : 0}            // look at the files every 30 ticks (twice a second); 0: never
```

The engine has no "dev mode": **the game decides** (a `--dev` flag, a build type). Leave it at 0 in a shipped game: a script file
that changes under a running game also changes its fingerprint (3.5). The system logs `the scripts were reloaded`, or
`the scripts were not reloaded, the old ones go on: <why>`.

You can also call `scripts->reloadChanged()` yourself (it returns `{reloaded, error}`), and `ScriptSystem::restart()` lets a stopped
system run again.

### 3.5 Mods in multiplayer

In multiplayer **the server decides** which mods are in play. `Mods::fingerprint()` is a 64-bit hash of the mods' names,
versions, **every file and its bytes** (the scripts are files like the others). The client sends it when it joins
(`MatchmakingClient::setMods()`), and a client whose fingerprint is not the server's gets `JoinError::Mods`. No mod gives 0.

So a script is not "trusted by the client": the client must have the **same** scripts as the room, byte for byte, or it is not let in.

### 3.6 Testing a mod with no window

The engine's own tests do it this way. A mod's script, a world and a few seconds:

```cpp
kuge::ComponentRegistry registry;
kuge::describeCore(registry);
registry.describe<Faller>("Faller", {kuge::field<Faller>("speed", &Faller::speed), kuge::field<Faller>("value", &Faller::value)});

kuge::ScriptEngine scripts(registry);
scripts.load("mods/golden/scripts/golden.lua");                    // @throw ScriptError

kw::World world;
const std::array<double, 1> dt{1.0 / 60.0};
for (int tick = 0; tick < 720; ++tick) {                           // twelve seconds
    scripts.seed(kuge::seedFor(0, tick));                          // what ScriptSystem does
    const auto result = scripts.call(world, "update", dt);         // never throws for a script's mistake
    assert(result.ok);                                             // ...it says why not: result.message
}
assert(registry.entitiesWith(world, {"Faller"}).size() == 2);      // two gold coins in twelve seconds (no game here to catch them)
```

`tests/script/` has more, and `modding_smoke` plays the whole example game with and without its mod.

### 3.7 Reference

**`ScriptOptions`** (the engine):

| Field | Default | Meaning |
|---|---|---|
| `seed` | 1 | First seed of `math.random` |
| `budget` | 1,000,000 | Lua instructions that one `load()` or `call()` may run |
| `memory` | 64 MiB | Bytes all the scripts together may hold |
| `modMemory` | 16 MiB | Bytes the scripts of one scope (one mod) may hold. `ScriptEngine::memoryUsed(scope)` says how much one holds |
| `maxEntity` | 1,048,576 | A script that names an entity above it is stopped (the world grows to hold any number it is given) |

**`ScriptSystemOptions`**:

| Field | Default | Meaning |
|---|---|---|
| `function` | `"update"` | The global function called at each tick |
| `seedSalt` | 0 | Mixed into the seed of each tick, so two systems do not roll the same numbers |
| `reloadEvery` | 0 | Ticks between two looks at the files (0: never) |

**`ScriptEngine`**: `load(path[, scope])`, `loadString(code, name[, scope])` (both `@throw ScriptError`; the scope is a mod's name, empty
for a game's own scripts), `scopes(function)` (who defines it, in load order), `has(function)`, `call(world, function, args)` (the function
of **every** scope; `{ok, message}` with one line per failure), `call(world, scope, function, args)` (one scope), `seed(n)`, `onPrint(sink)`
(where `print` goes; `ScriptSystem` sends it to the logger, category `script`), `reloadChanged()` → `{reloaded, error}`.

**`ScriptSystem`**: `stopped()`, `restart()`.

**`Mods`**: `scan(folder[, version])`, `ordered()`, `apply(roots)`, `overrides(gameRoot)`, `scripts()` (a list of `{mod, file}`), `fingerprint()`.

**`ComponentRegistry`**: `describe<C>(name, fields)`, `byName`, `has`/`add`/`remove`, `get`/`set`, `componentsOf`, `entitiesWith(world, names)`,
`toJson`/`fromJson`, `entityToJson`/`entityFromJson` (the same registry serves an editor, a prefab file, a timeline...).

---

## 4 How it works inside

For people who change the engine, or who want to know what to trust.

- **One Lua state per `ScriptEngine`** (Lua 5.5, from vcpkg: the feature `script`, `KUGE_BUILD_SCRIPT`, ON by default). Only the
  libraries `base`, `table`, `string`, `math`, `utf8`, `coroutine` are opened, then `dofile`, `loadfile`, `load`, `collectgarbage` are
  removed. Chunks are loaded in text mode only (`"t"`).
- **Scopes** are what lets mods live together in that one state. Each scope is a table of globals: a copy of the engine's globals, where
  the libraries and `world` are copies too (shallow: they hold functions and strings), with no metatable, and holding `_G` (itself). So
  nothing a mod writes reaches another, and there is no door to the originals: `getmetatable(_G)` is `nil`, and the metatable of strings
  (whose `__index` is the engine's `string` table) is closed with `__metatable`, so `getmetatable("")` gives a text, not a table. A chunk is
  loaded with the scope's table as its `_ENV` (the first upvalue of a chunk). Calling a function of a scope is a `rawget` in that table, so
  only what the scope itself defined is found: `call(world, "update")` is a loop over the scopes in load order. Which scopes failed is the
  **system's** business (`ScriptSystem`), not the engine's: the engine keeps no memory of failures, so a test or a game can call anything again.
- **The budget** is a Lua *count hook*: every 1000 instructions it adds to a counter. Past the budget it raises an error **and
  from then on fails at every single instruction**, in every coroutine, so `pcall` cannot swallow it and loop. The count restarts at each `load()` or `call()`.
- **Memory** goes through a custom allocator that refuses a request past the limit; Lua then raises `not enough memory`. Each block has an
  8 byte header with its owner: the scope that was running when it was allocated (0, the engine, for the Lua stack, the table of strings and
  what `makeImpl` builds). A block is given back to its owner whoever frees it (the collector runs in whichever mod is running), and a
  request is refused if it would take the total past `memory` or the owner's share past `modMemory`. The engine's own share is held by the
  total only. Objects are never shared between mods (each has its copy of the globals), so an owner is its mod's.
- **The `world` methods** convert the Lua arguments to `kuge::Json`, and let the registry validate and write (`fromJson`: checks
  everything first and rolls back if a write fails). Reading is the reverse (`toJson`, then pushed as Lua tables).
- **Errors cross the C boundary with `longjmp`**: Lua is C. So every method of `world` is a lambda that throws C++ exceptions, caught
  by a wrapper (`guarded()`) that pushes the message **after** the lambda is gone, and only then raises the Lua error, so no
  C++ object is left to leak. The message gets `file:line:` of the script's call. (If you add a method: no `luaL_error` inside the lambda.)
- **`pairs` order**: Lua 5.5 takes a seed for its string hashing; KUGE gives a fixed one, so the order does not change from
  one run to the next.
- **Hot reload** builds a *whole new* state (the old one is kept until the new one has loaded every source without error), then swaps.
  The dates and sizes of the files are recorded even when a reload fails, so a bad file is reported once.
- **`ScriptSystem`** is a normal `kw::ISystem`: the engine's scheduler, stages and `Time` resource are used as for any other system.

---

## 5 Limits: what is not there, and what the sandbox does not stop

**Not stopped by the sandbox**

- A **single, very long call of Lua's own library** runs without counting instructions: a pathological `string.find` pattern can take
  long. (`string.rep` and big tables are stopped by the memory limit.)
- The memory limit covers what Lua holds, not the game's own memory. A script can make the game slow without an infinite loop,
  for example by spawning thousands of entities each tick: the only limit on that is the instruction budget. Mods are
  *sandboxed*, not *trusted*: for a public game, think about which components you describe and which limits (`min`, `max`) you write.

**Not built (yet)**

- **Mods cannot call each other**; they meet in components. All mods run in one Lua state, but each has its own memory limit
  (`modMemory`): a mod that eats its share fails alone. What is not counted for a mod: the Lua stack and the table of strings (the engine's,
  held by the total `memory`), and the game's own memory. The collector is one for the state, so a mod that makes a lot of garbage makes
  every mod pay a little time.
- **Prefab files**: `world:spawn("enemy")` from a file waits for the scene and prefab files of the editor epic (#98): today a script
  gives the components in the call.
- **Events** (a script that reacts to a collision, a death): scripts are polled each tick. The game can expose an event as a
  component (a `Hit` entity with `Transform2D`) that scripts read.
- **Reloading `data/` files, and new script files**, without a restart.
- **Speed**: each `get`/`set` converts values through a JSON-like form. It is fine for hundreds of entities a tick; the cost has not been
  measured for tens of thousands, where the loop belongs in C++.
- **Windows**: the module builds with MSVC and the example's smoke test runs on CI; the unit tests of `kuge-script` are kronklab tests and do not run there.

---

## 6 When something goes wrong

Messages are real ones. A script error always begins with `file:line:`.

| You see | What it means | Fix |
|---|---|---|
| `mod.lua:3: there is no component 'Mana'` | The game did not describe it (or a typo) | Check the names the game gives; names are case-sensitive |
| `mod.lua:3: Health has no field 'mana'` | The component has no such field | Look at the game's `describe` |
| `mod.lua:3: Health.points is a whole number` | You gave `"many"`, `nil`, or `1.5` to an integer | Give a whole number (`math.floor`) |
| `mod.lua:3: Health.points: 5000 is outside 0.000000 to 1000.000000` | Past the limits the game wrote | Stay inside |
| `mod.lua:3: Transform2D.position is a pair [x, y]` | A `Vec2` needs two numbers | `{x, y}` |
| `mod.lua:3: a table of the game is a list (1, 2, 3...) or has names for its keys, not a mix` | `{1, a = 2}` | Choose one |
| `mod.lua:3: argument #1 must be an entity: a whole number from 0 to 1048576` | You passed a string, a negative or a huge number as an entity | Use the numbers `view`/`spawn` gave you |
| `mod.lua:3: argument #2 must be the name of a component` | A call lacks its component name | `world:has(e, "Health")` |
| `mod.lua:3: the component 'Nope' is not described` | `world:view{"Nope"}` | Same as the first line |
| `mod.lua:3: the entity 0 has no component: it is gone already` | `world:remove` on an entity that is already removed | Check with `world:get(e, "X")` first |
| `the world can only be used inside a call (not while a script is loaded)` | `world:` at the top level of a file | Put it in `update` |
| `mod.lua:14: the script ran over its budget of 1000000 instructions` | A loop that is too long or never ends | Do less per tick; spread work over ticks |
| `not enough memory` | Past the memory limit of your mod (or of all the mods) | Build smaller tables, keep less in globals |
| `mod.lua:3: attempt to index a nil value (field 'x')` | An ordinary Lua error | `world:get` gave `nil`: the entity has no such component |
| `(the error is a table)` | `error({...})` with something that is not a string | `error("message")` |
| `there is no function 'update'` | No mod defined a global `update` | Define it (a `local function update` is private to the file and does not count) |
| `the scripts were not reloaded, the old ones go on: ...` | A hot reload met an error | Fix the file; it loads on the next save |
| `JoinError::Mods` (a client cannot join) | The client's mods differ from the server's, scripts included | Same mods, same bytes |
| The mod does nothing, no error | It was stopped earlier (look for `is stopped` in the log), or its `update` is not where you think | `print("here")`; check the log from the start |

**`print` is your debugger.** It goes to the game's log as `[ INFO ] ... [script] <text>`. A mod that was **stopped** by a failure
says so once: `[ ERROR ] ... [script] the script of 'golden' is stopped, the game goes on: <message>`. After that, that mod is not called
until a successful reload or a `restart()`, so a mod that fails *at every tick* does not fill the log; the other mods go on.
