# Step 12: Lobby and rooms

`kuge-server` is built entirely from what the previous steps showed: scenes (5), spawned on threads (7), that talk
through mailboxes, each with endpoints (10, 11). This step follows a player from `join("hello", "Ana")` to the room's
`Welcome`, and back.

## What a server is made of

```cpp
// modules/server/src/GameServer.cpp
GameServer::GameServer(ServerConfig config)
    : m_state(std::make_shared<ServerState>()),
      m_engine(Engine::Config{.mode = Engine::Mode::Headless, .tickRate = config.tickRate, .workers = config.workers})
{ ... }

void GameServer::start(void)
{
    m_engine.scenes().change<LobbyScene>(m_state);    // the lobby is the engine's main scene
}

int GameServer::run(void)
{
    start();
    return m_engine.run();                            // step 2's loop, with no window
}
```

A server is an `Engine` in `Headless` mode, whose main scene is the **lobby**. Each room is a scene the lobby
**spawns**, on a thread of its own (`RunPolicy::Dedicated`) or on the workers (`Pooled`). There are two kinds of
talk:

- **the network**, between a client and the lobby (TCP by default) or a room (UDP);
- **scene messages** (step 7), between the lobby and its rooms: `ExpectPlayer`, `KickPlayer` one way; `RoomReady`,
  `PlayerJoined`, `PlayerLeft`, `RoomClosing`, `RoomEnded`, `RoomFailed` the other.

## The path of a join

```
client (MatchmakingClient)          lobby scene (main thread)                  room scene (its own thread)
 connect ───────── TCP ──────────▶ Handshaking → Connected
 JoinRoom{"hello", "Ana"} ───────▶ tryJoin: a room of that kind with a place?
                                    none: makeRoom → spawnScene(Dedicated) ──▶ onEnter: listen (UDP port or name)
                                    Member{playerId, token = 64 random bits}      ... RoomReady ──▶ (lobby's mailbox)
                                    ExpectPlayer{playerId, token} ─ mailbox ──▶ m_expected[token]
                ◀── RoomAssigned{address, port, token}   (once the room is ready)
 connect ────────────────────────────── UDP ───────────────────────────────▶ m_unknown (no hello yet)
 Hello{token} ─────────────────────────────────────────────────────────────▶ admit: token known? used once
                ◀────────────────────────────────── Welcome{networkId, roomId, tickRate}
                                    ◀── PlayerJoined ─ mailbox ─                onPlayerJoined(player)
```

```cpp
// modules/server/src/Lobby.cpp, LobbyScene::tryJoin (shortened)
... wrong protocol, unknown kind of room: refused (JoinRefused)
... already in a room: that room may be ending, and the lobby not know yet: tried again for 0.25 s
Room* room = findRoom(request.roomType);                  // the fullest one that still has a place
if (!room && m_rooms.size() < m_state->config.maxRooms) {
    room = makeRoom(request.roomType, kind->second);      // a port from the range, a RoomScene, spawned
}
Member member{m_nextPlayer++, from, room->id, newToken(), request.playerName, m_state->config.now(), false};

room->handle.send(detail::ExpectPlayer{member.playerId, member.name, member.token});   // a scene message
... the player is counted in the room, and remembered by its connection
if (room->ready) {
    assign(member, *room);                                // RoomAssigned: where, and the token
} else {
    room->waiting.push_back(member.playerId);             // told when the room says RoomReady
}
```

```cpp
// modules/server/src/RoomScene.cpp, RoomScene::admit
const auto found = m_expected.find(token);
if (found == m_expected.end()) {
    return false;                                         // unknown: it waits HELLO_GRACE (1 s), then Rejected
}
Player who{found->second.playerId, found->second.name, m_nextNetworkId++, from};

m_expected.erase(found);                                  // a token opens the door once
m_players[who.networkId] = who;
m_endpoint->send(from, net::Welcome{who.networkId, m_init.roomId, m_init.server.tickRate});
tellLobby(detail::PlayerJoined{m_init.roomId, who.playerId});
onPlayerJoined(who);                                      // the game's code
```

