# 20 Scripts

A mod can change numbers and files ([Mods](../07-data-saves-and-config/README.md)). To add a **behaviour** (an enemy that
dodges, a weapon with an effect that exists nowhere in the game's C++) without a rebuild, KUGE has Lua in an optional
module, `kuge-script`. A game that does not use it does not pay for it: build with `-DKUGE_BUILD_SCRIPT=OFF`, or leave
`lua` out of its `vcpkg.json`.

Lua was chosen over WebAssembly because it is small, well known and in vcpkg. WebAssembly would give a sandbox
for any language, at the price of a heavier runtime and a harder binding.

## A script reads and writes the world by name

A script cannot see your C++ types. It sees the components you **described** to the `ComponentRegistry`
(the same registry an editor or a prefab uses), by name:

```cpp
kuge::ComponentRegistry registry;
kuge::describeCore(registry);                                    // Transform2D, Transform3D, Collider3D...
registry.describe<Health>("Health", {
    kuge::field<Health>("points", &Health::points, {.min = 0, .max = 1000}),
});

kuge::ScriptEngine scripts(registry);
scripts.load("mods/wand/ai.lua");                                // @throw ScriptError: "mods/wand/ai.lua:12: ..."
auto result = scripts.call(world, "update", {dt});               // {ok, message}
if (!result.ok) { log.error("{}", result.message); }             // the script is stopped, the game goes on
```

```lua
function update(dt)
    for _, e in ipairs(world:view{"Health", "Enemy"}) do     -- a list taken now: removing inside the loop is safe
        local h = world:get(e, "Health")                     -- {points = 80}, or nil if the entity has none
        if h.points <= 0 then
            world:remove(e)
        else
            world:set(e, "Health", "points", h.points - 1)   -- or world:set(e, "Health", {points = 1})
        end
    end
    local boss = world:spawn{Health = {points = 500}, Transform2D = {position = {0, 0}}}
end
```

| Method | |
|---|---|
| `world:view{"A", "B"}` | The entities that have both, as a list, by ascending number |
| `world:get(e, "A")` / `world:get(e, "A", "field")` | The fields as a table (or one value); `nil` if `e` has no `A` |
| `world:set(e, "A", "field", v)` / `world:set(e, "A", {field = v, ...})` | Writes; the component is added if missing. All or nothing |
| `world:has(e, "A")`, `world:add(e, "A" [, {...}])`, `world:drop(e, "A")` | Test, add (with the defaults and these fields), take off |
| `world:spawn{A = {...}, B = {...}}` | A new entity, returns its number. At least one component |
| `world:remove(e)` | The entity and all its components |
| `world:components(e)` | The names of the described components it has |

A value is what the registry writes in JSON: a number, a boolean, a string, an enum by its name, a `Vec2` as `{x, y}`,
a `Vec3` as `{x, y, z}`, an entity as its number. A wrong component, field, type or limit stops the script with the
registry's message (`Health.points: 5000 is outside 0 to 1000`) and changes **nothing**.

## The sandbox

A script gets the base library (without `dofile`, `loadfile`, `load`, `collectgarbage`), `string`, `table`, `math`,
`utf8` and `coroutine`. There is no `io`, `os`, `package` or `debug`, and a file must be Lua text, never bytecode.

- **A budget.** A `load()` or a `call()` may run `ScriptOptions::budget` instructions (a million by default). Over it, the
  script is stopped with `"ai.lua:7: the script ran over its budget of 1000000 instructions"`, and the next call runs. `pcall`
  and coroutines do not get around it.
- **A memory limit.** `ScriptOptions::memory` bytes (64 MiB by default); `string.rep('x', 1e9)` fails instead of eating the machine.
- **No clock, a seeded `math.random`.** It is KUGE's `Random` (PCG32): the same seed gives the same numbers on every
  machine. `scripts.seed(seedFor(player, tick))` before the call makes a predicted system agree with the room.
- **Entities are bounded** (`ScriptOptions::maxEntity`): the world grows to hold any number it is given, so a script that names
  `2^40` is stopped.

What the sandbox does **not** stop: a single, very long call of the standard library (a pathological `string.find` pattern runs
without counting instructions), and the order of `pairs` over a table of strings, which is the same in every run but is Lua's
to choose: sort the keys when the order matters to a result. `world:view` and `world:get` are already in a fixed order.

## What is next

This is the first half: the engine. Still to come (see [TODO](../TODO.md)): a `ScriptSystem` that calls
`update(dt)` in a stage you choose and logs what a script prints, the scripts in the mods' fingerprint (the server decides which
scripts run), hot reload in `--dev`, and an example mod.
