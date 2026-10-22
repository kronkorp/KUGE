#pragma once

#include "Chat.hpp"
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
struct SetTeam { KUGE_MESSAGE(SetTeam, team) std::uint32_t team = 0; };
struct TeamSay { KUGE_MESSAGE(TeamSay, text) std::string text; };
struct WinNow { KUGE_MESSAGE(WinNow) };
struct SetDoor { KUGE_MESSAGE(SetDoor, open) bool open = true; };
struct StartCine { KUGE_MESSAGE(StartCine, id, limit) std::string id; double limit = 30.0; };

// What the rooms of the tests did (they run on other threads: everything is under the mutex)
struct RoomLog
{
    std::mutex                                             mutex;
    std::vector<std::string>                               joined;
    std::vector<std::pair<std::string, kuge::net::DisconnectReason>> left;
    std::vector<std::string>                               away;       //!< Players whose connection was lost and whose place is kept
    std::vector<std::pair<std::string, std::uint32_t>>     rejoined;   //!< Players that came back: their name, and their network id
    std::vector<std::uint32_t>                             networkIds;
    std::vector<std::uint32_t>                             playerIds;   //!< Player::playerId of each player that joined
    std::vector<std::string>                               addresses;   //!< Player::address of each player that joined
    std::vector<std::thread::id>                           threads;
    std::vector<std::pair<std::string, bool>>              roomNames;   //!< What each room was told of its name, and of its privacy
    std::vector<std::string>                               roomOptions; //!< ... and of its options
    int                                                    entered = 0;
    int                                                    exited = 0;
    std::atomic<int>                                       roomsListening{0};
    std::atomic<int>                                       finished{0};   //!< Rooms that were told to finish
    std::vector<std::pair<std::string, bool>>              cinematics;    //!< Cinematics that ended: the id, and whether all players finished it

    std::size_t joinedCount(void) { std::lock_guard lock(mutex); return joined.size(); }
    std::size_t leftCount(void) { std::lock_guard lock(mutex); return left.size(); }
    std::size_t awayCount(void) { std::lock_guard lock(mutex); return away.size(); }
    std::size_t rejoinedCount(void) { std::lock_guard lock(mutex); return rejoined.size(); }
    std::size_t cinematicCount(void) { std::lock_guard lock(mutex); return cinematics.size(); }
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
                roomLog().roomNames.emplace_back(init().roomName, init().isPrivate);
                roomLog().roomOptions.push_back(init().options);
            }
            enableChat({.maxBytes = 40, .burst = 2, .refillSeconds = 1000.0});   // (a bucket that does not refill in a test)
            on<Echo>([this](const Player& who, const Echo& echo) { send(who.networkId, Echo{who.name + ": " + echo.text}); });
            on<SetDoor>([this](const Player&, const SetDoor& door) { setOpen(door.open); });
            on<SetTeam>([this](const Player& who, const SetTeam& team) { setTeam(who.networkId, team.team); });
            on<TeamSay>([this](const Player& who, const TeamSay& say) { broadcastTeam(who.team, Echo{who.name + " (team): " + say.text}); });
            on<StartCine>([this](const Player&, const StartCine& start) { playCinematic(start.id, start.limit); });
            on<WinNow>([this](const Player&, const WinNow&) { finish(kuge::net::RoomEnd::Victory); ++roomLog().finished; });
            on<FinishNow>([this](const Player&, const FinishNow&) { finish(kuge::net::RoomEnd::GameOver); ++roomLog().finished; });
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
            roomLog().addresses.push_back(player.address);
            roomLog().playerIds.push_back(player.playerId);
        }

        void onCinematicEnded(const std::string& id, bool completed) override
        {
            std::lock_guard lock(roomLog().mutex);
            roomLog().cinematics.emplace_back(id, completed);
        }

        void onPlayerAway(const Player& player, kuge::net::DisconnectReason) override
        {
            std::lock_guard lock(roomLog().mutex);
            roomLog().away.push_back(player.name);
        }

        void onPlayerRejoined(const Player& player, kuge::net::ConnectionId) override
        {
            std::lock_guard lock(roomLog().mutex);
            roomLog().rejoined.emplace_back(player.name, player.networkId);
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
        roomLog().away.clear();
        roomLog().rejoined.clear();
        roomLog().networkIds.clear();
        roomLog().playerIds.clear();
        roomLog().addresses.clear();
        roomLog().threads.clear();
        roomLog().roomNames.clear();
        roomLog().roomOptions.clear();
        roomLog().entered = roomLog().exited = 0;
        roomLog().finished = 0;
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
    std::vector<kuge::net::ChatLine> chats;
    std::vector<kuge::net::RoomEnd> closed;
    std::vector<std::string>        failures;
    std::vector<std::string>        cinematicsStarted;
    std::vector<std::string>        cinematicsStopped;
    std::vector<kuge::net::RoomList> lists;   //!< What the lobby said when asked for the rooms
    int                             joined = 0;

    explicit TestClient(std::string playerName) : name(std::move(playerName))
    {
        matchmaking.onRoomList([this](const kuge::net::RoomList& list) { lists.push_back(list); });
        matchmaking.onJoined([this](kuge::net::Endpoint& room, const kuge::net::Welcome& welcome) {
            ++joined;
            roomId = welcome.roomId;
            networkId = welcome.networkId;
            room.on<Echo>([this](kuge::net::ConnectionId, const Echo& echo) { echoes.push_back(echo.text); });
            room.on<kuge::net::CinematicStart>([this](kuge::net::ConnectionId, const kuge::net::CinematicStart& m) { cinematicsStarted.push_back(m.id); });
            room.on<kuge::net::CinematicStop>([this](kuge::net::ConnectionId, const kuge::net::CinematicStop& m) { cinematicsStopped.push_back(m.id); });
            room.on<kuge::net::ChatLine>([this](kuge::net::ConnectionId, const kuge::net::ChatLine& line) { chats.push_back(line); });
        });
        matchmaking.onRoomClosed([this](kuge::net::RoomEnd why) { closed.push_back(why); });
        matchmaking.onFailed([this](const std::string& why) { failures.push_back(why); });
    }

    void connect(Harness& harness, double timeout = 0.0)
    {
        kuge::net::EndpointConfig config;

        if (timeout > 0.0) {
            config.timeout = timeout;   // how soon this client finds out that it has lost the server
        }
        matchmaking.connectLobby("lobby", harness.network, config);
    }

    void join(const char* roomType = "duel")
    {
        matchmaking.join(roomType, name);
    }

    void create(const std::string& roomName, bool isPrivate = false, const char* roomType = "duel", const std::string& options = "")
    {
        matchmaking.createRoom(roomType, roomName, name, isPrivate, options);
    }

    void joinNamed(std::uint32_t id, const std::string& roomName)
    {
        matchmaking.joinRoom(id, roomName, name);
    }

    // Asks for the rooms and waits for the answer
    const kuge::net::RoomList* list(const char* roomType = "")
    {
        const std::size_t before = lists.size();

        matchmaking.requestRooms(roomType);
        if (!until([&] { return lists.size() > before; })) {
            return nullptr;
        }
        return &lists.back();
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
