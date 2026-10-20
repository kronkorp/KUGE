extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Commands.hpp"
#include "Engine.hpp"
#include <thread>

// NOTE: kronklab test names are limited to 31 characters.

Test(cvars, set_parses_and_refuses)
{
    kuge::Cvars cvars;
    auto rate = cvars.add("tick_rate", 60);
    auto gravity = cvars.add("gravity", 9.8f);
    auto god = cvars.add("god", false);
    auto name = cvars.add<std::string>("player_name", "bob");

    Assert(cvars.set("tick_rate", "30").ok && rate.get() == 30, "an int is parsed");
    const auto bad = cvars.set("tick_rate", "abc");

    Assert(!bad.ok && rate.get() == 30, "a bad value leaves the variable as it was");
    Assert(bad.message.find("tick_rate") != std::string::npos && bad.message.find("abc") != std::string::npos, "and the message names both: %s", bad.message.c_str());
    Assert(!cvars.set("tick_rate", "3x").ok && !cvars.set("tick_rate", "").ok, "half a number, and nothing, are not numbers");
    Assert(cvars.set("gravity", "1.5").ok && gravity.get() == 1.5f, "a float");
    Assert(!cvars.set("gravity", "nan").ok, "not a number is refused");
    Assert(cvars.set("god", "on").ok && god.get(), "a bool: on");
    Assert(cvars.set("god", "FALSE").ok && !god.get(), "a bool: any case");
    Assert(!cvars.set("god", "maybe").ok, "a bool: maybe");
    Assert(cvars.set("player_name", "a b").ok && name.get() == "a b", "a string");
    Assert(!cvars.set("nothing", "1").ok && cvars.get("nothing").empty(), "an unknown variable");
    AssertStrEq(cvars.get("tick_rate").c_str(), "30", "read as text");
}

Test(cvars, flags_decide_who_may)
{
    kuge::Cvars cvars;
    auto cheat = cvars.add("damage", 1, kuge::CvarFlag::Cheat);
    auto server = cvars.add("room_size", 4, kuge::CvarFlag::ServerOnly);
    auto client = cvars.add("fov", 90, kuge::CvarFlag::ClientOnly);

    Assert(!cvars.set("damage", "5").ok && cheat.get() == 1, "a cheat is refused with cheats off");
    cvars.setCheats(true);
    Assert(cvars.set("damage", "5").ok && cheat.get() == 5, "and accepted with them on");
    cvars.setSide(kuge::Cvars::Side::Client);
    Assert(!cvars.set("room_size", "8").ok && server.get() == 4, "a server variable is refused on a client");
    Assert(cvars.set("fov", "100").ok && client.get() == 100, "a client one is fine");
    cvars.setSide(kuge::Cvars::Side::Server);
    Assert(!cvars.set("fov", "70").ok && cvars.set("room_size", "8").ok, "and the other way round");
    bool threw = false;

    try { cvars.add("fov", 1); } catch (const std::invalid_argument&) { threw = true; }
    Assert(threw, "a name is not taken twice");
    threw = false;
    try { cvars.add("bad name", 1); } catch (const std::invalid_argument&) { threw = true; }
    Assert(threw, "a name has no spaces");
}

Test(cvars, persistent_ones_are_saved)
{
    kuge::Cvars cvars;
    kuge::ConfigFile file;

    cvars.add("volume", 0.5f, kuge::CvarFlag::Persistent);
    cvars.add("difficulty", 2, kuge::CvarFlag::Persistent | kuge::CvarFlag::Cheat);
    cvars.add("scratch", 7);
    cvars.set("volume", "0.8");
    cvars.setCheats(true);
    cvars.set("difficulty", "3");
    cvars.save(file);
    Assert(file.has("cvar.volume") && file.has("cvar.difficulty") && !file.has("cvar.scratch"), "only the persistent ones are written");

    kuge::Cvars later;
    auto volume = later.add("volume", 0.5f, kuge::CvarFlag::Persistent);
    auto difficulty = later.add("difficulty", 2, kuge::CvarFlag::Persistent | kuge::CvarFlag::Cheat);

    file.set("cvar.volume", "loud");
    AssertEq(later.load(file).size(), 1u, "a value that cannot be used is said");
    Assert(volume.get() == 0.5f && difficulty.get() == 3, "and left out; the others are read back (even a cheat, with cheats off)");
}

