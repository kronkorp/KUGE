# Make R-Type, from A to Z

In these ten steps you build a small **R-Type**: up to four ships fly in an arena, shoot waves of enemies, and
the game runs on a **server** with **clients** connected to it. You will end with three programs made from one
set of code:

| Program | What it is |
|---|---|
| `rtype_server` | A dedicated server: a lobby and a room for each game. No window. |
| `rtype_client` | A window that connects to a server. |
| `rtype_host` | A player who hosts: the room and the window in one process. |

The finished game is in `example/rtype/`. The code blocks in this tutorial are taken from it (shortened where you see `...`), so you can read
the real file next to each step.

## What you need to know first

C++ (templates and lambdas are used), and the ideas in the engine pages, especially:

- [02 How the engine works](../02-how-the-engine-works/README.md): the fixed tick and the stages
- [03 Scenes, systems and the ECS](../03-scenes-systems-and-the-ecs/README.md)

The network pages ([08](../08-the-network/README.md), [09](../09-the-server/README.md),
[10](../10-replication-and-prediction/README.md)) explain what steps 5 to 7 use; you can read them first or when
you get there.

## The steps

| | Step | You will |
|---|---|---|
| 1 | [The plan](01-the-plan/README.md) | Decide what runs where, and lay out the folders |
| 2 | [A project](02-a-project/README.md) | Write the CMake for the libraries and the programs |
| 3 | [The shared vocabulary](03-the-shared-vocabulary/README.md) | Define messages, components, entity types, and say what is replicated |
| 4 | [The arena and the rules](04-the-arena-and-the-rules/README.md) | Write the game itself, with no network and no screen, and test it |
| 5 | [The room](05-the-room/README.md) | Write the server side: inputs, rules, snapshots |
| 6 | [The client](06-the-client/README.md) | Draw the game and read the keys |
| 7 | [Making it feel right](07-making-it-feel-right/README.md) | Predict your own ship, smooth the others, tune the clocks |
| 8 | [Three ways to run it](08-three-ways-to-run-it/README.md) | Dedicated server, client, host |
| 9 | [Testing a networked game](09-testing-a-networked-game/README.md) | Test the rules, a host, a server, several rooms, the real programs |
| 10 | [Where to go next](10-where-to-go-next/README.md) | Ideas, and where each one plugs in |

## The one principle

> **Write the game once, as if there were no network. Then decide where each part runs.**

The rules (steps 3 and 4) know nothing about sockets or windows. The room (step 5) runs them and tells the clients
what happened. The client (step 6) draws what it is told. Because nothing in the rules knows *where* it runs, the
same code is a server, a client's prediction, and a host: that is what the three programs prove.
