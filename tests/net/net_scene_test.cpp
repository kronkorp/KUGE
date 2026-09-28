extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Engine.hpp"
#include "Net.hpp"
#include "SocketTransport.hpp"
#include "net_fixture.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <random>
#include <set>
#include <thread>

// NOTE: kronklab test names are limited to 31 characters.
// Scenes on several threads that talk over a network: each has its endpoints, and only messages cross.

namespace
{
    using namespace kuge::net;
    using Clock = std::chrono::steady_clock;

    bool waitUntil(const std::function<bool(void)>& done, double seconds = 30.0)
    {
        const auto end = Clock::now() + std::chrono::duration<double>(seconds);

        while (!done()) {
            if (Clock::now() > end) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    struct Hooks
    {
        std::function<void(Net&, kuge::SceneContext&)> enter;
        std::function<void(Net&)>                      exit;
    };

    // A scene with a Net, that says what to do with it
    class NetScene : public kuge::Scene
    {
        public:
            explicit NetScene(Hooks hooks) : m_hooks(std::move(hooks)) {}

            void onEnter(void) override
            {
                installNet(setup());
                if (m_hooks.enter) {
                    m_hooks.enter(world().getResource<Net>(), ctx());
                }
            }

            void onExit(void) override
            {
                if (m_hooks.exit) {
                    m_hooks.exit(world().getResource<Net>());
                }
            }

        private:
            Hooks m_hooks;
    };

    struct Ticks : kw::ISystem
    {
        bool handle(kw::World&) override { return true; }
    };

    kuge::Engine::Config config(std::uint32_t tickRate, std::uint32_t workers = 2)
    {
        return kuge::Engine::Config{.tickRate = tickRate, .workers = workers};
    }

    std::uint16_t freePort(bool tcp)
    {
        static std::mt19937 rng(std::random_device{}());

        for (int attempt = 0; attempt < 200; ++attempt) {
            const auto port = static_cast<std::uint16_t>(20000 + rng() % 30000);

            try {
                (void)(tcp ? makeTcpServer(port) : makeUdpServer(port));
                return port;
            } catch (const std::runtime_error&) {
            }
        }
        throw std::runtime_error("no free port");
    }

    // The server echoes numbers back +1. The client sends N, and counts what comes back.
    struct Run
    {
        static constexpr std::uint32_t COUNT = 500;
        std::atomic<std::uint32_t>     answers{0};
        std::atomic<std::uint32_t>     wrong{0};
        std::atomic<bool>              connected{false};
        std::atomic<bool>              listening{false};
        std::mutex                     mutex;
        std::set<std::thread::id>      serverThreads, clientThreads;

        Hooks server(std::function<Endpoint&(Net&)> make)
        {
            Hooks hooks;

            hooks.enter = [this, make](Net& net, kuge::SceneContext&) {
                Endpoint& endpoint = make(net);

                listening = true;
                endpoint.on<Number>([this, &endpoint](ConnectionId from, const Number& number) {
                    { std::lock_guard lock(mutex); serverThreads.insert(std::this_thread::get_id()); }
                    endpoint.send(from, Number{number.value + 1});
                });
            };
            return hooks;
        }

        Hooks client(std::function<Endpoint&(Net&)> make)
        {
            Hooks hooks;

            hooks.enter = [this, make](Net& net, kuge::SceneContext&) {
                Endpoint& endpoint = make(net);
                auto next = std::make_shared<std::uint32_t>(0);

                endpoint.onConnected([this, &endpoint, next](ConnectionId id) {
                    connected = true;
                    for (; *next < 20; ++*next) {          // a first window of messages
                        endpoint.send(id, Number{*next * 2});
                    }
                });
                endpoint.on<Number>([this, &endpoint, next](ConnectionId id, const Number& number) {
                    { std::lock_guard lock(mutex); clientThreads.insert(std::this_thread::get_id()); }
                    if (number.value % 2 == 0) {
                        ++wrong;                           // the server adds 1 to an even number
                    }
                    ++answers;
                    if (*next < COUNT) {
                        endpoint.send(id, Number{*next * 2});
                        ++*next;
                    }
                });
            };
            return hooks;
        }
    };
}

Test(net_scene, two_scenes_on_loopback)
{
    LoopbackNetwork network;
    Run run;
    kuge::Engine engine(config(500));
    LoopbackNetwork* net = &network;

    engine.spawn<NetScene>(kuge::RunPolicy::Dedicated, run.server([net](Net& n) -> Endpoint& { return n.listen("game", *net); }));
    waitUntil([&] { return run.listening.load(); });
    engine.spawn<NetScene>(kuge::RunPolicy::Dedicated, run.client([net](Net& n) -> Endpoint& { return n.connect("game", *net); }));
    Assert(waitUntil([&run] { return run.answers == Run::COUNT; }), "the client got its %u answers (%u so far)", Run::COUNT, run.answers.load());
    AssertEq(run.wrong.load(), 0, "each is the number sent plus one");
    Assert(run.serverThreads.size() == 1 && run.clientThreads.size() == 1, "each scene's messages arrive on one thread");
    Assert(*run.serverThreads.begin() != *run.clientThreads.begin(), "and they are not the same thread");
    Assert(!run.serverThreads.count(std::this_thread::get_id()), "nor this one");
}

Test(net_scene, a_player_hosts)
{
    // The game (a client) runs on the main thread, and the room (a server) on its own, in the same process
    LoopbackNetwork network;
    Run run;
    kuge::Engine engine(config(500));
    LoopbackNetwork* net = &network;

    engine.scenes().change<NetScene>(Hooks{});
    engine.spawn<NetScene>(kuge::RunPolicy::Dedicated, run.server([net](Net& n) -> Endpoint& { return n.listen("host", *net); }));
    waitUntil([&] { return run.listening.load(); });
    engine.spawn<NetScene>(kuge::RunPolicy::Main, run.client([net](Net& n) -> Endpoint& { return n.connect("host", *net); }));
    Assert(waitUntil([&] { engine.step(0.002); return run.answers == Run::COUNT; }), "the game gets its answers from the room (%u)", run.answers.load());
    AssertEq(run.wrong.load(), 0, "right ones");
    Assert(run.clientThreads.count(std::this_thread::get_id()) == 1, "the client scene ran on this thread");
    Assert(!run.serverThreads.count(std::this_thread::get_id()), "the room on another");
}

Test(net_scene, over_tcp)
{
    const std::uint16_t port = freePort(true);
    Run run;
    kuge::Engine engine(config(500));

    engine.spawn<NetScene>(kuge::RunPolicy::Dedicated, run.server([port](Net& n) -> Endpoint& { return n.listen(Protocol::Tcp, port); }));
    waitUntil([&] { return run.listening.load(); });
    engine.spawn<NetScene>(kuge::RunPolicy::Dedicated, run.client([port](Net& n) -> Endpoint& { return n.connect(Protocol::Tcp, "127.0.0.1", port); }));
    Assert(waitUntil([&run] { return run.answers == Run::COUNT; }), "tcp: %u answers", run.answers.load());
    AssertEq(run.wrong.load(), 0, "right ones");
}

Test(net_scene, over_udp)
{
    const std::uint16_t port = freePort(false);
    Run run;
    kuge::Engine engine(config(500));

    engine.spawn<NetScene>(kuge::RunPolicy::Dedicated, run.server([port](Net& n) -> Endpoint& { return n.listen(Protocol::Udp, port); }));
    waitUntil([&] { return run.listening.load(); });
    engine.spawn<NetScene>(kuge::RunPolicy::Dedicated, run.client([port](Net& n) -> Endpoint& { return n.connect(Protocol::Udp, "127.0.0.1", port); }));
    Assert(waitUntil([&run] { return run.answers == Run::COUNT; }), "udp: %u answers", run.answers.load());
    AssertEq(run.wrong.load(), 0, "right ones");
}

Test(net_scene, pooled_scenes_talk)
{
    // Four pairs, on 3 workers: a scene's endpoints are used by whichever worker runs its tick, never two at once
    LoopbackNetwork network;
    std::vector<std::unique_ptr<Run>> runs;
    kuge::Engine engine(config(500, 3));
    LoopbackNetwork* net = &network;

    for (int i = 0; i < 4; ++i) {
        runs.push_back(std::make_unique<Run>());
        const std::string name = "pool" + std::to_string(i);

        engine.spawn<NetScene>(kuge::RunPolicy::Pooled, runs[static_cast<std::size_t>(i)]->server([net, name](Net& n) -> Endpoint& { return n.listen(name, *net); }));
        waitUntil([&runs, i] { return runs[static_cast<std::size_t>(i)]->listening.load(); });   // (a client that comes first finds nobody)
        engine.spawn<NetScene>(kuge::RunPolicy::Pooled, runs[static_cast<std::size_t>(i)]->client([net, name](Net& n) -> Endpoint& { return n.connect(name, *net); }));
    }
    Assert(waitUntil([&runs] { return std::all_of(runs.begin(), runs.end(), [](const auto& r) { return r->answers == Run::COUNT; }); }), "the four pairs finish");
    for (auto& run : runs) {
        AssertEq(run->wrong.load(), 0, "right ones");
    }
}

Test(net_scene, a_scene_that_ends)
{
    // When a scene is left, its endpoints go and their peers are told at once
    LoopbackNetwork network;
    std::atomic<int> reason{-1};
    std::atomic<bool> connected{false};
    std::atomic<int> serverExits{0};
    kuge::Engine engine(config(200));
    LoopbackNetwork* net = &network;
    Hooks server;
    Hooks client;

    std::atomic<bool> listening{false};

    server.enter = [&listening, net](Net& n, kuge::SceneContext&) { n.listen("bye", *net); listening = true; };
    server.exit = [&serverExits](Net& n) { ++serverExits; (void)n; };
    client.enter = [&, net](Net& n, kuge::SceneContext&) {
        Endpoint& e = n.connect("bye", *net);

        e.onConnected([&](ConnectionId) { connected = true; });
        e.onDisconnected([&](ConnectionId, DisconnectReason r) { reason = static_cast<int>(r); });
    };
    const auto room = engine.spawn<NetScene>(kuge::RunPolicy::Dedicated, server);

    waitUntil([&listening] { return listening.load(); });
    engine.spawn<NetScene>(kuge::RunPolicy::Dedicated, client);
    Assert(waitUntil([&connected] { return static_cast<bool>(connected); }), "connected");
    room.stop();
    Assert(waitUntil([&reason] { return reason >= 0; }, 5.0), "the client is told, well before any timeout");
    AssertEq(reason.load(), static_cast<int>(DisconnectReason::Remote), "that the server left");
    AssertEq(serverExits.load(), 1, "and the scene was left once");
}

Test(net_scene, endpoints_from_handlers)
{
    // A handler that adds an endpoint and one that removes itself, while the Net is being polled
    LoopbackNetwork network;
    std::atomic<int> secondHeard{0};
    std::atomic<int> helloHeard{0};
    std::atomic<int> remaining{-1};
    std::atomic<bool> doorOpen{false};
    kuge::Engine engine(config(200));
    LoopbackNetwork* net = &network;
    Hooks lobby, first, second;

    lobby.enter = [&, net](Net& n, kuge::SceneContext&) {
        Endpoint& door = n.listen("door", *net);

        doorOpen = true;
        door.on<Ping>([&, net](ConnectionId, const Ping&) {
            ++helloHeard;
            if (helloHeard == 1) {
                Endpoint& room = n.listen("room", *net);                      // made while polling

                room.on<Number>([&](ConnectionId, const Number&) { ++secondHeard; });
                n.remove(door);                                               // and the first one goes
                remaining = static_cast<int>(n.count());
            }
        });
    };
    first.enter = [net](Net& n, kuge::SceneContext&) {
        Endpoint& e = n.connect("door", *net);

        e.onConnected([&e](ConnectionId id) { e.send(id, Ping{}); });
    };
    second.enter = [net](Net& n, kuge::SceneContext&) {
        Endpoint& e = n.connect("room", *net);

        e.onConnected([&e](ConnectionId id) { e.send(id, Number{1}); });
    };
    engine.spawn<NetScene>(kuge::RunPolicy::Dedicated, lobby);
    waitUntil([&doorOpen] { return doorOpen.load(); });
    engine.spawn<NetScene>(kuge::RunPolicy::Dedicated, first);
    Assert(waitUntil([&remaining] { return remaining >= 0; }, 10.0), "the first endpoint was used");   // (set at the end of the handler)
    AssertEq(remaining.load(), 1, "it was removed, the new one stays");
    engine.spawn<NetScene>(kuge::RunPolicy::Dedicated, second);   // (the room exists now: it was made by the handler)
    Assert(waitUntil([&secondHeard] { return secondHeard >= 1; }, 10.0), "and the endpoint made from the handler works");
}
