extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Engine.hpp"
#include "EngineCommands.hpp"
#include "Logger.hpp"
#include "Stage.hpp"
#include <filesystem>
#include <sstream>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    struct Counter : kw::ISystem
    {
        explicit Counter(int& count) : m_count(count) {}
        bool handle(kw::World&) override { ++m_count; return true; }
        int& m_count;
    };

    struct Level : kuge::Scene
    {
        explicit Level(int* count) : m_count(count) {}
        void onEnter(void) override { addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Counter>(*m_count)); }
        int* m_count;
    };

    struct Rig
    {
        kuge::Engine    engine;
        kuge::Cvars     cvars;
        kuge::Commands  commands{cvars};
        int             ticks = 0;

        Rig()
        {
            kuge::registerEngineCommands(engine, commands);
            engine.scenes().change<Level>(&ticks);
            engine.step(0.0);
        }

        void frames(int count) { for (int i = 0; i < count; ++i) { engine.step(1.0 / 60.0); } }
    };
}

Test(time, pause_and_step)
{
    Rig rig;

    rig.frames(10);
    AssertEq(rig.ticks, 10, "ten frames of one tick");
    AssertStrEq(rig.commands.run("pause on").c_str(), "paused (step runs a tick)", "paused");
    rig.frames(10);
    AssertEq(rig.ticks, 10, "the tick counter does not move while paused");
    Assert(rig.engine.paused(), "the engine says so");
    rig.commands.run("step 3");
    rig.frames(1);
    AssertEq(rig.ticks, 13, "step 3: exactly three ticks");
    rig.frames(5);
    AssertEq(rig.ticks, 13, "and paused again");
    rig.commands.run("step");
    rig.frames(2);
    AssertEq(rig.ticks, 14, "step alone: one");
    rig.commands.run("pause");
    rig.frames(4);
    AssertEq(rig.ticks, 18, "pause with no word flips: running again");
    Assert(rig.commands.run("step 0").find("step [n]") == 0 && rig.commands.run("step abc").find("step [n]") == 0, "a bad count is an answer");
}

Test(time, scale_changes_the_pace_only)
{
    Rig slow;
    Rig real;

    slow.commands.run("timescale 0.5");
    slow.frames(120);
    real.frames(60);
    Assert(std::abs(slow.ticks - real.ticks) <= 1, "half speed for twice as long: the same number of ticks (%d and %d)", slow.ticks, real.ticks);
    Rig fast;

    fast.commands.run("timescale 4");
    fast.frames(30);
    Assert(fast.ticks >= 119 && fast.ticks <= 121, "four times as fast: %d ticks in half a second", fast.ticks);
    Rig bad;

    Assert(bad.commands.run("timescale -1").find("timescale <x>") == 0 && bad.commands.run("timescale x").find("timescale <x>") == 0, "bad values are refused");
    AssertEq(bad.engine.timeScale(), 1.0, "and nothing changed");
}

Test(time, a_server_needs_cheats)
{
    Rig rig;

    rig.cvars.setSide(kuge::Cvars::Side::Server);
    Assert(rig.commands.run("pause on").find("need cheats") != std::string::npos, "refused on a server: %s", rig.commands.run("pause on").c_str());
    Assert(rig.commands.run("timescale 2").find("need cheats") != std::string::npos && !rig.engine.paused(), "all of them");
    rig.cvars.setCheats(true);
    Assert(rig.commands.run("pause on").find("paused") == 0, "with cheats on it works");
    rig.engine.setPaused(false);
    kuge::Engine other;
    kuge::Commands commands(rig.cvars);

    kuge::registerEngineCommands(other, commands, [] { return std::string("there are two players"); });
    AssertStrEq(commands.run("step").c_str(), "there are two players", "the game's own rule gives its reason");
}

Test(time, the_fps_command)
{
    Rig rig;

    rig.frames(30);
    const std::string line = rig.commands.run("fps");

    Assert(line.find("fps") != std::string::npos && line.find("tick") != std::string::npos, "it tells: %s", line.c_str());
    Assert(rig.engine.stats().frames() >= 30, "the engine counted the frames");
    Assert(rig.commands.run("profile top").find("profiling is off") != std::string::npos, "profile top with nothing says how to start");
    rig.commands.run("profile start");
    rig.frames(3);
    Assert(rig.commands.run("profile top 3").find("Counter") != std::string::npos, "profile top names the system");
    const auto file = std::filesystem::temp_directory_path() / "kuge_time_trace.json";

    Assert(rig.commands.run("profile save " + file.string()).find("events written") != std::string::npos, "profile save");
    std::filesystem::remove(file);
    rig.commands.run("profile stop");
    Assert(rig.commands.run("profile").find("profile start") == 0, "no argument: the usage");
}

Test(debug, categories_from_a_list)
{
    Logger logger;
    auto out = std::make_shared<std::ostringstream>();

    logger.registerHandler(out);
    logger.setLevel(LoggerLevel::INFO);
    logger.debugCategories("net, physics ,,");
    logger.category("net").debug("net talks");
    logger.category("physics").debug("physics talks");
    logger.category("audio").debug("audio is quiet");
    logger.debug("the program is quiet");
    Assert(out->str().find("net talks") != std::string::npos && out->str().find("physics talks") != std::string::npos, "the listed ones talk");
    Assert(out->str().find("audio is quiet") == std::string::npos && out->str().find("the program is quiet") == std::string::npos, "the others do not");
}
