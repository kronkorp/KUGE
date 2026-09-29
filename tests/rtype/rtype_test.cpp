extern "C" {
    #include "kronklab/kronklab.h"
}
#include "GameServer.hpp"
#include "RTypeClient.hpp"
#include "RTypeRoom.hpp"
#include "SocketTransport.hpp"
#include "backend/dummy/DummyBackend.hpp"
#include <chrono>
#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <thread>

// NOTE: kronklab test names are limited to 31 characters.
// R-Type, three ways: the rules alone; a server with a client in the same process (a player who hosts);
// and a server that clients reach over real sockets (a dedicated server). The game code is the same.

namespace
{
    using namespace rtype;
    using Clock = std::chrono::steady_clock;

    // A game that lasts seconds, not minutes
    void shortGames(void)
    {
        Settings& s = settings();

        s = Settings{};
        s.waveSize = 5;
        s.spawnEvery = 15;
        s.enemyHealth = 1;
        s.enemySpeed = 300.0f;
    }

    // The rules, in a World of their own: an arena, a match, and ships
    struct Arena
    {
        kw::World world;

        Arena(void)
        {
            buildArena(world);
            world.addResource<Match>();
            world.getResource<Match>().track = [](kw::Entity, kuge::replication::EntityType, std::uint32_t) {};
        }

        kw::Entity ship(kuge::Vec2 at, std::uint32_t player)
        {
            const kw::Entity e = buildShip(world, at);

            world.add<Health>(e, Health{settings().shipHealth});
            world.add<Score>(e, Score{});
            world.add<Gun>(e, Gun{});
            world.add<Owned>(e, Owned{player});
            return e;
        }

        kw::Entity enemy(kuge::Vec2 at)
        {
            const kw::Entity e = world.create();

            world.add<kuge::Transform2D>(e, kuge::Transform2D{at});
            world.add<Enemy>(e, Enemy{at.y, 0.0f, 0.0f});
            world.add<Health>(e, Health{settings().enemyHealth});
            return e;
        }

        std::size_t count(void) { return countOf<Bullet>(); }
        template<typename C> std::size_t countOf(void)
        {
            std::size_t n = 0;
            auto view = world.view<C>();

            for ([[maybe_unused]] kw::Entity e : view) { ++n; }
            return n;
        }
    };

    // A client with no screen, in this process
    struct Pilot
    {
        kuge::DummyBackend               dummy;
        kuge::Engine                     engine;
        kuge::ClientModule*              client;
        std::shared_ptr<ClientReport>    report = std::make_shared<ClientReport>();

