# Step 6: The client

Files: `client/RTypeClient.hpp` (the scene), `client/Script.hpp`, `client/main.cpp`.

The client is **a window on the game**. It does four jobs: connect and join a room, read the keys and send them,
build something to look at for each entity the server mentions, and draw.

## The keys

```cpp
enum class Action : std::uint8_t { Left, Right, Up, Down, Fire };

inline void bindDefaults(kuge::InputMap& input)
{
    input.declare(Action::Left, "left");
    ...
    input.bind(Action::Left, kuge::Key::Left);
    input.bind(Action::Left, kuge::Key::A);
    ...
    input.bind(Action::Fire, kuge::Key::Space);
}
```

The game only reads *actions* (see [05](../../05-the-client/README.md)), so the player can rebind them with a
keybinds file, and a test can press a key without a window.

## The scene and its options

```cpp
struct ClientOptions
{
    bool                        sockets  = true;                 // false: a server of the same process, by name
    std::string                 host     = "127.0.0.1";
    std::uint16_t               port     = 4242;
    std::string                 lobby    = "lobby";              // loopback: the name of its lobby
    kuge::net::LoopbackNetwork* network  = nullptr;              // loopback: null is the process-wide one
    std::string                 name     = "pilot";
    bool                        rejoin   = true;                 // ask for another game when one ends
};

class RTypeScene : public kuge::ClientScene
{
    public:
        RTypeScene(ClientOptions options, std::shared_ptr<ClientReport> report);
        void onEnter() override;
        ...
};
```

`ClientOptions` is the **only place** that knows how the server is reached. That is what lets the same scene be
the client of a dedicated server (`sockets = true`) and of a hosted one (`sockets = false`). Nothing else changes.

`ClientReport` is a struct the scene fills in (are we connected, how many ships do we see, how many corrections):
it is what a script or a test reads afterwards to know what happened, since a window cannot be asserted on.

## `onEnter`

```cpp
void RTypeScene::onEnter()
{
    installClientSystems();                          // sprites, input sampling
    kuge::net::installNet(setup());                  // a Net resource, polled each tick
    makeStars();                                     // decoration
    m_matchmaking = std::make_unique<kuge::net::MatchmakingClient>(world().getResource<kuge::net::Net>());
    m_matchmaking->onJoined([this](Endpoint& room, const Welcome& welcome) { joined(room, welcome); });
    m_matchmaking->onRoomClosed([this](RoomEnd) {
        ++m_report->gamesEnded;
        m_prediction.reset();                        // they hold the room's endpoint, which is gone
        m_replication.reset();
        if (m_options.rejoin) { m_matchmaking->join("rtype", m_options.name); }
    });
    connect();                                       // to the lobby, and ask for a game
    addSystem(Fixed, stage::Simulation, ... tick(w) ...);
    addSystem(Frame, stage::Late,       ... frame(w) ...);
}
```

**Joining is `MatchmakingClient`'s job** (see [08](../../08-the-network/README.md)). You give it the lobby's address
and the kind of room you want; it does the lobby, the token and the `Hello`, and calls `onJoined` with the
room's endpoint once welcomed.

Note `onRoomClosed`: it is called whenever the room is gone. Everything that holds the room's `Endpoint`
(the replication client, the prediction) **must be dropped there**, or it would use a destroyed object.

**Reconnecting.** If the server is not there yet (clients started first), `MatchmakingClient` says so through
its state, and `tick()` tries again every second. A client that finds nobody and never retries is a bug that a
player meets on the first day.

## Replication: what the server tells us

```cpp
void RTypeScene::joined(Endpoint& room, const Welcome& welcome)
{
    m_networkId = welcome.networkId;
    m_replication = std::make_unique<ReplicationClient>(world(), m_registry,
                        ReplicationClientConfig{.tickRate = welcome.tickRate});
    m_replication->setLocalPlayer(welcome.networkId);
    m_replication->predictType(SHIP);                      // our own ship is predicted
    for (const auto type : {SHIP, BULLET, ENEMY}) {
        m_replication->onSpawn(type, [this](World&, Entity e, const SpawnInfo& info) { spawned(e, info); });
    }
    m_replication->attach(room);
    ... prediction (step 7) ...
}
```

