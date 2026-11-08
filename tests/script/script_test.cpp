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

Test(script, a_mod_has_its_memory)
{
    kuge::ComponentRegistry registry;
    kuge::ScriptOptions options;
    kw::World world;

    options.modMemory = 1u << 20;       // 1 MiB each, 64 MiB for all
    kuge::ScriptEngine scripts(registry, options);
    scripts.loadString("function hog() local t = {} for i = 1, 1e6 do t[i] = i end end function keep() held = string.rep('x', 600000) end", "a.lua", "mod_a");
    scripts.loadString("function keep() held = string.rep('y', 600000) end function light() local t = {1, 2, 3} return #t end", "b.lua", "mod_b");
    Assert(scripts.memoryUsed("nobody") == 0, "a scope that does not exist holds nothing");

    // Each mod holds 600 kB: more than a mod may hold if they shared 1 MiB, less than what one may
    Assert(scripts.call(world, "mod_a", "keep", {}).ok && scripts.call(world, "mod_b", "keep", {}).ok, "two mods hold 600 kB each");
    Assert(scripts.memoryUsed("mod_a") > 600000 && scripts.memoryUsed("mod_a") < (1u << 20), "mod_a holds what it made: %zu", scripts.memoryUsed("mod_a"));
    Assert(scripts.memoryUsed("mod_b") > 600000 && scripts.memoryUsed("mod_b") < (1u << 20), "mod_b too: %zu", scripts.memoryUsed("mod_b"));

    // mod_a goes past its own limit: it fails, mod_b is not touched and can still allocate
    const auto hog = scripts.call(world, "mod_a", "hog", {});

    Assert(!hog.ok && hog.message.find("not enough memory") != std::string::npos, "mod_a is stopped: %s", hog.message.c_str());
    Assert(scripts.memoryUsed("mod_a") <= (1u << 20), "and never held more than its limit: %zu", scripts.memoryUsed("mod_a"));
    Assert(scripts.call(world, "mod_b", "light", {}).ok, "mod_b still runs (and could not hold a second string at once: its limit is its own too)");
    Assert(scripts.memoryUsed("mod_b") > 600000 && scripts.memoryUsed("mod_b") < (1u << 20), "with its own memory: %zu", scripts.memoryUsed("mod_b"));

    // What a mod let go of (the garbage collector gives it back to its owner, whoever is running) is not held any more
    scripts.loadString("held = nil", "a2.lua", "mod_a");
    scripts.loadString("local x = 0", "b2.lua", "mod_b");   // runs mod_b's code: its collector may free blocks of mod_a
    scripts.loadString("for j = 1, 150000 do local t = {j, j, j} end", "b3.lua", "mod_b");
    Assert(scripts.memoryUsed("mod_a") < 300000, "mod_a's string is given back: %zu", scripts.memoryUsed("mod_a"));
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

Test(script, every_mods_update_runs)
{
    Fixture f;

    // Two mods: each has its own globals, so the second `update` does not replace the first, and both run in load order
    f.scripts.loadString("shared = 'a' function update() world:spawn{Enemy = {level = 1}} end", "a.lua", "mod_a");
    f.scripts.loadString("assert(shared == nil) function update() world:spawn{Enemy = {level = 2}} end", "b.lua", "mod_b");
    Assert(f.scripts.scopes("update").size() == 2 && f.scripts.scopes("update")[0] == "mod_a", "two scopes have an update, in load order");
    Assert(f.ok(), "%s", f.why().c_str());
    const auto enemies = f.registry.entitiesWith(f.world, {"Enemy"});

    Assert(enemies.size() == 2 && f.world.get<Enemy>(enemies[0]).level == 1 && f.world.get<Enemy>(enemies[1]).level == 2, "both updates ran, the first mod first");
}

Test(script, globals_of_a_mod_stay_its_own)
{
    Fixture f;

    f.scripts.loadString("counter = 10 _G.leak = 1 function bump() counter = counter + 1 return counter end function update() end", "a.lua", "mod_a");
    f.scripts.loadString("function update() assert(counter == nil and leak == nil and _G == _ENV, 'nothing leaked') end", "b.lua", "mod_b");
    Assert(f.ok(), "%s", f.why().c_str());
    Assert(f.scripts.has("bump") && f.scripts.scopes("bump").size() == 1, "a function of one mod is found in that mod only");
    // The files of a mod do share their globals
    f.scripts.loadString("function twice() return bump() + bump() end function use() assert(twice() == 23) end", "a2.lua", "mod_a");
    Assert(f.ok("use"), "%s", f.why("use").c_str());
    // And the libraries and `world` are there for everyone
    f.scripts.loadString("function update() assert(math.floor(1.5) == 1 and type(world.get) == 'function') end", "c.lua", "mod_c");
    Assert(f.ok(), "%s", f.why().c_str());
}

Test(script, mods_cannot_break_each_other)
{
    Fixture f;

    // Every mod has its own copy of the standard tables and of `world`: what it does to them stays in the mod
    f.scripts.loadString(R"(
        math.floor = nil
        string.upper = function() return "X" end
        table.insert = nil
        world.get = nil
        print = nil
        function update() end
    )", "a.lua", "mod_a");
    f.scripts.loadString(R"(
        function update()
            assert(math.floor(1.5) == 1 and string.upper("a") == "A" and type(table.insert) == "function")
            assert(type(world.get) == "function" and type(print) == "function")
        end
    )", "b.lua", "mod_b");
    Assert(f.ok(), "%s", f.why().c_str());

    // And it cannot reach the real ones: not through the metatable of its globals, nor through the one of strings
    f.scripts.loadString(R"(
        function update()
            assert(type(getmetatable(_G)) ~= "table", "the globals have no door to the shared ones")
            assert(type(getmetatable("")) ~= "table", "the string metatable is closed")
            assert(not pcall(setmetatable, "", {}), "and so is its change")
            assert(("a"):upper() == "A" and ("ab"):rep(2) == "abab", "strings still have their methods")
        end
    )", "c.lua", "mod_c");
    Assert(f.ok(), "%s", f.why().c_str());
}

Test(script, a_failing_mod_stops_alone)
{
    Fixture f;

    f.scripts.loadString("function update() error('mod a is broken') end", "a.lua", "mod_a");
    f.scripts.loadString("function update() world:spawn{Enemy = {}} end", "b.lua", "mod_b");
    const auto result = f.scripts.call(f.world, "update");

    Assert(!result.ok && result.message.find("a.lua:1: mod a is broken") != std::string::npos, "the failure is said: %s", result.message.c_str());
    Assert(f.registry.entitiesWith(f.world, {"Enemy"}).size() == 1, "and the other mod still ran");
    Assert(f.scripts.call(f.world, "mod_b", "update", {}).ok, "one scope can be called alone");
    Assert(!f.scripts.call(f.world, "mod_z", "update", {}).ok, "a scope that does not exist is said");
}

Test(script, the_default_scope_is_shared)
{
    Fixture f;

    // With no scope, the scripts share their globals (what a game that has no mods does with its own files): the last `update` wins
    f.scripts.loadString("x = 1 function update() world:spawn{Enemy = {level = 1}} end", "one.lua");
    f.scripts.loadString("assert(x == 1) function update() world:spawn{Enemy = {level = 2}} end", "two.lua");
    Assert(f.ok(), "%s", f.why().c_str());
    Assert(f.registry.entitiesWith(f.world, {"Enemy"}).size() == 1 && f.world.get<Enemy>(0).level == 2, "one scope, the second update replaced the first");
}

Test(script, has_a_function)
{
    Fixture f;

    f.scripts.loadString("function update() end x = 1", "h.lua");
    Assert(f.scripts.has("update") && !f.scripts.has("x") && !f.scripts.has("nope"), "has() tells functions from the rest");
}
