extern "C" {
    #include "kronklab/kronklab.h"
}
#include "ScriptEngine.hpp"
#include "Transform2D.hpp"
#include <string>
#include <vector>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    struct Health
    {
        int         points = 100;
        float       regen = 0.5f;
        std::string label = "npc";
    };

    struct Enemy
    {
        int level = 1;
    };

    // The registry is filled before the engine is made (it is declared first)
    struct Described
    {
        kuge::ComponentRegistry registry;

        Described()
        {
            registry.describe<Health>("Health", {
                kuge::field<Health>("points", &Health::points, {.min = 0, .max = 1000}),
                kuge::field<Health>("regen", &Health::regen),
                kuge::field<Health>("label", &Health::label),
            });
            registry.describe<Enemy>("Enemy", {kuge::field<Enemy>("level", &Enemy::level)});
            kuge::describeCore(registry);
        }
    };

    struct Fixture : Described
    {
        kw::World           world;
        kuge::ScriptEngine  scripts;

        Fixture() : scripts(registry) {}

        bool ok(const std::string& function = "update") { return scripts.call(world, function).ok; }
        std::string why(const std::string& function = "update") { return scripts.call(world, function).message; }
    };

    bool contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }
}

Test(script, reads_and_sets_by_name)
{
    Fixture f;

    f.world.spawn(Health{});
    f.world.spawn(Health{});
    f.scripts.loadString(R"(
        function update()
            world:set(1, "Health", "points", 50)
            world:set(1, "Health", {regen = 2, label = "boss"})
            seen = world:get(1, "Health", "points")
        end
        function check() assert(seen == 50 and world:get(0, "Enemy") == nil and world:has(1, "Health")) end
    )", "ai.lua");
    Assert(f.ok(), "the call goes: %s", f.why().c_str());
    Assert(f.world.get<Health>(1).points == 50 && f.world.get<Health>(1).regen == 2.0f && f.world.get<Health>(1).label == "boss", "fields written by name");
    Assert(f.world.get<Health>(0).points == 100, "another entity is not touched");
    Assert(f.ok("check"), "get gives the value back, nil for a component the entity has not: %s", f.why("check").c_str());
}

Test(script, a_wrong_value_stops)
{
    Fixture f;

    f.world.spawn(Health{});
    f.scripts.loadString(R"(
        function bad_type() world:set(0, "Health", {points = "many"}) end
        function bad_limit() world:set(0, "Health", {points = 5000}) end
        function bad_field() world:set(0, "Health", "mana", 1) end
        function bad_name() world:get(0, "Mana") end
        function atomic() world:set(0, "Health", {regen = 9, points = -1}) end
    )", "rules.lua");
    for (const char* function : {"bad_type", "bad_limit", "bad_field", "bad_name", "atomic"}) {
        const auto result = f.scripts.call(f.world, function);

        Assert(!result.ok && contains(result.message, "rules.lua:"), "%s is stopped with the file and line: %s", function, result.message.c_str());
    }
    Assert(contains(f.why("bad_limit"), "points"), "the message names the field");
    Assert(contains(f.why("bad_name"), "Mana"), "and the component");
    Assert(f.world.get<Health>(0).points == 100 && f.world.get<Health>(0).regen == 0.5f, "nothing changed, not even the field that was valid");
}