Why the grace period: the lobby sends `ExpectPlayer` to the room's mailbox, and `RoomAssigned` to the client, at
about the same moment. A fast client's `Hello` can reach the room before the room's next loop has drained its
mailbox. So an unknown token is given a second before it is refused.

## Housekeeping: what the timers do

Both scenes have a system in the `Input` stage that looks at the clock:

- **the room** drops connections that never say hello (`helloTimeout`, 5 s), forgets tokens that expired
  (`tokenTtl`, 10 s), and closes itself when empty for `idleTimeout`;
- **the lobby** forgets players who never reached their room (after `tokenTtl` + 1 s), and ends the rooms whose
  handle is not `alive()` any more (a room that died without a word).

`finish()` in a room: `RoomClosing` to the lobby **first** (so a player who asks for a new game at once is not told
"already in a room"), `RoomClosed` to the players, a moment (`linger`) for those messages to leave, then
`RoomEnded` and the scene pops itself.

## The client's side: `MatchmakingClient`

| State | Means | Left when |
|---|---|---|
| `ConnectingLobby` | `connectLobby` called | the lobby's endpoint is connected: `Joining` (a `join()` asked earlier is sent now) |
| `Joining` | `JoinRoom` sent | `RoomAssigned`: `ConnectingRoom` (a room endpoint is made); `JoinRefused`: `InLobby` |
| `ConnectingRoom` | connected (or connecting) to the room, `Hello` sent | `Welcome`: `InRoom`, `onJoined(room, welcome)` |
| `InRoom` | the game talks on the room's endpoint | `RoomClosed`, room lost, `leave()`: `InLobby`, `onRoomClosed` |
| `Failed` | the lobby was lost | `connectLobby` again |

Every transition happens **inside a handler**, so inside `Net::poll`, in the `Network` stage of the client's tick.
That has a consequence the lab shows.

## Lab

`example/lab/lab12_server.cpp`:

