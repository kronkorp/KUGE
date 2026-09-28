# 15 Glossary

| Word | Meaning |
|---|---|
| **Action** | What a game wants to know about the player (`Fire`), as opposed to which key was pressed. Keys are bound to actions. |
| **Authoritative server** | The server decides what happens; clients send intentions (inputs) and receive results. |
| **Body** | A physics component that makes an entity move (`Kinematic`, `Dynamic`). |
| **Broadcast** | Send a message to every connected peer (optionally but one). |
| **Channel** | How a message is sent: `Unreliable` (once, may be lost) or `Reliable` (once and in order, resent if needed). |
| **Client** | The program a player runs: it has a window, reads the keys, and connects to a server. |
| **Collider** | A shape (box or circle) that can be touched. A collider with no body is a wall. |
| **Component** | A plain struct attached to an entity. |
| **Connection id** | Which peer of an endpoint a message is about. A client's only peer is `CLIENT_CONNECTION`. |
| **Dedicated** | A scene that runs on a thread of its own; also a server that runs alone, with no player on it. |
| **Determinism** | The same inputs give the same result, bit for bit. Prediction, tests and replays need it. |
| **Endpoint** | One end of a network connection: a server that accepts, or a client that connects. |
| **Engine** | The loop, the scene stack and the modules. |
| **Entity** | A number that components are attached to. |
| **Fixed tick** | One step of the simulation, always the same duration (`dt`). |
| **Frame** | One turn of the loop that draws. There can be more frames than ticks. |
| **Handle (scene)** | A copyable reference to a scene that can only send it messages. |
| **Host** | A player who runs the room in the same process as their window. |
| **Interpolation** | Drawing something between two known values, a little in the past, so movement is smooth. |
| **Jitter** | Variation in the time packets take. Also the small input buffer that absorbs it (`InputServerConfig::jitter`). |
| **Lobby** | The scene where clients arrive, and that gives them a room. |
| **Loopback** | An in-memory network inside one process; can lose, delay and reorder packets for tests. |
| **Message (scene)** | A value sent from one scene to another in the same process, read in `onMessage`. |
| **Message (network)** | A struct with `KUGE_MESSAGE` sent over an endpoint. |
| **Module** | A part of the engine added to an engine: the client, for example. Also a CMake library. |
| **NetworkId** | The number that names a replicated entity on the server and on every client. (A player has a different `networkId` in a room, see `Player`.) |
| **Owner** | The player an entity belongs to (a network id of the room). |
| **Pooled** | A scene run on the worker threads, one tick at a time, when it is due. |
| **Predicted (component)** | A component the client simulates itself for its own entity. |
| **Prediction** | Simulating your own player at once instead of waiting for the server. |
| **Prefab** | The function the client calls when an entity appears, to add what the server does not send (sprite, sound). |
| **Reconciliation** | Putting the prediction back in agreement with the server's state, and simulating again the inputs it has not seen. |
| **Replication** | Showing the server's world to the clients. |
| **Resource** | A value that exists once per World (a clock, a camera, a service). |
| **Room** | One game on a server: a scene with its own World, listening at its own address. |
| **Scene** | A part of a game (menu, level, room), with its own World. Scenes share nothing. |
| **Schedule** | `Fixed` (each tick) or `Frame` (each loop). |
| **Snapshot** | The state of the replicated world at a tick, sent as the changes since one the client acknowledged. |
| **Snap** | A correction too large to smooth: the player is put at the new place at once. |
| **Stage** | Where in a tick a system runs: Network, Input, Simulation, Physics, Late, Replication, Render. |
| **System** | Code called by the engine each tick or frame. |
| **Token** | A random one-time key the lobby gives a client to enter a room. |
| **Transport** | What carries whole packets: TCP, UDP or loopback. |
| **Trigger** | A collider that stops nothing and only reports who enters and leaves. |
| **World** | The ECS: entities, components, resources and systems of one scene. |
