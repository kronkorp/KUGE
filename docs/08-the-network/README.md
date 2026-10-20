# 08 The network

`kuge-net` sends **typed messages** between programs (or between threads of one program). It is built in
three layers, and you mostly work in the top one:

```
   Net              a scene's endpoints, polled in the Network stage
     │
   Endpoint         handshake, channels, keep-alive, timeouts, typed messages
     │
   ITransport       whole packets: TCP (framed), UDP, or an in-memory loopback
```

It needs only the core: a server links it without SDL.

## Typed messages

A message is a struct that lists its fields:

```cpp
struct PlayerMoved
{
    KUGE_MESSAGE(PlayerMoved, id, position, heading)   // the fields that go on the wire, in this order
    std::uint32_t id = 0;
    kuge::Vec2    position;
    float         heading = 0;
};
```

`KUGE_MESSAGE` gives the struct an **id** (a hash of its name: the same on every machine and every build)
and says how to write and read it. Fields can be numbers, bools, enums, strings, vectors, arrays, optionals,
`Vec2`, `Rect`, and other messages (up to 24 fields). Rules:

- Both sides need the **same name and fields, in the same order**.
- The struct must be a type of a **namespace**, not a local class (a local class cannot have the static members
  the macro adds).
- An empty message is fine: `struct Ping { KUGE_MESSAGE(Ping) };`
- If two different names hash to the same id, registering the second handler throws at once
  (`std::logic_error`): a collision is an error, not a wrong dispatch.

## Endpoints

An `Endpoint` is one end of a network: a **server** that accepts connections, or a **client** that makes one.

```cpp
kuge::net::Endpoint server(transport, kuge::net::Role::Server);

server.onConnected([](kuge::net::ConnectionId id) { ... });
server.onDisconnected([](kuge::net::ConnectionId id, kuge::net::DisconnectReason why) { ... });
server.on<PlayerMoved>([&](kuge::net::ConnectionId from, const PlayerMoved& moved) { ... });

server.send(id, PlayerMoved{...}, kuge::net::Channel::Unreliable);
server.broadcast(Chat{...}, kuge::net::Channel::Reliable, /*except*/ id);
server.disconnect(id);

server.poll();          // once per tick: reads, sends what is due, calls the handlers
```

A client's only connection (to the server) is always `kuge::net::CLIENT_CONNECTION`.

### Two channels

| Channel | Behaviour | For |
|---|---|---|
| `Unreliable` | Sent once. May be lost, arrive twice, out of order. | positions, inputs, snapshots |
| `Reliable` | Arrives **once and in order**, however many times it has to be sent | chat, events, handshakes |

How `Reliable` works: each message is numbered, acknowledged (acks ride on the traffic with a bit mask for what
came out of order), and sent again after a wait that follows the measured round trip and doubles at each miss.
A window (256 in flight) and a queue (1024) bound the memory: `send` returns `false` when full, instead of
growing without end. Over TCP nothing is ever sent twice.

`send` returns `false` if it could not be sent: not connected, too big for a packet (a message is at most
8192 bytes with its header), or too many are waiting. Check it when it matters.

**Over UDP, keep a message under about 1200 bytes** (`DATAGRAM_PAYLOAD`). 8192 is what a transport carries (and TCP
and the loopback carry it all), but a UDP datagram over what a path carries in one piece (about 1500 bytes on
Ethernet, 1280 at the least) is cut into fragments by IP, and losing one fragment loses the whole datagram: with 5
fragments and 2 % of loss, about 10 % of them are lost, and a reliable message is sent again and again. KUGE does not
cut a message itself (no fragmentation, and no ack per fragment): a big message is the game's to split into several,
or to make smaller. The endpoint counts the packets it sent over `DATAGRAM_PAYLOAD` on a transport that can lose them
(`stats().bigPackets`), so a game can see that it does it. Snapshots are cut by the replication, in parts of 1100 bytes
(see [10](../10-replication-and-prediction/README.md)).

### Handshake, keep-alive, timeouts

- A client sends `Connect` until the server answers `Accept`. So **"connected" means the other side answered**,
  even over UDP, where a socket knows nothing about who is at the other end. A server that is full, or that has
  another `protocol` version, refuses (`DisconnectReason::Refused`).
- **The server checks the address before it keeps anything.** Over UDP the first `Connect` is answered with a small
  `Challenge` (a cookie that only this server can make, for this address and this client); the client repeats its
  `Connect` with it, and only then is the connection made. A `Connect` from a forged address, which never receives
  the `Challenge`, leaves no connection, takes none of the `maxConnections` places (the peers that have not answered are
  at most `maxHalfOpen`, the oldest dropped first) and is answered by less than it sent. The connection costs one more
  round trip. TCP has proved the address already and skips this. `Connect` packets are counted by address
  (`connectsPerSecond`, 64 by default, per IP whatever the port: players behind one NAT share it) and the ones
  over are dropped. See `stats().challenges` and `stats().rateLimited`.
- **Every packet after the handshake ends with a 4-byte tag** (SipHash-2-4 with a key made of the two sides' secrets
  of the handshake): a packet with a wrong tag is dropped and counted in `stats().malformed`. **What this protects:**
  a connection against someone who is not on the path, who knows the addresses and guesses the sequence numbers, and
  tries to slip a message or a `Goodbye` in. **What it does not:** nothing is encrypted (a message can be read), and
  someone who reads the handshake has the key, so he can forge packets and replay old ones. A real encryption and the
  login of players are for later, and need a library. A game with accounts must not count on this alone.