        explicit Pilot(const ClientOptions& options)
            : dummy(kuge::makeDummyBackend({960.0f, 540.0f})),
              engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed, .tickRate = 60}),
              client(&engine.addModule<kuge::ClientModule>(std::move(dummy.backend)))
        {
            bindDefaults(client->input());
            engine.scenes().change<RTypeScene>(options, report);
        }

        void press(kuge::Key key, bool down) { client->input().handle(kuge::KeyEvent{key, down}); }
        bool frame(void) { return engine.step(1.0 / 60.0); }
    };

    // Runs several pilots for a while, at 60 Hz in real time (the server has a clock of its own)
    void fly(std::vector<Pilot*> pilots, double seconds, const std::function<bool(void)>& done = {})
    {
        const auto end = Clock::now() + std::chrono::duration<double>(seconds);
        auto next = Clock::now();

        while (Clock::now() < end && !(done && done())) {
            for (Pilot* pilot : pilots) {
                pilot->frame();
            }
            next += std::chrono::microseconds(16667);
            std::this_thread::sleep_until(next);
        }
    }

    // A server on a thread of its own
    struct Server
    {
        kuge::net::LoopbackNetwork              network;
        std::unique_ptr<kuge::server::GameServer> server;
        std::thread                             thread;

        explicit Server(bool sockets, std::uint16_t lobbyPort = 0, std::uint16_t roomPort = 0, std::size_t maxPlayers = 4, kuge::net::LoopbackNetwork* shared = nullptr)
        {
            kuge::server::ServerConfig config;

            if (sockets) {
                config.transport = kuge::server::Transport::Sockets;
                config.lobbyPort = lobbyPort;
                config.roomPortFirst = roomPort;
                config.roomPortCount = 8;
            } else {
                config.transport = kuge::server::Transport::Loopback;
                config.loopback = shared ? shared : &network;
            }
            server = std::make_unique<kuge::server::GameServer>(config);
            server->addRoomType<RTypeRoom>("rtype", kuge::server::RoomTypeConfig{.maxPlayers = maxPlayers, .idleTimeout = 30.0});
            thread = std::thread([this] { server->run(); });
            while (!server->stats().lobbyOpen) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        ~Server()
        {
            server->stop();
            thread.join();
        }

        ClientOptions options(const char* name, std::uint16_t lobbyPort = 0)
        {
            ClientOptions o;

            o.name = name;
            o.sockets = lobbyPort != 0;
            o.port = lobbyPort;
            o.network = &network;
            return o;
        }
    };

    std::uint16_t freePorts(void)
    {
        static std::mt19937 rng(std::random_device{}());

        for (int attempt = 0; attempt < 200; ++attempt) {
            const auto base = static_cast<std::uint16_t>(20000 + rng() % 20000);

            try {
                auto tcp = kuge::net::makeTcpServer(base);
                std::vector<std::unique_ptr<kuge::net::ITransport>> udp;

                for (std::uint16_t i = 1; i <= 9; ++i) {
                    udp.push_back(kuge::net::makeUdpServer(static_cast<std::uint16_t>(base + i)));
                }
                return base;
            } catch (const std::runtime_error&) {
            }
        }
        throw std::runtime_error("no free ports");
    }

    bool waitFor(std::vector<Pilot*> pilots, const std::function<bool(void)>& condition, double seconds = 20.0)
    {
        fly(pilots, seconds, condition);
        return condition();
    }
}

// -- The rules alone ------------------------------------------------------------------------------
Test(rtype_rules, a_bullet_kills)
{
    shortGames();
    settings().enemyHealth = 2;
    settings().enemySpeed = 0.0f;      // a target that stays where it is
    Arena arena;
    const kw::Entity ship = arena.ship({100.0f, 100.0f}, 1);
    const kw::Entity enemy = arena.enemy({200.0f, 100.0f});

    fire(arena.world, ship);
    AssertEq(arena.count(), 1, "a shot");
    fire(arena.world, ship);
    AssertEq(arena.count(), 1, "the gun is not ready again yet");
    settings().waveSize = 0;
    for (int t = 0; t < 60; ++t) {
        stepRules(arena.world, TICK_DT);
        fire(arena.world, ship);
    }
    Assert(!arena.world.has<Enemy>(enemy), "the enemy that was in the line of fire is dead");
    AssertEq(arena.world.get<Score>(ship).points, 1, "and the shooter has the point");
    AssertEq(arena.world.getResource<Match>().killed, 1, "one kill");
}

Test(rtype_rules, an_enemy_hits_a_ship)
{
    shortGames();
    settings().waveSize = 0;
    Arena arena;
    const kw::Entity ship = arena.ship({100.0f, 100.0f}, 1);

    arena.enemy({104.0f, 100.0f});
    stepRules(arena.world, TICK_DT);
    AssertEq(arena.world.get<Health>(ship).points, settings().shipHealth - 1, "the ship lost a point");
    AssertEq(arena.countOf<Enemy>(), 0, "and the enemy is gone");
}

