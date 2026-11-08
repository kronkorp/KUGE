extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Mods.hpp"
#include "Random.hpp"
#include "ScriptSystem.hpp"
#include "Time.hpp"
#include "Transform2D.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    namespace fs = std::filesystem;

    struct Enemy
    {
        float speed = 1.0f;
    };

    struct Described
    {
        kuge::ComponentRegistry registry;

        Described()
        {
            registry.describe<Enemy>("Enemy", {kuge::field<Enemy>("speed", &Enemy::speed)});
            kuge::describeCore(registry);
        }
    };

    // A folder for the files of a test, removed with it
    struct Folder
    {
        fs::path path = fs::temp_directory_path() / ("kuge_script_" + std::to_string(::getpid()) + "_" + std::to_string(counter()++));

        Folder() { fs::create_directories(path); }
        ~Folder() { std::error_code ignored; fs::remove_all(path, ignored); }

        static int& counter(void) { static int n = 0; return n; }

        fs::path write(const std::string& name, const std::string& text) const
        {
            std::ofstream(path / name, std::ios::binary) << text;
            return path / name;
        }
    };

    struct Game : Described
    {
        kw::World                      world;
        std::shared_ptr<kuge::ScriptEngine> engine = std::make_shared<kuge::ScriptEngine>(registry);

        Game() { world.addResource<kuge::Time>(); }

        void tick(kuge::ScriptSystem& system, int count)
        {
            for (int i = 0; i < count; ++i) {
                world.getResource<kuge::Time>().tick++;
                Assert(system.handle(world), "a script system always stays in the scheduler");
            }
        }

        std::size_t enemies(void) { return registry.entitiesWith(world, {"Enemy"}).size(); }
        std::size_t marks(void) { return registry.entitiesWith(world, {"Transform2D"}).size(); }

        std::vector<float> xs(void)
        {
            std::vector<float> out;

            for (const kw::Entity e : registry.entitiesWith(world, {"Enemy"})) {
                out.push_back(world.get<kuge::Transform2D>(e).position.x);
            }
            return out;
        }
    };
}

Test(script_system, the_example_mod)
{
    const auto mods = kuge::Mods::scan(WAVES_MODS_DIR);
    const auto files = mods.scripts();

    Assert(files.size() == 1 && files[0].filename() == "waves.lua", "the example mod has its script");
    std::vector<float> first;

    for (int run = 0; run < 2; ++run) {
        Game game;

        for (const auto& file : files) {
            game.engine->load(file);
        }
        kuge::ScriptSystem system(game.engine);

        game.tick(system, 400);
        Assert(!system.stopped() && game.enemies() == 3, "an enemy every two seconds: %zu after 6.7 s", game.enemies());
        game.tick(system, 500);
        Assert(!system.stopped() && game.enemies() < 7, "and the ones that are out of the screen are removed: %zu after 15 s", game.enemies());
        Assert(game.enemies() >= 1, "the last ones are still there");
        for (const float x : game.xs()) {
            Assert(x >= -10.0f && x <= 10.0f, "an enemy is on the screen: %f", x);
        }
        if (run == 0) {
            first = game.xs();
        } else {
            Assert(game.xs() == first, "the same ticks give the same enemies in the same places (seeded random, fixed dt)");
        }
    }
}

// The snippet of docs/20-scripts (3.6): a mod tested with no game and no window
Test(script_system, golden_with_no_game)
{
    struct Faller
    {
        float speed = 120.0f;
        int   value = 1;
    };
    kuge::ComponentRegistry registry;

    kuge::describeCore(registry);
    registry.describe<Faller>("Faller", {
        kuge::field<Faller>("speed", &Faller::speed, {.min = 0.0, .max = 1000.0}),
        kuge::field<Faller>("value", &Faller::value, {.min = 1.0, .max = 100.0}),
    });
    const auto mods = kuge::Mods::scan(GOLDEN_MODS_DIR);
    kuge::ScriptEngine scripts(registry);

    for (const auto& file : mods.scripts()) {
        scripts.load(file);
    }
    kw::World world;
    const std::array<double, 1> dt{1.0 / 60.0};

    for (std::uint32_t tick = 0; tick < 720; ++tick) {
        scripts.seed(kuge::seedFor(0, tick));
        const auto result = scripts.call(world, "update", dt);

        Assert(result.ok, "%s", result.message.c_str());
    }
    const auto coins = registry.entitiesWith(world, {"Faller"});

    Assert(coins.size() == 2, "two gold coins in twelve seconds: %zu", coins.size());
    Assert(world.get<Faller>(coins[0]).value == 5 && world.get<Faller>(coins[0]).speed == 240.0f, "worth 5, falling at 240");
}

