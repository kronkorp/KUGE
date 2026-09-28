# KUGE documentation

KUGE is a C++20 game engine made of independent modules: a game links the ones it needs and nothing
else. The same code can be a solo game, a dedicated server with no window, and a game where one
player hosts the match.

These pages explain **how the engine works**, module by module, and then build a whole game with it:
**a small R-Type**, from an empty folder to a server, a client and a host, with tests.

Read them in order the first time. Each page stands alone afterwards.

## The engine

| Page | What you learn |
|---|---|
| [01 Getting started](01-getting-started/README.md) | Build it, run the examples, see how the repository is laid out |
| [02 How the engine works](02-how-the-engine-works/README.md) | The loop, the fixed tick, schedules and stages, time, modules: the big picture |
| [03 Scenes, systems and the ECS](03-scenes-systems-and-the-ecs/README.md) | The three ideas you write games with |
| [04 Threads and messages](04-threads-and-messages/README.md) | Scenes on several threads, how they talk, the worker pool, background loading |
| [05 The client](05-the-client/README.md) | Window, inputs and keybinds, sprites, the camera, assets, sound, text, UI, tilemaps, animations |
| [06 Physics](06-physics/README.md) | Colliders, bodies, triggers, queries, determinism |
| [07 Data, saves and config](07-data-saves-and-config/README.md) | The serializer, config files, save slots, snapshots of a world |
| [08 The network](08-the-network/README.md) | Endpoints, typed messages, reliable channels, TCP, UDP and the in-memory loopback |
| [09 The server](09-the-server/README.md) | A lobby, rooms, tokens, and how a player is matched to a game |
| [10 Replication and prediction](10-replication-and-prediction/README.md) | Showing a world to clients, interpolation, and making your own player answer at once |

## Make R-Type, from A to Z

| Step | |
|---|---|
| [The tutorial](11-make-rtype/README.md) | What we build, and the map of the ten steps |
| [1 The plan](11-make-rtype/01-the-plan/README.md) | The three programs, one game, the folders |
| [2 A project](11-make-rtype/02-a-project/README.md) | CMake: the libraries and the programs |
| [3 The shared vocabulary](11-make-rtype/03-the-shared-vocabulary/README.md) | Messages, components, what is replicated |
| [4 The arena and the rules](11-make-rtype/04-the-arena-and-the-rules/README.md) | The game itself, with no network and no screen |
| [5 The room](11-make-rtype/05-the-room/README.md) | The server side of a game |
| [6 The client](11-make-rtype/06-the-client/README.md) | A window on the game |
| [7 Making it feel right](11-make-rtype/07-making-it-feel-right/README.md) | Prediction, interpolation, jitter, the clocks |
| [8 Three ways to run it](11-make-rtype/08-three-ways-to-run-it/README.md) | Dedicated server, client, host |
| [9 Testing a networked game](11-make-rtype/09-testing-a-networked-game/README.md) | Rules, a host, a server, several rooms, real programs |
| [10 Where to go next](11-make-rtype/10-where-to-go-next/README.md) | Ideas, and how each one fits |

## Around the code

| Page | |
|---|---|
| [12 Testing and debugging](12-testing-and-debugging/README.md) | The tests, sanitizers, simulating a bad network |
| [13 Cookbook](13-cookbook/README.md) | Short recipes for things you will want to do |
| [14 Pitfalls](14-pitfalls/README.md) | The traps that were met while building the engine |
| [15 Glossary](15-glossary/README.md) | Words used everywhere |

The [README of the repository](../README.md) is the reference; these pages are the explanation.
