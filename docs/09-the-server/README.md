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
| `setOpen(bool)` | Opens or closes the room to newcomers (see "A room that starts" below) |
| `finish(reason)` | Ends the game: players are told, then the room is left |
| `init()` | The `RoomInit`: room id, type, settings |
| hooks | `onRoomEnter`, `onRoomExit`, `onPlayerJoined`, `onPlayerLeft`, `onRoomMessage` |

A `Player` has `playerId` (server-wide), `name`, `networkId` (**in this room**: 1, 2, 3…, never reused: how the
game names this player), `connection`, and `address`: the IP the connection comes from, without the port (`"203.0.113.7"`, `"2001:db8::1"`;
`net::addressOf(endpoint.peer(connection))` is how the room makes it). The handshake proved it, but it is not a player: several players can share
one address, and one player can change it. For stats by player, use `playerId` with authentication (below).

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
  one, up to `maxRooms`. It refuses with a reason (`JoinError`): unknown kind, full, room could not start, room closed to newcomers (`Started`).
- It can also be told which room to use (see "Naming, listing and choosing rooms" below). It never fills a private
  room by chance.
- It keeps the TCP connection of each client: **losing it takes the player out of the room**.
- When a room ends, it gets the place and the port back. The lobby learns that a game is over *before* the players
  do, so a client asking for another game at once is not told "already in a room".
- A client that never reaches its room loses its place after the token's lifetime.
- A port that could not be bound is set aside; the next room uses another.

## Naming, listing and choosing rooms

`join("deathmatch", "Ana")` lets the lobby choose. To let a player choose, the client has three more calls, and the
lobby keeps the connection all along, so a client can look, leave, and join another:

```cpp
matchmaking.onRoomList([](const kuge::net::RoomList& list) {
    for (const auto& room : list.rooms) {          // public rooms only, by id, at most 64 (list.total: how many)
        // room.roomId, room.name, room.roomType, room.players, room.maxPlayers, room.options
    }
});
matchmaking.requestRooms("deathmatch");            // "" for every kind: the answer goes to onRoomList

matchmaking.createRoom("deathmatch", "Les copains", "Ana");          // opens it, and Ana is in it
matchmaking.createRoom("deathmatch", "Secret", "Ana", true);         // private: in no list
matchmaking.createRoom("deathmatch", "Vite", "Ana", false, "hurry=1");   // with options (below)
matchmaking.joinRoom(12, "Secret", "Ben");                           // the id AND the name must be the room's
```

- A name is **1 to 32 bytes of UTF-8, with no control character**. The blanks at its ends (space, tab, line break, vertical tab, form feed: a code pasted from a chat ends with one) are dropped. The lobby
  refuses another name with `JoinError::InvalidName`; `validRoomName()` and `trimRoomName()` let a UI check it first.
- **A private room** is in no list and `join()` never puts anyone in it. The only way in is its name and its id
  (`JoinNamedRoom`). Every way to be wrong (no such room, another name, a room that is ending) gets the same
  `JoinError::UnknownRoom`, so a private room cannot be told from one that does not exist. Be aware of how little
  that protects: the ids are 1, 2, 3..., so **the name is the only secret**, and there is no password. A room that is
  full is `JoinError::Full`.
- **Options.** `createRoom(..., options)` carries a text that KUGE does not read: the game puts what it likes in it
  (`"hurry=1;map=3"`, a compact JSON...). It is **at most 128 bytes** (`MAX_ROOM_OPTIONS`) of valid UTF-8 with no
  control character (`validRoomOptions()` lets a UI check it); another text is refused with
  `JoinError::InvalidOptions` and no room is made. The room reads it in `init().options` (in `onRoomEnter`), and the
  list carries it in `RoomInfo::options`, so a menu can show how a room is set without a kind of room per
  combination. A room that the lobby makes for `join()` has none. Options are chosen by the creator and are not
  checked by KUGE beyond this: **the game validates them** (a client may send any text that fits).
- A public room is filled by `join()` too, like any other.
- The room sees its own name, whether it is private and its options in `init().roomName`, `init().isPrivate` and
  `init().options`.
- The lobby does not limit how often a client asks for the list.

## Who a player is

KUGE does not know what an account is: the game does. A client gives the token it got from the game (`matchmaking.setToken(...)`: a ticket from your
backend, a signed text, at most 512 bytes), and the server turns it into a player:

```cpp
kuge::server::ServerConfig config;

config.authenticate = [&](const std::string& token, const std::string& address) -> std::optional<std::uint32_t> {
    return accounts.playerOfTicket(token);          // the id of the account, not 0; nothing: refused
};
```

The lobby asks it for every request to join a room (`JoinRoom`, `CreateRoom`, `JoinNamedRoom`), on its own thread, so it must be quick: check a signature or
a table, do not call a backend there. A player it does not know is told `JoinError::Unauthenticated` (`stats().authRefused` counts them), and the id it
answers is `Player::playerId` in the room, the same every time that account plays. The same id twice at once is refused as `AlreadyJoined`: the second
connection of an account uses `rejoin()` (it has the key) or waits for the first to go. Without `authenticate`, nothing is checked and the lobby numbers the
players itself.

The token travels in the clear, like everything else (see the RFC, section 10): give out tokens that are used once or run out quickly, and keep the secrets of
your backend out of the game.

## A text chat

`enableChat()` in `onRoomEnter()` is all a room writes:

```cpp
void onRoomEnter() override { enableChat({.maxBytes = 200, .burst = 4, .refillSeconds = 1.0}); }

std::optional<std::string> onChatLine(const Player& who, std::string text) override
{
    return isMuted(who) ? std::nullopt : std::optional(std::move(text));      // refuse or change a line that passed
}
```