Test(script_system, a_failure_stops_it)
{
    Game game;

    game.engine->loadString("local calls = 0 function update(dt) calls = calls + 1 world:spawn{Enemy = {}} if calls == 3 then error('boom') end end", "bad.lua");
    kuge::ScriptSystem system(game.engine);

    game.tick(system, 10);
    Assert(system.stopped(), "the script that failed is stopped");
    Assert(game.enemies() == 3, "and calls nothing more: %zu", game.enemies());
    system.restart();
    game.tick(system, 1);
    Assert(game.enemies() == 4 && !system.stopped(), "restart() lets it run again");
}

Test(script_system, over_budget_is_stopped)
{
    Game game;

    game.engine->loadString("function update(dt) while true do end end", "slow.lua");
    kuge::ScriptSystem system(game.engine);

    game.tick(system, 3);
    Assert(system.stopped(), "a script over its budget is stopped, and the tick goes on");
}

Test(script_system, no_function_is_said)
{
    Game game;

    game.engine->loadString("x = 1", "empty.lua");
    kuge::ScriptSystem system(game.engine);

    game.tick(system, 1);
    Assert(system.stopped(), "no update(): stopped, and logged");
    kuge::ScriptSystem other(game.engine, {.function = "on_tick"});
    game.engine->loadString("function on_tick(dt) world:spawn{Enemy = {}} end", "named.lua");
    game.tick(other, 2);
    Assert(game.enemies() == 2, "another function name: %zu", game.enemies());
}

Test(script_system, a_new_seed_each_tick)
{
    Game game;

    game.engine->loadString("function update(dt) world:spawn{Enemy = {speed = math.random()}} end", "seed.lua");
    kuge::ScriptSystem system(game.engine);
    kuge::ScriptSystem salted(game.engine, {.seedSalt = 7});

    game.tick(system, 2);
    std::vector<float> speeds;

    for (const kw::Entity e : game.registry.entitiesWith(game.world, {"Enemy"})) {
        speeds.push_back(game.world.get<Enemy>(e).speed);
    }
    Assert(speeds.size() == 2 && speeds[0] != speeds[1], "the numbers change from a tick to the next");
    // The same tick again (a prediction run again) rolls the same number, another salt another one
    game.world.getResource<kuge::Time>().tick = 2;
    salted.handle(game.world);
    game.world.getResource<kuge::Time>().tick = 2;
    system.handle(game.world);
    const auto all = game.registry.entitiesWith(game.world, {"Enemy"});

    Assert(all.size() == 4, "two more");
    Assert(game.world.get<Enemy>(all[3]).speed == speeds[1], "the same tick, the same number");
    Assert(game.world.get<Enemy>(all[2]).speed != speeds[1], "another salt, another number");
}

Test(script_system, reload_when_a_file_changes)
{
    Folder folder;
    const auto file = folder.write("ai.lua", "function update(dt) world:spawn{Enemy = {}} end");
    Game game;

    game.engine->load(file);
    Assert(!game.engine->reloadChanged().reloaded, "nothing changed, nothing reloaded");
    folder.write("ai.lua", "function update(dt) world:spawn{Enemy = {}} world:spawn{Enemy = {}} end   -- two");
    auto report = game.engine->reloadChanged();

    Assert(report.reloaded && report.error.empty(), "a changed file is loaded again: %s", report.error.c_str());
    Assert(!game.engine->reloadChanged().reloaded, "and only once");
    Assert(game.engine->call(game.world, "update").ok && game.enemies() == 2, "the new one runs");

    folder.write("ai.lua", "function update(dt) this is not lua end");
    report = game.engine->reloadChanged();
    Assert(!report.reloaded && report.error.find("ai.lua") != std::string::npos, "a script that does not load is refused, with its file: %s", report.error.c_str());
    Assert(game.engine->call(game.world, "update").ok && game.enemies() == 4, "the old one keeps running");
    Assert(report.error != "" && game.engine->reloadChanged().error.empty(), "the error is said once");

    folder.write("ai.lua", "function update(dt) world:spawn{Enemy = {}} end -- fixed, and longer than the broken one");
    Assert(game.engine->reloadChanged().reloaded, "a fixed file is loaded");
}

Test(script_system, reloads_in_dev)
{
    Folder folder;
    const auto file = folder.write("ai.lua", "function update(dt) error('first version') end");
    Game game;

    game.engine->load(file);
    kuge::ScriptSystem system(game.engine, {.reloadEvery = 5});

    game.tick(system, 4);   // ticks 1..4: no look at the file yet; the first call fails
    Assert(system.stopped() && game.enemies() == 0, "the first version fails");
    folder.write("ai.lua", "function update(dt) world:spawn{Enemy = {}} end   -- the second version");
    game.tick(system, 1);   // tick 5: the file is looked at
    Assert(!system.stopped(), "a reload that works lets the system run again");
    game.tick(system, 2);
    Assert(game.enemies() == 3, "and the new script runs: %zu", game.enemies());

    folder.write("ai.lua", "function update(dt) syntax error here end");
    game.tick(system, 5);
    Assert(!system.stopped() && game.enemies() == 8, "a broken edit does not stop the old script: %zu", game.enemies());
}