Test(script, view_spawn_remove)
{
    Fixture f;

    f.scripts.loadString(R"(
        function update()
            local a = world:spawn{Health = {points = 10}, Enemy = {level = 3}}
            local b = world:spawn{Health = {points = 20}}
            local c = world:spawn{Health = {points = 30}, Enemy = {}}
            local enemies = world:view{"Health", "Enemy"}
            assert(#enemies == 2 and enemies[1] == a and enemies[2] == c, "view gives them by ascending number")
            for _, e in ipairs(enemies) do world:remove(e) end
            assert(#world:view{"Enemy"} == 0 and #world:view{"Health"} == 1)
            world:add(b, "Enemy", {level = 7})
            world:drop(b, "Health")
            local names = world:components(b)
            assert(#names == 1 and names[1] == "Enemy" and world:get(b, "Enemy", "level") == 7)
        end
    )", "spawn.lua");
    Assert(f.ok(), "%s", f.why().c_str());
    Assert(f.registry.entitiesWith(f.world, {"Enemy"}).size() == 1, "the entity that is left");
}

Test(script, spawn_is_checked)
{
    Fixture f;

    f.scripts.loadString(R"(
        function empty() world:spawn{} end
        function wrong() world:spawn{Health = {points = 1}, Enemy = {level = "x"}} end
        function twice() local e = world:spawn{Enemy = {}} world:remove(e) world:remove(e) end
    )", "s.lua");
    Assert(!f.ok("empty"), "an entity with no component is refused");
    Assert(!f.ok("wrong"), "a wrong component refuses the whole entity");
    Assert(f.registry.entitiesWith(f.world, {"Health"}).empty() && f.registry.entitiesWith(f.world, {"Enemy"}).empty(), "and leaves nothing behind");
    Assert(!f.ok("twice") && contains(f.why("twice"), "gone already"), "removing twice would hand one number to two entities");
}

Test(script, budget_stops_a_loop)
{
    Fixture f;

    f.scripts.loadString(R"(
        function forever() while true do end end
        function swallowed() while true do pcall(function() while true do end end) end end
        function fine() count = (count or 0) + 1 end
        function coroutines() local co = coroutine.wrap(function() while true do end end) co() end
    )", "loop.lua");
    auto result = f.scripts.call(f.world, "forever");

    Assert(!result.ok && contains(result.message, "budget") && contains(result.message, "loop.lua:2"), "stopped, and told where: %s", result.message.c_str());
    result = f.scripts.call(f.world, "swallowed");
    Assert(!result.ok && contains(result.message, "budget"), "pcall does not catch it: %s", result.message.c_str());
    Assert(f.ok("fine") && f.ok("fine"), "the next calls run");
    Assert(!f.ok("coroutines"), "a coroutine is counted too");
    Assert(!f.scripts.call(f.world, "nothing").ok, "a function that is not there is said");
}

Test(script, a_loop_at_load_is_stopped)
{
    Fixture f;

    try {
        f.scripts.loadString("while true do end", "top.lua");
        Assert(false, "a loop at the top level must be stopped");
    } catch (const kuge::ScriptError& e) {
        Assert(contains(e.what(), "top.lua:1"), "%s", e.what());
    }
}

Test(script, memory_is_limited)
{
    kuge::ComponentRegistry registry;
    kuge::ScriptOptions options;
    kw::World world;

    options.memory = 1u << 20;
    kuge::ScriptEngine scripts(registry, options);
    scripts.loadString("function hog() local s = string.rep('x', 100000000) end function table_hog() local t = {} for i = 1, 1e7 do t[i] = i end end", "m.lua");
    Assert(!scripts.call(world, "hog").ok, "a string of 100 MB is refused");
    Assert(!scripts.call(world, "table_hog").ok, "so is a table that grows");
}

Test(script, the_sandbox_is_closed)
{
    Fixture f;

    f.scripts.loadString(R"(
        function update()
            for _, name in ipairs{"io", "os", "package", "debug", "require", "dofile", "loadfile", "load", "collectgarbage"} do
                assert(_G[name] == nil, name .. " must not exist")
            end
            assert(string.upper("a") == "A" and table.concat({1, 2}) == "12" and math.floor(1.5) == 1 and utf8.char(65) == "A")
        end
    )", "sandbox.lua");
    Assert(f.ok(), "%s", f.why().c_str());
    try {
        f.scripts.loadString("\x1b" "Lua bytecode", "bin.lua");
        Assert(false, "bytecode must be refused");
    } catch (const kuge::ScriptError&) {
    }
    try {
        f.scripts.load("/does/not/exist.lua");
        Assert(false, "a missing file must be said");
    } catch (const kuge::ScriptError& e) {
        Assert(contains(e.what(), "exist.lua"), "%s", e.what());
    }
}

Test(script, errors_name_file_and_line)
{
    Fixture f;

    try {
        f.scripts.loadString("local x = 1\nlocal y = = 2\n", "syntax.lua");
        Assert(false, "a syntax error must be raised");
    } catch (const kuge::ScriptError& e) {
        Assert(contains(e.what(), "syntax.lua:2:"), "%s", e.what());
    }
    f.scripts.loadString("function update()\n  local t = nil\n  return t.x\nend", "runtime.lua");
    Assert(contains(f.why(), "runtime.lua:3:"), "%s", f.why().c_str());
}

Test(script, the_world_waits_for_a_call)
{
    Fixture f;

    try {
        f.scripts.loadString("world:spawn{Enemy = {}}", "early.lua");
        Assert(false, "the world is not there while a script loads");
    } catch (const kuge::ScriptError& e) {
        Assert(contains(e.what(), "inside a call"), "%s", e.what());
    }
}

Test(script, huge_entity_is_refused)
{
    Fixture f;

    f.scripts.loadString("function update() world:has(2^40 // 1, 'Health') end function neg() world:has(-1, 'Health') end function frac() world:has(1.5, 'Health') end", "e.lua");
    Assert(!f.ok(), "an entity number that would make the world grow for ever is refused");
    Assert(!f.ok("neg") && !f.ok("frac"), "negative and fractional too");
}

Test(script, random_follows_the_seed)
{
    kuge::ComponentRegistry registry;
    kw::World world;
    const std::string code = "function roll() local out = {} for i = 1, 20 do out[#out + 1] = math.random(1, 6) end local f = math.random() assert(f >= 0 and f < 1) "
                             "print(table.concat(out, ',')) end";
    std::string first;
    std::string again;

    for (std::string* result : {&first, &again}) {
        kuge::ScriptOptions options;

        options.seed = 42;
        kuge::ScriptEngine scripts(registry, options);

        scripts.onPrint([&](const std::string& line) { *result = line; });
        scripts.loadString(code, "r.lua");
        Assert(scripts.call(world, "roll").ok, "the script runs");
    }
    Assert(!first.empty() && first == again, "the same seed gives the same numbers: %s / %s", first.c_str(), again.c_str());

    kuge::ScriptEngine scripts(registry);
    std::string other;

    scripts.onPrint([&](const std::string& line) { other = line; });
    scripts.loadString(code, "r.lua");
    scripts.seed(43);
    Assert(scripts.call(world, "roll").ok && other != first, "another seed gives other numbers");
    scripts.seed(42);
    Assert(scripts.call(world, "roll").ok && other == first, "seed() starts the sequence again");
    scripts.loadString("function bad() math.random(5, 1) end function big() math.random(1, 2^40 // 1) end", "b.lua");
    Assert(!scripts.call(world, "bad").ok && !scripts.call(world, "big").ok, "an empty or too large interval is refused");
}

Test(script, has_a_function)
{
    Fixture f;

    f.scripts.loadString("function update() end x = 1", "h.lua");
    Assert(f.scripts.has("update") && !f.scripts.has("x") && !f.scripts.has("nope"), "has() tells functions from the rest");
}
