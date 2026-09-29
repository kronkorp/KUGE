# 09 The server

`kuge-server` is a game server with **no window and no drawing code**: it needs only the core and the network
(a test checks that it cannot include the client). It gives you a **lobby** where players arrive, and **rooms**,
one per game, that the lobby makes for them.

## The flow

```
 client                        lobby (well known address)                room (its own address)
   | -- JoinRoom ------------->  |                                          |
   | <-- RoomAssigned(address, token)                                       |
   | ------------------------- connect ----------------------------------> |
   | ------------------------- Hello(token) ----------------------------->  |
   | <------------------------ Welcome(networkId) -------------------------- |
   |                          ... the game ...                              |
   | <------------------------ RoomClosed --------------------------------- |
   | (back in the lobby, which the client never left)
```

Why two steps? A lobby has one address that everybody knows; each room has its own. The lobby decides *which*
room and hands the player a **token**, a one-time key to that room's door. A stranger who guesses the room's
address gets nowhere without the token.

## Writing a room

You derive from `RoomScene` and make the game. Nothing in it is about a screen.

```cpp
class DeathmatchRoom : public kuge::server::RoomScene
{
    public:
        using RoomScene::RoomScene;                       // built with a RoomInit

    protected:
        void onRoomEnter() override
        {
            on<Input>([this](const Player& who, const Input& input) { ... });      // messages of the game, from players only
            addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Simulate>());
        }
        void onPlayerJoined(const Player& player) override { send(player.networkId, Welcome...); }
        void onPlayerLeft(const Player& player, kuge::net::DisconnectReason) override { ... }
};
```

A room is a **scene**: it has a World, systems, and runs on its own thread (or a worker). `RoomScene` gives
you:

| | |
|---|---|
| `endpoint()` | The room's `Endpoint` (already listening) |
| `on<T>(handler)` | A handler for a game message, called only for messages from **players** (clients that said `Hello` with a good token) |
| `send(networkId, message)`, `broadcast(message)` | To one player, or to all |
| `players()`, `player(connection)` | Who is in the room |
| `kick(networkId)` | Sends a player away |
| `finish(reason)` | Ends the game: players are told, then the room is left |
| `init()` | The `RoomInit`: room id, type, settings |
| hooks | `onRoomEnter`, `onRoomExit`, `onPlayerJoined`, `onPlayerLeft`, `onRoomMessage` |

A `Player` has `playerId` (server-wide), `name`, `networkId` (**in this room**: 1, 2, 3…, never reused: how the
game names this player), and `connection`.

## Running the server

```cpp
int main()
{
    kuge::server::GameServer server({.lobbyPort = 4242});

    server.addRoomType<DeathmatchRoom>("deathmatch", {.maxPlayers = 8, .idleTimeout = 30.0});
    return server.run();                    // until Ctrl+C: every room is closed properly
}
```

`GameServer` is an engine in `Headless` mode with a lobby scene. `ServerConfig` says how it is reached:

| Field | |
|---|---|
| `transport` | `Sockets` (a lobby on `lobbyPort`, rooms on UDP ports from `roomPortFirst`) or `Loopback` (by name, in the process) |
| `lobbyProtocol` | TCP by default: the connection stays for the whole session, and losing it is felt at once |
| `roomPortFirst`, `roomPortCount` | The range of UDP ports rooms use |
| `roomAddress` | The host that clients are told to use for a room (empty: the one they used for the lobby) |
| `maxRooms` | The most rooms at once |
| `tokenTtl`, `helloTimeout` | How long a token lives; how long a connection may stay silent before `Hello` |
| `closeDelay`, `linger` | How long a room that finishes runs before it tells its players (so the last snapshots leave first; 0: at once); then how long it waits for its last messages |
| `endpoint` | `EndpointConfig` for the lobby and the rooms (timeouts, the clock) |

`RoomTypeConfig`: `maxPlayers`, `idleTimeout` (a room with no player for this long closes) and `policy`
(`Dedicated` or `Pooled`: where rooms run).

`server.stats()` counts rooms, players, lobby clients, joins and refusals, readable from **any thread**.

## What the lobby does

- It finds a room of the asked kind that has a free place (**the fullest first**: games start sooner), or makes
  one, up to `maxRooms`. It refuses with a reason (`JoinError`): unknown kind, full, room could not start.
- It keeps the TCP connection of each client: **losing it takes the player out of the room**.
- When a room ends, it gets the place and the port back. The lobby learns that a game is over *before* the players
  do, so a client asking for another game at once is not told "already in a room".
- A client that never reaches its room loses its place after the token's lifetime.
- A port that could not be bound is set aside; the next room uses another.

## The door of a room

A client that connects to a room must say `Hello{token}` within `helloTimeout`, or it is dropped. The token opens
the door **once** (a stolen or reused token is `Rejected`), and expires. Anything a client sends before
its `Hello` is **never given to your game**: your `on<T>` handlers only see players.

## The end of a game

`finish()` (the game is over), an idle room (`idleTimeout`), or the server stopping: the players are told
(`RoomClosed`), the last messages get a moment to leave (`linger`), and the scene is left. The clients are back
in the lobby and can ask for another game.

After `finish()` the room keeps running for `closeDelay` (0.1 s by default) before it sends `RoomClosed`. A client
lets go of the room's replication when it hears `RoomClosed`, and the snapshots are not ordered with it: without the
delay, what the game did in the tick it ended (a result, a last state) would leave *after* the message that makes the
client stop listening. Whatever the game wants its players to see at the end, it changes before or when it calls
`finish()`.

## Testing a server

Use the `Loopback` transport and run the server on a thread of your test:

```cpp
kuge::net::LoopbackNetwork network;
kuge::server::ServerConfig config;

config.transport = kuge::server::Transport::Loopback;
config.loopback = &network;
kuge::server::GameServer server(config);

server.addRoomType<MyRoom>("game", {.maxPlayers = 2});
std::thread thread([&server] { server.run(); });
// ... clients connect to "lobby" on `network` ...
server.stop();
thread.join();
```

`tests/server` and `tests/rtype` do this for tokens, full rooms, cut cables, several rooms, stopping with
players, and the real sockets.

Next: [10 Replication and prediction](../10-replication-and-prediction/README.md).