Test(rtype_rules, a_ship_dies)
{
    shortGames();
    settings().waveSize = 0;
    settings().shipHealth = 1;
    Arena arena;

    arena.ship({100.0f, 100.0f}, 1);
    AssertEq(static_cast<int>(outcome(arena.world, true)), static_cast<int>(Outcome::Won), "no enemy to come: the players win");
    arena.enemy({100.0f, 100.0f});
    stepRules(arena.world, TICK_DT);
    AssertEq(ships(arena.world).size(), 0, "the ship is gone");
    AssertEq(static_cast<int>(outcome(arena.world, true)), static_cast<int>(Outcome::Lost), "all ships are dead: the players lose");
}

Test(rtype_rules, a_ship_is_found_by_its_owner)
{
    // The room finds a pilot's ship by its owner, never by a number it kept: the World gives the number of a
    // dead ship to the next entity it makes, and a kept number would then name somebody else's ship
    shortGames();
    settings().waveSize = 0;
    Arena arena;
    const kw::Entity first = arena.ship({100.0f, 100.0f}, 1);

    Assert(shipOf(arena.world, 1) == first, "a pilot's ship is found by its owner");
    arena.world.remove(first);                                      // Ana's ship dies...
    const kw::Entity second = arena.ship({100.0f, 200.0f}, 2);      // ...and Ben joins

    Assert(second == first, "(the World gave the dead ship's number to the new one: what this test is about)");
    Assert(!shipOf(arena.world, 1), "Ana has no ship any more: her inputs steer nothing, her leaving removes nothing");
    Assert(shipOf(arena.world, 2) == second, "and Ben's ship is his");
}

Test(rtype_rules, the_wave_and_the_end)
{
    shortGames();
    Arena arena;

    arena.ship({100.0f, 300.0f}, 1);
    AssertEq(static_cast<int>(outcome(arena.world, true)), static_cast<int>(Outcome::Playing), "playing");
    for (int t = 0; t < 400; ++t) {
        stepRules(arena.world, TICK_DT);
    }
    AssertEq(arena.world.getResource<Match>().spawned, settings().waveSize, "the whole wave came");
    Assert(outcome(arena.world, true) != Outcome::Playing, "and it is over once they are all dead or gone");
}

Test(rtype_rules, same_seed_same_game)
{
    auto play = [] {
        shortGames();
        Arena arena;
        std::vector<float> trace;

        arena.world.getResource<Match>().rng.seed(42);
        const kw::Entity ship = arena.ship({100.0f, 180.0f}, 1);

        for (int t = 0; t < 600; ++t) {
            steerShip(arena.world, ship, Steer{0, static_cast<std::int8_t>((t / 40) % 2 ? 1 : -1), true});
            if (arena.world.has<Gun>(ship)) {
                fire(arena.world, ship);
            }
            stepArena(arena.world, TICK_DT);
            stepRules(arena.world, TICK_DT);
            trace.push_back(static_cast<float>(arena.countOf<Enemy>()) * 1000.0f + static_cast<float>(arena.countOf<Bullet>()) + arena.world.getResource<Match>().killed * 100000.0f);
            if (arena.world.has<Gun>(ship)) {
                trace.push_back(arena.world.get<kuge::Transform2D>(ship).position.y);
            }
        }
        return trace;
    };

    Assert(play() == play(), "the same seed and the same inputs: the same game, tick for tick");
}