Test(commands, run_a_line)
{
    kuge::Cvars cvars;
    kuge::Commands commands(cvars);
    auto speed = cvars.add("speed", 3);
    std::vector<std::string> seen;

    commands.add("spawn", [&](const std::vector<std::string>& args) { seen = args; return std::string("spawned ") + args.at(0); }, "spawn <enemy> [n]");
    AssertStrEq(commands.run("echo \"a b\"").c_str(), "a b", "a quoted word keeps its space");
    AssertStrEq(commands.run("echo a   b").c_str(), "a b", "spaces between words are one");
    AssertStrEq(commands.run("spawn boss_blob 3").c_str(), "spawned boss_blob", "a command of the game");
    Assert(seen.size() == 2 && seen[1] == "3", "with its arguments");
    Assert(commands.run("nothing").find("unknown command 'nothing'") == 0, "an unknown command is an answer: %s", commands.run("nothing").c_str());
    Assert(commands.run("spawn").find("spawn:") == 0, "a command that throws is an answer too: %s", commands.run("spawn").c_str());
    AssertStrEq(commands.run("").c_str(), "", "an empty line says nothing");
    AssertStrEq(commands.run("speed").c_str(), "speed = 3", "a variable alone is read");
    commands.run("speed 7");
    AssertEq(speed.get(), 7, "with a value it is set");
    commands.run("set speed 9");
    AssertEq(speed.get(), 9, "set");
    Assert(commands.run("set speed no").find("not a whole number") != std::string::npos, "set says why not");
    AssertStrEq(commands.run("get speed").c_str(), "speed = 9", "get");
    Assert(commands.run("cvars sp").find("speed = 9") == 0, "cvars lists with a filter");
    Assert(commands.run("cvars zzz") == "no variable", "and says when there is none");
    Assert(commands.run("help").find("spawn - spawn <enemy> [n]") != std::string::npos, "help lists the commands");
    Assert(commands.run("help spawn").find("spawn <enemy>") != std::string::npos, "or one of them");
}

Test(commands, completion)
{
    kuge::Cvars cvars;
    kuge::Commands commands(cvars);

    cvars.add("sv_gravity", 1);
    cvars.add("sv_speed", 1);
    commands.add("spawn", [](const std::vector<std::string>&) { return std::string(); });
    const auto sv = commands.complete("sv_");
    const auto s = commands.complete("s");

    AssertEq(sv.size(), 2u, "the variables that start with sv_");
    Assert(s.size() == 4 && s.front() == "set" && std::is_sorted(s.begin(), s.end()), "commands and variables together, sorted: %zu", s.size());
    AssertEq(commands.complete("zz").size(), 0u, "none");
}

namespace
{
    struct World : kuge::Scene
    {
        using kuge::Scene::world;

        World(kuge::Commands* commands, std::shared_ptr<kuge::CommandQueue> queue) : m_commands(commands), m_queue(std::move(queue)) {}
        void onEnter(void) override { kuge::installCommandQueue(setup(), *m_commands, m_queue); }

        kuge::Commands*                     m_commands;
        std::shared_ptr<kuge::CommandQueue> m_queue;
    };
}

Test(commands, queued_lines_run_in_the_tick)
{
    kuge::Cvars cvars;
    kuge::Commands commands(cvars);
    auto queue = std::make_shared<kuge::CommandQueue>();
    auto value = cvars.add("value", 0);
    std::thread::id ranOn;

    commands.add("here", [&](const std::vector<std::string>&) { ranOn = std::this_thread::get_id(); return std::string(); });
    kuge::Engine engine;

    engine.scenes().change<World>(&commands, queue);
    engine.step(0.0);
    std::thread([&] { queue->push("value 5"); queue->push("here"); }).join();
    AssertEq(value.get(), 0, "nothing runs while the line waits");
    engine.step(1.0 / 60.0);
    Assert(value.get() == 5 && ranOn == std::this_thread::get_id(), "the next tick runs them, on its own thread");
    AssertEq(queue->take().size(), 0u, "and they are gone");
}