- A silent connection sends something every second, and a peer that says nothing for 10 seconds is gone
  (`Timeout`). Leaving (`disconnect`, or destroying the endpoint) tells the peer at once, so it need not wait.
- All the delays are in `EndpointConfig`, with a clock you can replace (tests do).

### Handlers

They run at the **end of `poll()`**, when the endpoint is in order, so a handler may send, broadcast, disconnect,
and add or remove endpoints. Bad data (junk packets, cut messages, unknown types) is dropped and counted in
`endpoint.stats()`, never fatal.

## Transports

| Transport | Made with | Notes |
|---|---|---|
| TCP | `makeTcpServer(port)`, `makeTcpClient(host, port)` | The stream is cut into whole packets (4-byte length, then the packet), whatever way TCP cuts or glues the bytes. A peer that announces a packet larger than 8192 is dropped. |
| UDP | `makeUdpServer(port)`, `makeUdpClient(host, port)` | A packet is a datagram. |
| Loopback | `LoopbackNetwork::listen(name)`, `connect(name)` | In memory, by name, **thread-safe**. |

TCP and UDP go through kronknet, over IPv4 or IPv6 ("localhost", "127.0.0.1", "::1": an address, not a host name). A server listens on both. A server that cannot bind throws;
a client that cannot reach its server does not throw: its endpoint reports a disconnection.

**The loopback is the most useful of the three.** It lets a client and a server in the same process, on
different threads, talk with no socket: a player who hosts the match. And it can misbehave on purpose:

```cpp
kuge::net::LoopbackNetwork network(kuge::net::LoopbackNetwork::Conditions{
    .loss = 0.3, .duplicate = 0.1, .latency = 0.05, .jitter = 0.03, .seed = 7});
```

It loses, repeats, delays and reorders packets (the same seed loses the same ones), drops the ones over a size
(`.mtu = 1200`: what a path that cannot carry more does, counted in `packetsTooBig()`), and its clock can be replaced,
so a test can make ten seconds pass in no time. The reliable channel is tested over 50 % loss with it.

## In a scene: `Net`

```cpp
kuge::net::installNet(setup());                                    // a Net resource, polled in the Network stage
auto& net = world().getResource<kuge::net::Net>();

auto& server = net.listen(kuge::net::Protocol::Udp, 4242);         // or net.listen("room", loopbackNetwork)
auto& client = net.connect(kuge::net::Protocol::Tcp, "127.0.0.1", 4242);
```

A scene's endpoints are polled by its own thread and destroyed with the scene (their peers are told). An
endpoint belongs to the thread that polls it.

### A connection that lives longer than a scene

The `Net` made by `installNet(setup())` dies with its scene. A connection to a lobby must survive the menu, the game and
the way back: make the `Net` outside, and give it to each scene.

```cpp
auto net = std::make_shared<kuge::net::Net>();          // in Main (or in the Session the scenes share)
engine.scenes().change<MainMenu>(net);

void MainMenu::onEnter()
{
    kuge::net::installNet(setup(), m_net);               // polls it; no Net of its own
    auto& net = *world().getResource<kuge::Ref<kuge::net::Net>>();    // (this form works for both installNet())
}
```

The system that polls it keeps a `shared_ptr`, so the `Net` is released after the scenes whatever the order. It is polled
once per tick (`Net::pollTick(Time::tick)`) even when two scenes poll it (one over the other, see 03). A
`MatchmakingClient(net)` is made over it the same way, and lives as long as the `Net` does.

## Joining a game: matchmaking

Getting a player into a room has its own small protocol, with its client side in this module (so a client does
not link the rooms):

```cpp
kuge::net::MatchmakingClient matchmaking(net);

matchmaking.connectLobby(kuge::net::Protocol::Tcp, "example.org", 4242);
matchmaking.onJoined([](kuge::net::Endpoint& room, const kuge::net::Welcome& welcome) {
    room.on<GameState>(...);                 // the game talks to the room through this endpoint
});
matchmaking.onRoomClosed([](kuge::net::RoomEnd why) { ... });   // the room ended: drop what holds its endpoint
matchmaking.onFailed([](const std::string& why) { ... });
matchmaking.join("deathmatch", "Ana");       // may be called at once: it waits for the lobby
```

**Why it failed.** `matchmaking.error()` is a text for the player, and `matchmaking.lastReason()` is the same
thing for the code: the `DisconnectReason` of the lobby (or room) that was lost. `Refused` means another version
of the game or a server that is full, and retrying in a loop is pointless; `Timeout` is a server that does not
answer ("the server does not answer"); `Unreachable` is nobody at that address ("the server cannot be reached").
`Local` means nothing was lost yet. `onFailed` gets the text, with no change of signature.

The server side is in [09 The server](../09-the-server/README.md).

## Things to remember

- **Never trust the network.** Handlers receive already-validated structs (a bad message never reaches them),
  but *values* can still be lies (a position of 1e30, a health of -5): check them in the game.
- **A message is a copy.** Do not send pointers; send ids.
- **Unreliable is the default for continuous data.** A lost position does not matter: the next one replaces it.
  A lost "player joined" does: use `Reliable` for events.
- **Drain sockets.** The socket transports read until the socket is empty in each poll (a peer that sends more
  than it is polled would otherwise be heard later and later).

Next: [09 The server](../09-the-server/README.md).