// -- A server and a client in the same process (a player who hosts the match) ----------------------------
Test(rtype_host, two_pilots_one_process)
{
    shortGames();
    settings().waveSize = 100;         // (it does not end: this is about seeing the game)
    Server host(false);
    Pilot ana(host.options("Ana"));
    Pilot ben(host.options("Ben"));

    Assert(waitFor({&ana, &ben}, [&] { return ana.report->ships == 2 && ben.report->ships == 2; }), "both pilots joined and see two ships (ana %zu, ben %zu)", ana.report->ships, ben.report->ships);
    AssertEq(host.server->stats().rooms.load(), 1, "in one room");
    // Ana flies right and fires
    ana.press(kuge::Key::Right, true);
    ana.press(kuge::Key::Space, true);
    const float before = ana.report->shipAt.x;

    fly({&ana, &ben}, 1.0);
    Assert(ana.report->shipAt.x > before + 50.0f, "Ana's ship moves: %f to %f", before, ana.report->shipAt.x);
    Assert(waitFor({&ana, &ben}, [&] { return ana.report->mostBullets > 0 && ben.report->mostBullets > 0; }), "bullets fly, on both screens");
    Assert(waitFor({&ana, &ben}, [&] { return ana.report->mostEnemies > 0 && ben.report->mostEnemies > 0; }), "enemies come, on both screens");
    ana.press(kuge::Key::Right, false);
    ben.press(kuge::Key::Space, true);
    fly({&ana, &ben}, 2.0);
    Assert(ana.report->score + ben.report->score > 0 || ana.report->mostBullets > 3, "the shooting goes on");
    const auto& stats = ana.report->prediction;

    Assert(stats.reconciliations > 50, "Ana's prediction was consulted: %llu", static_cast<unsigned long long>(stats.reconciliations));
    Assert(stats.corrections * 20 < stats.reconciliations, "and the server nearly never contradicted it: %llu corrections", static_cast<unsigned long long>(stats.corrections));
    AssertEq(stats.snaps, 0, "no snap");
    AssertEq(ana.report->replication.malformed, 0, "no snapshot was malformed");
    // Ben leaves
    ben.engine.scenes().clear();
    fly({&ana}, 1.5);
    AssertEq(ana.report->ships, 1, "when Ben leaves, his ship is gone from Ana's screen");
}

Test(rtype_host, a_game_ends)
{
    shortGames();
    Server host(false);
    Pilot ana(host.options("Ana"));
    Pilot ben(host.options("Ben"));

    Assert(waitFor({&ana, &ben}, [&] { return ana.report->ships == 2 && ben.report->ships == 2; }), "in the game");
    ana.press(kuge::Key::Space, true);
    ben.press(kuge::Key::Space, true);
    ben.press(kuge::Key::Down, true);
    Assert(waitFor({&ana, &ben}, [&] { return ana.report->gamesEnded == 1 && ben.report->gamesEnded == 1; }, 30.0), "the wave is over: the room tells both (ana %llu, ben %llu)",
        static_cast<unsigned long long>(ana.report->gamesEnded), static_cast<unsigned long long>(ben.report->gamesEnded));
    Assert(waitFor({&ana, &ben}, [&] { return ana.report->games == 2 && ben.report->games == 2; }, 20.0), "and they are in a new game");
    // The last picture of the first game stays on screen until the next one starts: then it must go
    Assert(waitFor({&ana, &ben}, [&] { return ana.report->ships == 2 && ben.report->ships == 2; }, 10.0), "the new game shows its two ships (ana %zu, ben %zu)", ana.report->ships, ben.report->ships);
    fly({&ana, &ben}, 0.5);
    AssertEq(ana.report->ships, 2, "and only them: nothing is left of the first game on Ana's screen");
    AssertEq(ben.report->ships, 2, "or on Ben's");
    AssertEq(ana.report->joined, true, "joined");
    Assert(waitFor({&ana, &ben}, [&] { return host.server->engine().spawned() == 1; }, 10.0), "the first room is gone: one runs");
}

Test(rtype_host, clients_before_the_server)
{
    // Clients started first (or all at once with the server) find nobody: they try again until it is there
    shortGames();
    settings().waveSize = 1000;
    kuge::net::LoopbackNetwork network;
    ClientOptions options;

    options.sockets = false;
    options.network = &network;
    options.name = "Ana";
    Pilot ana(options);
    options.name = "Ben";
    Pilot ben(options);

    fly({&ana, &ben}, 1.5);
    AssertEq(ana.report->joined, false, "nobody to join yet");
    Server host(false, 0, 0, 4, &network);

    Assert(waitFor({&ana, &ben}, [&] { return ana.report->ships == 2 && ben.report->ships == 2; }, 20.0), "the server comes up: both join the same game (ana %zu, ben %zu)", ana.report->ships, ben.report->ships);
}

