# Step 1: The plan

## The game

- An arena of **640 x 360** pixels, seen from the side.
- Up to **4 ships**, one per player, that move in four directions and fire bullets to the right.
- **Enemies** come from the right edge and wave across the screen. A bullet damages them; an enemy that touches
  a ship damages the ship.
- A game is a **wave** of 30 enemies. It ends when all ships are dead (**lost**) or the whole wave has been
  killed or has left the screen (**won**). Then a new game starts.
- No art files: everything is drawn as coloured rectangles.

## Who knows what

This is the most important decision, so make it explicit.

| | The room (server) | The client |
|---|---|---|
| The rules: bullets, enemies, hits, score, when the game ends | **decides** | does not know them |
| Where things are | **knows the truth** | has a copy, a little late |
| Your own ship's movement | knows the truth | **predicts** it, so it answers at once |
| How things look (colours, sizes) | does not know | **decides** |
| The keys | does not know | reads them, sends `Steer` |

And what they **share**: the vocabulary (what a `Steer` is, which components exist, how they go on the wire), the
arena and how a ship moves (so the client can predict), and the rules code (which only the room *calls*, but which
is plain functions you can test alone).

## The three programs

```
                          ┌──────────────────────┐
   rtype_server ─────────▶│ room: rules + inputs │─── snapshots ───▶ clients
   (no window)            └──────────────────────┘◀─── inputs ──────
   rtype_client ─────────▶  window, keys, drawing, prediction
   rtype_host   ─────────▶  both, in one process, over an in-memory network
```

## The folders

```
example/rtype/
  CMakeLists.txt
  common/    RType.hpp, Rules.cpp     shared: vocabulary, arena, rules
  server/    RTypeRoom.{hpp,cpp}, main.cpp     the room and the server program
  client/    RTypeClient.hpp, Script.hpp, main.cpp     the scene and the client program
  host/      main.cpp                 the host program
tests/rtype/rtype_test.cpp            the tests
cmake/RunRType.sh                     runs the real programs
```

The reason for `common/` is simple: **`server/` must never include anything from the client, and `client/` must
never include the room**. Everything both need goes in `common/`. The build enforces the first half (the server
links no client library, so a client header will not even be found).

## The engine pieces you will use

| Piece | Where you meet it |
|---|---|
| Scenes and systems | the room and the client scene |
| `kuge-physics` | the arena (walls) and how a ship moves |
| `kuge-net` | messages (`Steer`) and the client side of joining |
| `kuge-server` | `RoomScene` and `GameServer` |
| `kuge-replication` | what the room shows and what the client predicts |
| `kuge-client` | the window, the keys, the sprites |

Next: [Step 2: A project](../02-a-project/README.md).
