#pragma once

#include "GameServer.hpp"
#include "Matchmaking.hpp"
#include "SocketTransport.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

// Messages of the game of the tests
struct Echo
{
    KUGE_MESSAGE(Echo, text)
    std::string text;
};

struct FinishNow { KUGE_MESSAGE(FinishNow) };

// What the rooms of the tests did (they run on other threads: everything is under the mutex)
struct RoomLog
{
    std::mutex                                             mutex;
    std::vector<std::string>                               joined;
    std::vector<std::pair<std::string, kuge::net::DisconnectReason>> left;
    std::vector<std::uint32_t>                             networkIds;
    std::vector<std::thread::id>                           threads;
    int                                                    entered = 0;
    int                                                    exited = 0;
    std::atomic<int>                                       roomsListening{0};

    std::size_t joinedCount(void) { std::lock_guard lock(mutex); return joined.size(); }
    std::size_t leftCount(void) { std::lock_guard lock(mutex); return left.size(); }
};

inline RoomLog& roomLog(void)
{
    static RoomLog log;

    return log;
}

// A room that echoes, and ends when it is told to
class TestRoom : public kuge::server::RoomScene
{
    public:
        using RoomScene::RoomScene;

    protected:
        void onRoomEnter(void) override
        {
            {
                std::lock_guard lock(roomLog().mutex);
                ++roomLog().entered;
                roomLog().threads.push_back(std::this_thread::get_id());
            }
            on<Echo>([this](const Player& who, const Echo& echo) { send(who.networkId, Echo{who.name + ": " + echo.text}); });
            on<FinishNow>([this](const Player&, const FinishNow&) { finish(kuge::net::RoomEnd::GameOver); });
        }

        void onRoomExit(void) override
        {
            std::lock_guard lock(roomLog().mutex);
            ++roomLog().exited;
        }

        void onPlayerJoined(const Player& player) override
        {
            std::lock_guard lock(roomLog().mutex);
            roomLog().joined.push_back(player.name);
            roomLog().networkIds.push_back(player.networkId);
        }

        void onPlayerLeft(const Player& player, kuge::net::DisconnectReason reason) override
        {
            std::lock_guard lock(roomLog().mutex);
            roomLog().left.emplace_back(player.name, reason);
        }
};

inline bool waitUntil(const std::function<bool(void)>& done, double seconds = 20.0)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);

    while (!done()) {
        if (std::chrono::steady_clock::now() > end) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

// A running server (on its own thread) on a loopback network of the process
struct Harness
{
    kuge::net::LoopbackNetwork                 network;
    std::unique_ptr<kuge::server::GameServer>  server;
    std::thread                                thread;

    explicit Harness(std::function<void(kuge::server::ServerConfig&)> tune = {},
        kuge::server::RoomTypeConfig duel = {.maxPlayers = 2, .idleTimeout = 30.0},
        bool addDuel = true)
    {
        kuge::server::ServerConfig config;

        roomLog().joined.clear();
        roomLog().left.clear();
        roomLog().networkIds.clear();
        roomLog().threads.clear();
        roomLog().entered = roomLog().exited = 0;
        config.transport = kuge::server::Transport::Loopback;
        config.loopback = &network;
        config.tickRate = 200;
        config.endpoint.timeout = 3.0;
        if (tune) {
            tune(config);
        }
        server = std::make_unique<kuge::server::GameServer>(config);
        if (addDuel) {
            server->addRoomType<TestRoom>("duel", duel);
        }
        thread = std::thread([this] { server->run(); });
        waitUntil([this] { return server->stats().lobbyOpen.load(); });
    }

    ~Harness(void)
    {
        server->stop();
        thread.join();
    }
};

// A client of the tests: its own Net, polled by the test thread
struct TestClient
{
    kuge::net::Net                  net;
    kuge::net::MatchmakingClient    matchmaking{net};
    std::string                     name;
    std::uint32_t                   roomId = 0;
    std::uint32_t                   networkId = 0;
    std::vector<std::string>        echoes;
    std::vector<kuge::net::RoomEnd> closed;
    std::vector<std::string>        failures;
    int                             joined = 0;

    explicit TestClient(std::string playerName) : name(std::move(playerName))
    {
        matchmaking.onJoined([this](kuge::net::Endpoint& room, const kuge::net::Welcome& welcome) {
            ++joined;
            roomId = welcome.roomId;
            networkId = welcome.networkId;
            room.on<Echo>([this](kuge::net::ConnectionId, const Echo& echo) { echoes.push_back(echo.text); });
        });
        matchmaking.onRoomClosed([this](kuge::net::RoomEnd why) { closed.push_back(why); });
        matchmaking.onFailed([this](const std::string& why) { failures.push_back(why); });
    }

    void connect(Harness& harness)
    {
        matchmaking.connectLobby("lobby", harness.network);
    }

    void join(const char* roomType = "duel")
    {
        matchmaking.join(roomType, name);
    }

    void poll(void) { net.poll(); }

    bool until(const std::function<bool(void)>& done, double seconds = 20.0)
    {
        return waitUntil([this, &done] { poll(); return done(); }, seconds);
    }

    bool inRoom(void) { return matchmaking.state() == kuge::net::MatchmakingClient::State::InRoom; }
    bool inLobby(void) { return matchmaking.state() == kuge::net::MatchmakingClient::State::InLobby; }
};

// Polls several clients until done
template<typename Done>
bool pumpAll(std::vector<std::unique_ptr<TestClient>>& clients, Done done, double seconds = 30.0)
{
    return waitUntil([&] {
        for (auto& client : clients) {
            client->poll();
        }
        return done();
    }, seconds);
}