// -- Several rooms at the same time ------------------------------------------------------------------
namespace
{
    // Pilots, made and kept together
    struct Squad
    {
        std::vector<std::unique_ptr<Pilot>> pilots;

        Squad(Server& server, std::size_t count, std::uint16_t lobbyPort = 0)
        {
            for (std::size_t i = 0; i < count; ++i) {
                pilots.push_back(std::make_unique<Pilot>(server.options(("P" + std::to_string(i)).c_str(), lobbyPort)));
            }
        }

        std::vector<Pilot*> all(void)
        {
            std::vector<Pilot*> list;

            for (auto& pilot : pilots) { list.push_back(pilot.get()); }
            return list;
        }

        // Who is in which room
        std::map<std::uint32_t, std::vector<Pilot*>> byRoom(void)
        {
            std::map<std::uint32_t, std::vector<Pilot*>> rooms;

            for (auto& pilot : pilots) { rooms[pilot->report->roomId].push_back(pilot.get()); }
            return rooms;
        }

        bool allIn(void)
        {
            return std::all_of(pilots.begin(), pilots.end(), [](const auto& p) { return p->report->joined && p->report->ships >= 1; });
        }
    };
}

Test(rtype_rooms, games_do_not_mix)
{
    // Five pilots, two to a room: three games at once. Each pilot sees its own room only.
    shortGames();
    settings().waveSize = 1000;
    Server host(false, 0, 0, 2);
    Squad squad(host, 5);

    Assert(waitFor(squad.all(), [&] { return squad.allIn() && host.server->stats().rooms == 3; }), "five pilots, three rooms");
    Assert(waitFor(squad.all(), [&] {
        const auto rooms = squad.byRoom();

        return std::all_of(rooms.begin(), rooms.end(), [](const auto& room) {
            return std::all_of(room.second.begin(), room.second.end(), [&room](Pilot* p) { return p->report->ships == room.second.size(); });
        });
    }), "each pilot sees as many ships as there are pilots in its room, and no other");
    const auto rooms = squad.byRoom();
    std::vector<std::size_t> sizes;

    for (const auto& room : rooms) { sizes.push_back(room.second.size()); }
    std::sort(sizes.begin(), sizes.end());
    Assert(sizes == std::vector<std::size_t>({1, 2, 2}), "rooms of 2, 2 and 1");
    AssertEq(rooms.size(), 3, "three room numbers");

    // Only the pilots of the first room fire: nothing they do shows in the other rooms
    const std::uint32_t firing = rooms.begin()->first;

    for (Pilot* pilot : rooms.at(firing)) { pilot->press(kuge::Key::Space, true); }
    fly(squad.all(), 2.5);
    for (const auto& [room, pilots] : rooms) {
        for (Pilot* pilot : pilots) {
            if (room == firing) {
                Assert(pilot->report->mostBullets > 0, "the shooters see their bullets");
            } else {
                AssertEq(pilot->report->mostBullets, 0, "room %u: nobody there fired: no bullet from another room shows", room);
            }
            Assert(pilot->report->mostEnemies > 0, "every room has its own enemies");
        }
    }
}