A client sends `kuge::net::ChatSend{text}`; the room relays `ChatLine{from, text}` to everybody (`from` is the network id of the speaker).
A client is not trusted, so before that a `ChatRelay` checks the line: not empty once its spaces are cut, at most `maxBytes` (bytes, not
characters), **well-formed UTF-8**, **no control character** (a line break, a tab, an escape, the C1 controls), and a rate limit per
player (`burst` lines, one more every `refillSeconds`). A line that fails costs nothing; the bucket of a player is forgotten when it
leaves. Names, channels (team, all), commands and a mute list are the game's. `utf8Valid`, `utf8Length` and `trimSpaces` (`Utf8.hpp`)
are there for your own checks. On the client, `UiChatLog` is the widget (see [05](../05-the-client/README.md)).

## A player who loses its connection

A cable that is cut, a Wi-Fi that drops: by default the player is gone (`onPlayerLeft`) and comes back as a new one. A room type can keep its
place instead:

```cpp
server.addRoomType<DeathmatchRoom>("deathmatch", {.maxPlayers = 8, .reconnectSeconds = 30.0});
```

When the connection is **lost** (a timeout; not a player who leaves, is kicked, or a game that ends) the room calls `onPlayerAway(player, reason)`
and keeps the `Player` in `players()` with `away` set and no connection (`broadcast` and `send` skip it). Remove its replication client
and its input there; what the avatar does meanwhile (stands, is driven by a bot, dies) is the game's. If the player comes back in time,
`onPlayerRejoined(player, previousConnection)` is called, with the **same `Player` and the same network id**: give it again to the
replication (`addClient(player.connection)`) and it is sent the whole world. If the time runs out, `onPlayerLeft` follows as usual.

The client does the rest: when it hears that its room (or the lobby) is lost it connects to the lobby again and calls
`matchmaking.rejoin()`. Coming back needs the `rejoinKey` that the lobby gave with the room (`RoomAssigned`): it is the proof that this
connection is the player that lost its own, and anyone who watches the traffic can read it (see the RFC). `canRejoin()` says if there is
a room to come back to. The lobby answers `JoinRefused(UnknownRoom)` to everything that is wrong (no such player, a wrong key, a room that
no longer keeps the place), so that ids cannot be tried.

## Teams

`Player::team` is a number the game sets (`setTeam(networkId, team)`; 0: none). KUGE does nothing with it but pick who a message goes to:
`broadcastTeam(team, message)` is the team chat, what only allies may know. What a team is, who may change it, a score, a side swap,
rounds with a buy phase: all of that is the game's. (A phase machine for rounds is not in the engine on purpose: a timer in a `Fixed`
system is a few lines, and the phases are the rules of the game.)

## A cinematic that stops the game

A boss that enters, a round that ends: every player must see it before the game goes on.

```cpp
if (playCinematic("boss_entrance", 20.0)) { m_paused = true; }     // the game decides what "stopped" is
void onCinematicEnded(const std::string& id, bool completed) override { m_paused = false; }
```

The players there get `net::CinematicStart{id}`; the client plays the timeline (`CinematicScene`, see [05](../05-the-client/README.md#cinematics))
and answers `net::CinematicDone{id}` when it ends or the player skips it. The room waits for everybody, for at most `timeLimit` seconds (the
ones still playing get `net::CinematicStop{id}` and skip it): then `onCinematicEnded(id, completed)`. A player who leaves, or whose connection
is lost, is no longer waited for. One at a time (`playCinematic` returns false while one runs). KUGE does not stop anything: the room goes on
simulating, and not ticking the game, not reading the inputs, is the game's (`cinematicRunning()` tells).

## A room that starts

A game with a start (a match, a round, a race) is open, then closed. When it starts, the room calls
`setOpen(false)`: it tells the lobby, which from then on gives no place in it. The room stays in the lists with
`RoomInfo::open == false` (a menu can grey it out: "in progress"); a client that asks for it by id and name gets
`JoinError::Started` ("this game has already started"), and the automatic matchmaking (`join()`, a quick play) skips it
and makes another room. The players that are in it are not affected, nor the ones already on their way with a token;
the lobby learns it a moment after the room calls it, so a join that is already in flight can still get in.
`setOpen(true)` opens it again; a room is open when it is made. `isOpen()` says where it is.

## The door of a room

A client that connects to a room must say `Hello{token}` within `helloTimeout`, or it is dropped. The token opens
the door **once** (a stolen or reused token is `Rejected`), and expires. Anything a client sends before
its `Hello` is **never given to your game**: your `on<T>` handlers only see players.

## The end of a game

`finish(reason)` (the game is over), an idle room (`idleTimeout`), or the server stopping: the players are told
(`RoomClosed`), the last messages get a moment to leave (`linger`), and the scene is left. The clients are back
in the lobby and can ask for another game.

`finish()` takes the reason that the players hear (`onRoomClosed`): `RoomEnd::GameOver` (the default) or
`RoomEnd::Victory` when the game was won, so a client can tell a win from a loss without a message of its own. The two
mean the same to KUGE: the room is over, the lobby counts it closed, `closeDelay` applies. `kuge::net::describe(reason)`
gives a short text for each value. (`Idle`, `ServerStopping`, `Kicked` and `Error` are KUGE's.)

When the **server stops** (Ctrl+C, `GameServer::stop()`), the players hear `RoomEnd::ServerStopping`, not
`RoomEnd::Error`: the lobby tells the players that are in a room, and the room tells its own, before their
connections go (a room that was finishing keeps the reason of its game). A client that was only in the lobby has
nothing to be told and sees the lobby lost. Both messages are sent once, with no wait for an ack: over UDP the room's
may be lost, and the client then hears the lobby's.

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
