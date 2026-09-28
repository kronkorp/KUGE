# TODO

Known bugs, most serious first. Found in the code review of 2026-09-28; lines are as of `11314dd`.

## Every game (offline too)

- [ ] **A `RunPolicy::Main` scene that throws brings down the main loop** (`modules/core/src/Engine.cpp:297`).
  - The exception escapes `Engine::step()`. The README says such a scene "is logged and stopped", which is only
    true for `Dedicated` and `Pooled` scenes (`Engine.cpp:189`).
  - The failing scene also stays in the list, so the next `step()` throws again.
  - Fix: catch around `m_main.run`, log, and remove the scene that threw, as `runDedicated` does.
- [ ] **A trailing `# comment` in a `.cfg` file becomes part of the value** (`modules/core/src/ConfigFile.cpp:68`).
  - `Space, Pad.A  # fire` loses `Pad.A`. The README's own keybinds example uses this style.
  - The tilemap parser already strips such comments (`TileMap.cpp:142`): do the same here.
- [ ] **Calling `SceneHandle::stop()` twice pops two scenes** (`modules/core/src/SceneHandle.cpp:8`).
  - Each call posts a `StopRequest`. On the main stack, the second one pops the scene underneath.
- [ ] **Spawning during shutdown can outlive `run()`** (`modules/core/src/Engine.cpp:141`).
  - `spawnScene` reads `m_stopSpawned` before taking the lock, so a scene spawned at that moment can keep running
    after `run()` returns. Check the flag under the lock.
- [ ] **Saves don't survive a power loss** (`modules/core/src/Serializer.cpp:114`).
  - `writeFile` renames the temporary file without an `fsync`: a save survives a crash, not a power cut.
- [ ] **Logger text is treated as a format string** (`modules/logger/src/Logger.hpp:140`).
  - The one-argument overloads pass the message to `std::vformat`, so a message containing `{` throws. No current
    call does this.
- [ ] **Two engines calling `run()` in one process share one signal flag** (`modules/core/src/Engine.cpp:12`, `:30`).
  - This is the host setup (a server and a client in one process). Each engine saves and restores the other's
    handlers. It works in today's shutdown order, but it is fragile.

## Online games

- [ ] **`Prediction` keeps writing into its entity after the server removes it**
  (`modules/replication/src/Prediction.hpp:149`, `:153`).
  - It has no way to learn that the entity is gone. Since kronkworld reuses entity ids, it then writes
    `Transform2D` and `Body` into whatever entity gets that id next.
- [ ] **R-Type leaves the previous game's entities on screen** (`example/rtype/client/RTypeClient.hpp:166`, `:197`).
  - When a new game starts, nothing removes the old replicated entities, so they stay frozen on screen.
  - Starfall (`docs/16-make-a-coop-shmup`) has the fix: remove them in `joined` and `onRoomClosed`.
- [ ] **A replication client can get stuck after a bad snapshot**.
  - A malformed snapshot is dropped without asking the server for a full one
    (`modules/replication/src/ReplicationClient.cpp:109`).
  - The server skips an op too big to send (`oversized`) but still records the snapshot as sent
    (`modules/replication/src/ReplicationServer.cpp:104`, `:129`). Later diffs refer to an entity the client never
    got, and replication stalls.
- [ ] **`RoomClosed` arrives before the room's last snapshots** (`modules/server/src/RoomScene.cpp:193`).
  - The client drops its replication as soon as it hears it, so the final state (a game result, for example)
    never shows unless the room waits before calling `finish()`.
- [ ] **A full server doesn't send `Reject`** (`modules/net/src/Endpoint.cpp:386`).
  - The client sees `Unreachable` or `Timeout` instead of the `Refused` the README promises. The test only checks
    that the client gets some disconnection.

## kronknet (`vendor/kronknet`, commit in that repo, then bump the submodule)

- [ ] **A TCP client is kicked when its socket is full** (`src/client/client_send.c:40`, `:54`).
  - The client's ring buffer is never created (`client->buff` stays NULL, the client is allocated with `calloc`),
    so on `EAGAIN` or a partial write the client is kicked instead of buffering.
- [ ] **Leftover from the merge: every UDP send re-arms epoll** (`src/connection/hooks/udp/conn_udpSend.c:35`).
  - `knConnection_setEvents(conn, EPOLLOUT | EPOLLIN)` is harmless but wasted work: 2 `epoll_ctl` calls, a wakeup
    and a walk over the connection map per datagram. Remove the line.

## Done

- [x] Reliable messages lost when kronknet glued UDP datagrams (`11314dd`, kronknet `3aa3ad3`).
- [x] Use-after-free when a handler destroys its `Endpoint` (`11314dd`).
- [x] R-Type: a dead player's ship id could steer another player's ship (`11314dd`).
- [x] kronknet: new UDP connection used before its null check (fixed upstream in the epoll refactor).