Test(rtype_rooms, one_room_leaves)
{
    shortGames();
    settings().waveSize = 1000;
    Server host(false, 0, 0, 2);
    Squad squad(host, 4);

    Assert(waitFor(squad.all(), [&] { return squad.allIn() && host.server->stats().rooms == 2; }), "four pilots, two rooms");
    auto rooms = squad.byRoom();
    const std::uint32_t leaving = rooms.begin()->first;
    const std::uint32_t staying = rooms.rbegin()->first;

    Assert(leaving != staying, "two different rooms");
    for (Pilot* pilot : rooms.at(staying)) { pilot->press(kuge::Key::Space, true); }
    // The pilots of the other room quit
    for (auto& pilot : squad.pilots) {
        if (pilot->report->roomId == leaving) {
            pilot->engine.scenes().clear();
        }
    }
    std::vector<Pilot*> remaining = rooms.at(staying);

    fly(remaining, 2.0);
    for (Pilot* pilot : remaining) {
        AssertEq(pilot->report->ships, 2, "the game that goes on is not troubled: two ships");
        Assert(pilot->report->mostBullets > 0 && pilot->report->prediction.snaps == 0, "it keeps running");
    }
    Assert(waitFor(remaining, [&] { return host.server->stats().players == 2; }), "the lobby counts the two that are left");
}

Test(rtype_rooms, games_end_apart)
{
    // Two rooms play short games and end one after the other: each pilot is told about its own room, and asks for the next
    shortGames();
    Server host(false, 0, 0, 2);
    Squad squad(host, 4);

    Assert(waitFor(squad.all(), [&] { return squad.allIn() && host.server->stats().rooms == 2; }), "two rooms");
    for (auto& pilot : squad.pilots) { pilot->press(kuge::Key::Space, true); }
    Assert(waitFor(squad.all(), [&] {
        return std::all_of(squad.pilots.begin(), squad.pilots.end(), [](const auto& p) { return p->report->gamesEnded >= 1; });
    }, 40.0), "every pilot hears that its game is over");
    Assert(waitFor(squad.all(), [&] {
        return std::all_of(squad.pilots.begin(), squad.pilots.end(), [](const auto& p) { return p->report->games >= 2 && p->report->ships >= 1; });
    }, 25.0), "and is in a second game");
    const auto rooms = squad.byRoom();

    for (const auto& [room, pilots] : rooms) {
        Assert(pilots.size() <= 2, "room %u holds at most two", room);
    }
    Assert(waitFor(squad.all(), [&] { return host.server->engine().spawned() == 2; }, 10.0), "two rooms run (the first two are gone)");
}

Test(rtype_rooms, over_real_sockets)
{
    // Six pilots on a dedicated server, three to a room: two rooms, each on a port of its own
    shortGames();
    settings().waveSize = 1000;
    const std::uint16_t port = freePorts();
    Server dedicated(true, port, static_cast<std::uint16_t>(port + 1), 3);
    Squad squad(dedicated, 6, port);

    Assert(waitFor(squad.all(), [&] { return squad.allIn() && dedicated.server->stats().rooms == 2; }), "six pilots, two rooms, over tcp and udp");
    Assert(waitFor(squad.all(), [&] {
        const auto rooms = squad.byRoom();

        return rooms.size() == 2 && std::all_of(rooms.begin(), rooms.end(), [](const auto& room) {
            return room.second.size() == 3 && std::all_of(room.second.begin(), room.second.end(), [](Pilot* p) { return p->report->ships == 3; });
        });
    }), "three and three, each seeing its three ships");
    for (auto& pilot : squad.pilots) { pilot->press(kuge::Key::Space, true); }
    fly(squad.all(), 2.5);
    for (auto& pilot : squad.pilots) {
        Assert(pilot->report->mostBullets > 0 && pilot->report->mostEnemies > 0, "bullets and enemies in every room");
        AssertEq(pilot->report->replication.malformed, 0, "nothing malformed");
    }
}

Test(rtype_host, pilots_come_and_go)
{
    shortGames();
    settings().waveSize = 1000;
    Server host(false);
    Pilot ana(host.options("Ana"));
    auto ben = std::make_unique<Pilot>(host.options("Ben"));

    Assert(waitFor({&ana, ben.get()}, [&] { return ana.report->ships == 2; }), "two ships");
    ben.reset();
    Assert(waitFor({&ana}, [&] { return ana.report->ships == 1; }), "Ben left");
    Pilot cleo(host.options("Cleo"));

    Assert(waitFor({&ana, &cleo}, [&] { return ana.report->ships == 2 && cleo.report->ships == 2; }), "Cleo takes his place, and sees Ana");
}

