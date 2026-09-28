# Step 5: The room

Files: `server/RTypeRoom.hpp`, `server/RTypeRoom.cpp`, `server/main.cpp`.

A **room** is one game running on the server. It is a scene (see [03](../../03-scenes-systems-and-the-ecs/README.md)):
it has a World, systems, and its own thread. You derive from `kuge::server::RoomScene`, which already handles the
network side: listening, tokens, `Hello`, `Welcome`, network ids, players coming and going, and ending.

## The class

```cpp
class RTypeRoom : public kuge::server::RoomScene
{
    public:
        using RoomScene::RoomScene;                 // built with a RoomInit, given by the lobby

    protected:
        void onRoomEnter() override;
        void onPlayerJoined(const Player& player) override;
        void onPlayerLeft(const Player& player, kuge::net::DisconnectReason reason) override;

    private:
        void applyInputs(kw::World& world);
        void afterPhysics(kw::World& world);

        kuge::replication::ReplicationRegistry                m_registry;
        std::unique_ptr<kuge::replication::ReplicationServer> m_replication;
        std::unique_ptr<kuge::replication::InputServer<Steer>> m_inputs;
        bool                                                  m_anyShip = false;
};
```

Nothing in it mentions a window, a texture or a key.

## Building the world: `onRoomEnter`

```cpp
void RTypeRoom::onRoomEnter()
{
    m_registry = makeRegistry();                      // the same list as the clients
    buildArena(world());
    world().addResource<Match>();
    world().getResource<Match>().rng.seed(init().roomId);     // the game of a room is reproducible
    m_replication = std::make_unique<ReplicationServer>(world(), m_registry, endpoint());
    m_inputs = std::make_unique<InputServer<Steer>>(endpoint(), InputServerConfig{.jitter = 2});
    world().getResource<Match>().track = [this](kw::Entity e, EntityType type, std::uint32_t owner) {
        m_replication->track(e, type, owner);         // the rules make bullets and enemies: replicate them
    };
    ... four systems (below) ...
}
```

`onRoomEnter` runs when the room's scene is entered: the endpoint is already listening and the room is not yet
announced to the lobby, so this is the place to create everything. `endpoint()` is the room's network endpoint.

`init()` gives the `RoomInit`: `roomId`, the type name, the settings. Seeding the random generator with the room id
means each room plays its own game, and the same room number plays the same game (useful when you chase a bug).

## Four systems, four stages

```cpp
addSystem(Fixed, stage::Simulation,  [this](World& w) { applyInputs(w); });
addSystem(Fixed, stage::Physics,     [](World& w)     { stepArena(w, TICK_DT); });
addSystem(Fixed, stage::Late,        [this](World& w) { afterPhysics(w); });
addSystem(Fixed, stage::Replication, [this](World& w) { m_replication->update(tick + 1); });
```

(In the file they are wrapped in a small `Simulate` system that calls a function, as in
[03](../../03-scenes-systems-and-the-ecs/README.md).) Read them as one tick of the room, following the stages:

1. **`Simulation`: apply the inputs.** Each player's `Steer` becomes a velocity.
2. **`Physics`: move.** `stepArena`, the very function the client predicts with.
3. **`Late`: rules.** Bullets, enemies, hits, deaths, and the end of the game.
4. **`Replication`: tell the clients.** Snapshots of what the tick produced.

The **order matters**: replication is last so a snapshot always describes a finished tick.

(`RoomScene` also polls the network in the `Network` stage, before all of these: messages arrive first.)

## Inputs

```cpp
void RTypeRoom::applyInputs(kw::World& world)
{
    for (const auto& applied : m_inputs->collect()) {         // one input per player, in order
        const Player* who = player(applied.connection);
        const auto ship = who ? shipOf(world, who->networkId) : std::nullopt;

        if (!ship) {
            continue;                                          // a player whose ship is dead
        }
        steerShip(world, *ship, applied.input);
        if (applied.input.fire && !applied.repeated) {
            fire(world, *ship);
        }
        m_replication->setInputAck(applied.connection, applied.sequence);
    }
}
```

`collect()` (see [10](../../10-replication-and-prediction/README.md)) gives exactly one input per player per tick,
in order, and repeats the last one if the next has not arrived. Three details:

- **The ship is found by its owner** (`shipOf`, in the rules), at each tick. The room does not keep a map from
  player to entity: when a ship dies, the World gives its number to the next entity it makes, and a kept number
  would then name somebody else's ship. A player who joins later could get it, and the dead player's inputs would
  steer it.

- **`!applied.repeated`** on firing: if the input is only a *repeat* because the real one is late, do not fire
  again. Holding the key would otherwise shoot extra bullets when the network hiccups. (Movement is fine to repeat.)
- **`setInputAck`** tells the replication which input the state now includes. It goes back to the client inside the
  snapshots, and the client's prediction depends on it (step 7).

## Players coming and going

```cpp
void RTypeRoom::onPlayerJoined(const Player& player)
{
    const kw::Entity ship = buildShip(world(), {40.0f, 60.0f + 70.0f * float((player.networkId - 1) % 4)});

    world().add<Health>(ship, Health{settings().shipHealth});
    world().add<Score>(ship, Score{});
    world().add<Slot>(ship, Slot{std::uint8_t((player.networkId - 1) % 4)});
    world().add<Gun>(ship, Gun{});
    world().add<Owned>(ship, Owned{player.networkId});
    m_replication->track(ship, SHIP, player.networkId);        // "this is a SHIP, and it belongs to this player"
    m_replication->addClient(player.connection);               // start sending this player snapshots
    m_inputs->addClient(player.connection);                    // and listening to its inputs
    m_anyShip = true;
}
```

The `Player` (given by `RoomScene`) has a `networkId` (1, 2, 3, 4 in this room, never reused), a `name` and a
`connection`. The ship is built with the shared `buildShip`, then the server-only components are added. **`track`**
is what makes the entity exist on the clients.

The colour slot comes from the network id, so the four ships get four colours.

`onPlayerLeft` removes the player's ship if it is still alive (found with `shipOf`, like the inputs), which the
clients see at the next snapshot, and stops the replication and input for that connection.

## The end of a game

```cpp
void RTypeRoom::afterPhysics(kw::World& world)
{
    stepRules(world, TICK_DT);
    if (!finishing() && !players().empty() && outcome(world, m_anyShip) != Outcome::Playing) {
        finish(kuge::net::RoomEnd::GameOver);
    }
}
```

`finish()` tells the players (`RoomClosed`), gives the last messages a moment to leave, then leaves the scene.
The lobby gets the room's slot back, and the clients are in the lobby again, ready to ask for a new game.

## The server program

`server/main.cpp` is the whole program:

```cpp
int main(int argc, char** argv)
{
    kuge::server::ServerConfig config;

    if (argc > 1) {
        config.lobbyPort = std::uint16_t(std::atoi(argv[1]));
        config.roomPortFirst = std::uint16_t(config.lobbyPort + 1);
    }
    kuge::server::GameServer server(config);

    server.addRoomType<rtype::RTypeRoom>("rtype", {.maxPlayers = 4, .idleTimeout = 30.0});
    return server.run();
}
```

Run `kuge_rtype_server 4242`: it listens for clients on port 4242 (TCP), and each game gets its own UDP port after
that. Ctrl+C closes every room properly.

Notice what is **not** here: no lobby code, no token, no matchmaking, no thread creation. The engine does them.

Next: [Step 6: The client](../06-the-client/README.md).