`Welcome` tells the client its `networkId` in this room and the server's tick rate. From then on, snapshots
arrive on `room`, and `ReplicationClient` makes entities appear in the client's World.

## What things look like: the prefab

The server sent positions, health and colour slots. It never said *how a ship looks*. That is decided here, in
the function called for each new entity:

```cpp
void RTypeScene::spawned(kw::Entity entity, const SpawnInfo& info)
{
    kuge::Sprite sprite;

    switch (info.type) {
        case SHIP:
            sprite.size = SHIP_SIZE;
            sprite.tint = info.owner == m_networkId ? kuge::Color{255, 255, 255, 255}     // ours: white
                                                    : slotColor(world().get<Slot>(entity).color);
            sprite.layer = 3;
            break;
        case BULLET:
            sprite.size = BULLET_SIZE;
            sprite.tint = kuge::Color{255, 240, 120, 255};                                // yellow
            sprite.layer = 2;
            break;
        default:                                                                          // an enemy: red
            sprite.size = ENEMY_SIZE;
            sprite.tint = kuge::Color{230, 70, 70, 255};
            sprite.layer = 1;
            break;
    }
    world().add<kuge::Sprite>(entity, sprite);
}
```

**A sprite with no texture is a coloured rectangle**, so the game has no image files. To use real art you would
load textures and set `sprite.texture` and `sprite.source` here, and nothing else in the game changes. That is
the benefit of the split: the server never has to know what art you use.

When the components `Transform2D`, `Health` and `Slot` arrive with the entity, the `SpriteRender` system that
`installClientSystems()` added draws it. You did not write any drawing code.

## Each tick and each frame

```cpp
void RTypeScene::tick(kw::World& world)              // Fixed, Simulation
{
    const auto& actions = world.getResource<kuge::ActionState>();
    Steer steer;

    steer.dx = (actions.isDown(Action::Right) ? 1 : 0) - (actions.isDown(Action::Left) ? 1 : 0);
    steer.dy = (actions.isDown(Action::Down) ? 1 : 0) - (actions.isDown(Action::Up) ? 1 : 0);
    steer.fire = actions.isDown(Action::Fire);
    if (m_prediction) { m_prediction->tick(steer); }   // simulate it now, and send it to the server
    ... retry the lobby if it was lost ...
}
```

The keys become one `Steer` per tick, exactly the message from step 3.

```cpp
void RTypeScene::frame(kw::World& world)             // Frame, Late
{
    camera.position = {ARENA_W / 2, ARENA_H / 2};    // the whole arena on the screen, whatever its size
    camera.zoom = std::min(screen.x / ARENA_W, screen.y / ARENA_H);
    if (m_replication) { m_replication->update(frameDt); }     // place the interpolated entities
    ... scroll the stars, tint ships by health, fill the report ...
}
```

Two different clocks, again: **input and prediction run in the fixed tick** (60 per second, the server's rate);
**interpolation and drawing run per frame** (as fast as the screen).

## The client program

`client/main.cpp`:

```cpp
kuge::Engine engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed, .tickRate = 60});
auto& client = engine.addModule<kuge::ClientModule>(kuge::makeSdlBackend(window), kuge::ClientConfig{...});
auto report = std::make_shared<rtype::ClientReport>();

rtype::bindDefaults(client.input());
engine.scenes().change<rtype::RTypeScene>(options, report);
return engine.run();
```

That is it. With `--frames N` the same file instead runs a scripted pilot (`Script.hpp`) for N frames and keeps a
picture of the last one: how the smoke test in step 9 uses it on a machine with no screen.

Run a server and this client and you have a multiplayer game. It will *work* but feel sluggish, since your own
ship waits for the server. That is the next step.

Next: [Step 7: Making it feel right](../07-making-it-feel-right/README.md).