```cpp
// Lab 12: a real GameServer on a thread, and a client that joins a room through the lobby.
// Everything goes through a loopback network of this process: no port is opened.
#include "Engine.hpp"
#include "GameServer.hpp"
#include "Logger.hpp"
#include "Matchmaking.hpp"
#include "Net.hpp"
#include "RoomScene.hpp"
#include "Stage.hpp"
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <thread>

namespace
{
    struct Greeting
    {
        KUGE_MESSAGE(Greeting, text)
        std::string text;
    };

    // The smallest room: it greets whoever comes in
    class HelloRoom final : public kuge::server::RoomScene
    {
        public:
            using RoomScene::RoomScene;

        protected:
            void onPlayerJoined(const Player& player) override
            {
                send(player.networkId, Greeting{"welcome, " + player.name + " (network id " + std::to_string(player.networkId) + ")"});
            }
    };

    const char* name(kuge::net::MatchmakingClient::State state)
    {
        using State = kuge::net::MatchmakingClient::State;
        switch (state) {
            case State::Idle:            return "Idle";
            case State::ConnectingLobby: return "ConnectingLobby";
            case State::InLobby:         return "InLobby";
            case State::Joining:         return "Joining";
            case State::ConnectingRoom:  return "ConnectingRoom";
            case State::InRoom:          return "InRoom";
            case State::Failed:          return "Failed";
        }
        return "?";
    }

    class Watch final : public kw::ISystem
    {
        public:
            explicit Watch(std::function<void(void)> fn) : m_fn(std::move(fn)) {}
            bool handle(kw::World&) override { m_fn(); return true; }

        private:
            std::function<void(void)> m_fn;
    };

    // The client: a scene with a Net and a MatchmakingClient
    class Player final : public kuge::Scene
    {
        public:
            explicit Player(kuge::net::LoopbackNetwork* network) : m_network(network) {}

            void onEnter(void) override
            {
                kuge::net::installNet(setup());
                m_matchmaking = std::make_unique<kuge::net::MatchmakingClient>(world().getResource<kuge::net::Net>());
                m_matchmaking->onJoined([this](kuge::net::Endpoint& room, const kuge::net::Welcome& welcome) {
                    m_joined = true;
                    std::printf("state: %-16s onJoined: room %u, network id %u, tick rate %u\n", name(m_matchmaking->state()),
                        welcome.roomId, welcome.networkId, welcome.tickRate);
                    room.on<Greeting>([this](kuge::net::ConnectionId, const Greeting& greeting) {
                        std::printf("  the room says: \"%s\"\n", greeting.text.c_str());
                        m_matchmaking->leave();                    // done: back to the lobby
                        std::printf("state: %-16s after leave()\n", name(m_matchmaking->state()));
                        ctx().engine().stop();
                    });
                });
                m_matchmaking->connectLobby("lobby", *m_network);
                m_matchmaking->join("hello", "Ana");           // may be asked at once: it waits for the lobby
                // What a system sees at the end of each tick (the handlers above print what happens inside a poll)
                addSystem(kw::Schedule::Fixed, kuge::stage::Late, std::make_unique<Watch>([this] {
                    if (!m_joined && m_matchmaking->state() != m_last) {
                        m_last = m_matchmaking->state();
                        std::printf("state: %s\n", name(m_last));
                    }
                }));
            }

        private:
            kuge::net::LoopbackNetwork*                   m_network;
            std::unique_ptr<kuge::net::MatchmakingClient> m_matchmaking;
            kuge::net::MatchmakingClient::State           m_last = kuge::net::MatchmakingClient::State::Idle;
            bool                                          m_joined = false;
    };
}

int main()
{
    Logger::logger().enable(false);
    kuge::net::LoopbackNetwork network;
    kuge::server::ServerConfig config;

    config.transport = kuge::server::Transport::Loopback;
    config.loopback = &network;
    kuge::server::GameServer server(config);

    server.addRoomType<HelloRoom>("hello", {.maxPlayers = 2});
    std::thread serverThread([&server] { server.run(); });
    while (!server.stats().lobbyOpen) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    {
        kuge::Engine client({.mode = kuge::Engine::Mode::Headless, .tickRate = 60});

        client.scenes().change<Player>(&network);
        client.run();
    }
    std::printf("the server made %zu room(s) and had %zu join(s)\n", server.stats().roomsMade.load(), server.stats().joins.load());
    server.stop();
    serverThread.join();
}
```

What it prints (`./build/example/lab12_server`):

```text
state: ConnectingLobby
state: Joining
state: ConnectingRoom
state: InRoom           onJoined: room 1, network id 1, tick rate 60
  the room says: "welcome, Ana (network id 1)"
state: InLobby          after leave()
the server made 1 room(s) and had 1 join(s)
```

## Reading the output

- The first three states are seen by the `Watch` system, at the end of three different ticks: each change waited for
  an answer from the other side (the lobby's `Accept`, then its `RoomAssigned`).
- `InRoom` and `InLobby` are printed from the handlers. That is deliberate. The room sends `Welcome`, then (in
  `onPlayerJoined`) the greeting, in the same tick: both arrive in the **same poll** of the client. So `onJoined`
  runs, the greeting handler runs and calls `leave()`, and the state is back to `InLobby`, all inside one
  `Net::poll`. A system that checks the state once per tick would never see `InRoom`. (An earlier version of this lab
  did exactly that, and waited forever.) React to changes in the callbacks, not by checking the state.
- The server made one room, spawned on its own thread when the first join came, and counted one join. Nothing went
  through a socket: the lobby and the room listened on names of a `LoopbackNetwork`.

Next: [Step 13: Replication](../13-replication/README.md).
