# Step 10: Where to go next

You have a networked game. Here are ideas, each with the place where it plugs in. They are ordered from the
easiest to the most work, and none of them changes the engine.

## Easy: change the game

| Idea | Where |
|---|---|
| Faster or tougher enemies, a bigger wave | `Settings` in `common/RType.hpp` |
| A different enemy movement (a dive, a zig-zag) | the "enemies cross the screen" part of `stepRules` |
| Different colours and sizes | `RTypeScene::spawned` and the constants in `common/RType.hpp`. The server does not care. |
| Two kinds of bullet | a new `EntityType`, a field in `Bullet`, a branch in `fire` and in the prefab |
| Power-ups | a new entity type with a trigger `Collider`; ships pick them up in `stepRules` (the platformer's coins are the model) |
| Lives and respawn | instead of removing a dead ship, reset its `Health` and position after a delay: a component with a countdown |

## Medium: use more of the engine

- **Real art.** Load textures with `client.textures().load(...)`, cut a `Spritesheet`, add an `Animator`
  in the prefab. The room does not change. With `watchAssets` you can even edit a picture while the game runs.
- **Sound.** Play a shot with `Ref<Audio>` when a bullet spawns on the client (in `onSpawn` for `BULLET`), and an
  explosion in `onDestroy` for `ENEMY`. The server never knows sound exists.
- **A HUD.** Score and health with `UiLabel` and a font, from the replicated `Score` and `Health`.
- **A menu and a name.** A `MenuScene` with "Play" and a text field, pushed before joining (the platformer's menu is
  the model); pass the name in `ClientOptions`.
- **A scrolling level.** Make the arena wider than the screen, move the `Camera2D` with the ship, and generate
  enemies from a `TileMap` or a script instead of a random number.
- **Settings and keybinds.** `input.loadBindings("keybinds.cfg")` and a small options screen with
  `startCapture()`.
- **Saving a high score.** `SaveSlots` with the best score, written when a game ends (`onRoomClosed`).
- **Different room types.** Register a second type (`server.addRoomType<Survival>("survival", ...)`) and let the
  client ask for it in `join("survival", name)`.

## Bigger: server features

- **Interest management.** With larger worlds, do not send everything to everyone:
  `replication.setFilter([](ConnectionId c, NetworkId id, const EntityRecord& r) { ... })` returns whether a client
  should see an entity (only near it, only the same team).
- **Bots.** A bot is a system in the room that produces `Steer`s for a ship with no client. Since the rules take
  a `Steer`, a bot needs no network at all.
- **Spectators.** A client that is a player without a ship: in `onPlayerJoined`, do not build one, but do
  `addClient` so it receives snapshots.
- **A ranked lobby.** `RoomTypeConfig` per skill level, and a small `LobbyScene` variant; the matchmaking protocol
  (`JoinRoom`) is where a rating would travel.
- **A dashboard.** `server.stats()` is readable from any thread: a status thread can print rooms, players and
  joins every second.

## Bigger: engine-level things you could add

The engine is finished for what is described here, but these are natural next steps if you need them:

- **A CMake package** (`find_package(kuge)` and a `kuge_add_game()` function) so a game can live outside the
  repository.
- **In-game debug tools**: an overlay with the tick time, snapshots per second, corrections, and the round trip
  (`endpoint.rtt()` and the report fields are already there).
- **Client-side prediction of shots**, if instant muzzle flashes matter: predict a *cosmetic* bullet and let the
  server own the real one.
- **3D**: the client module draws in 2D; a 3D module would add a renderer next to it, and the server would not care.

## The general recipe

Whatever you add, ask the same four questions, in this order:

1. **Who decides?** (Almost always the room.) Put the rule in `common/` as a plain function and test it alone.
2. **Who needs to know?** Does the client need it to draw or to predict? Then add a replicated component with the
   right mode. Otherwise keep it server-only.
3. **What does the client do with it?** Decide in the prefab or a client system, never in the room.
4. **How do I test it?** Rules alone first, then a `Pilot`, then several rooms, then the real programs.

Go back to the [documentation index](../../README.md), or try the [cookbook](../../13-cookbook/README.md) for
short recipes.