// -- A server that clients reach over the network (a dedicated server) ---------------------------------
Test(rtype_dedicated, over_real_sockets)
{
    shortGames();
    settings().waveSize = 100;
    const std::uint16_t port = freePorts();
    Server dedicated(true, port, static_cast<std::uint16_t>(port + 1));
    Pilot ana(dedicated.options("Ana", port));
    Pilot ben(dedicated.options("Ben", port));

    Assert(waitFor({&ana, &ben}, [&] { return ana.report->ships == 2 && ben.report->ships == 2; }), "two pilots joined over tcp and udp (ana %zu, ben %zu)", ana.report->ships, ben.report->ships);
    ana.press(kuge::Key::Space, true);
    ana.press(kuge::Key::Right, true);
    ben.press(kuge::Key::Up, true);
    fly({&ana, &ben}, 1.0);
    Assert(waitFor({&ana, &ben}, [&] { return ana.report->mostBullets > 0 && ben.report->mostBullets > 0 && ben.report->mostEnemies > 0; }), "bullets and enemies on both screens");
    Assert(ana.report->shipAt.x > 60.0f, "Ana moved: %f", ana.report->shipAt.x);
    std::printf("    (dedicated: %llu corrections in %llu snapshots, largest %f, %llu replays)\n", static_cast<unsigned long long>(ana.report->prediction.corrections),
        static_cast<unsigned long long>(ana.report->prediction.reconciliations), ana.report->prediction.largestCorrection, static_cast<unsigned long long>(ana.report->prediction.replays));
    Assert(ana.report->prediction.corrections * 10 < ana.report->prediction.reconciliations + 10, "and her prediction held (%llu corrections)", static_cast<unsigned long long>(ana.report->prediction.corrections));
}

Test(rtype_dedicated, four_pilots)
{
    shortGames();
    settings().waveSize = 100;
    const std::uint16_t port = freePorts();
    Server dedicated(true, port, static_cast<std::uint16_t>(port + 1));
    std::vector<std::unique_ptr<Pilot>> pilots;
    std::vector<Pilot*> all;

    for (const char* name : {"A", "B", "C", "D"}) {
        pilots.push_back(std::make_unique<Pilot>(dedicated.options(name, port)));
        all.push_back(pilots.back().get());
    }
    Assert(waitFor(all, [&] { return std::all_of(all.begin(), all.end(), [](Pilot* p) { return p->report->ships == 4; }); }), "four pilots in one room, each sees four ships");
    const kuge::Key keys[4] = {kuge::Key::Right, kuge::Key::Up, kuge::Key::Down, kuge::Key::Left};

    for (std::size_t i = 0; i < 4; ++i) {
        all[i]->press(kuge::Key::Space, true);
        all[i]->press(keys[i], true);
    }
    fly(all, 4.0);
    for (Pilot* pilot : all) {
        Assert(pilot->report->mostBullets > 0 && pilot->report->mostEnemies > 0, "a pilot sees bullets and enemies");
        AssertEq(pilot->report->replication.malformed, 0, "nothing malformed");
    }
    AssertEq(dedicated.server->stats().rooms.load(), 1, "still one room");
}

Test(rtype_dedicated, the_server_stops)
{
    // The server ends with pilots in a game: everything is left, and the clients see the loss
    for (int round = 0; round < 5; ++round) {
        shortGames();
        settings().waveSize = 100;
        auto dedicated = std::make_unique<Server>(false);
        Pilot ana(dedicated->options("Ana"));

        Assert(waitFor({&ana}, [&] { return ana.report->ships >= 1; }), "round %d: in the game", round);
        dedicated.reset();
        Assert(waitFor({&ana}, [&] { return ana.report->state == kuge::net::MatchmakingClient::State::Failed; }, 10.0), "round %d: the client sees the server go", round);
    }
}
