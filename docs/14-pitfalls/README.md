# 14 Pitfalls

Every item here was met while building the engine and the games. Reading them is cheaper than meeting them.

## The ECS

- **A view's order is not stable.** `world.view<...>()` visits entities in the order the ECS stores them, which
  changes when entities are removed. Anything reproducible (physics, rules, events, drawing) must sort by entity
  number first. Symptom: a replay differs from the original run, or a server and a client disagree, only sometimes.
- **Do not remove while you walk a view.** Collect the entities into a vector, then remove.
- **256 component types, 256 resource types**, per process. Do not register a type per level or per player.
- **Components are added with aggregate initialization.** `world.add<C>(e, C{...})`; a component with a
  user-provided constructor may not work the way you expect.
- **`MAX_COMPONENTS` is a macro** in kronkworld. Do not name your own constant that: the preprocessor rewrites it into
  a syntax error. (The replication registry's limit is `MAX_REPLICATED` for this reason.)

## Scenes and threads

- **`change<T>(args...)` copies its arguments** (they are decayed into the scene's constructor). A reference parameter
  does not bind to your variable. Pass a pointer or a `std::shared_ptr`.
- **A scene must not touch another scene's data.** Use a `SceneHandle` and messages. If two threads must share a value,
  it must be atomic or read-only.
- **Constructor vs `onEnter`.** `ctx()` and the World's resources from modules are not available in the constructor;
  use `onEnter`.
- **`Engine::step` returns false when there is no scene**, and does not run the modules that loop. A test that steps
  an engine with no scene sees nothing happen.
- **Textures and sounds must not outlive the backend that made them.** In tests, let them go before the backend is
  destroyed.

## Physics

- **Layers and masks must agree both ways.** A trigger fires only if each thing is on a layer the other's mask includes.
  A hero whose mask does not include the coins' layer never picks a coin up. (This bug hid in the platformer until a
  test walked the hero into a coin.)
- **Bodies do not stop each other.** Use triggers, or your own overlap test.
- **Physics needs the same order every time**, and it has it. Do not use an `unordered_map` iteration in code you predict.

## Serialization and messages

- **`ByteReader` reads a buffer it does not own.** Building one on a temporary vector does not compile, on purpose.
  Keep the vector alive while you read.
- **A message must be a namespace-scope type.** `KUGE_MESSAGE` adds static members, which a local class cannot have.
- **Both sides must list the same fields in the same order.** The id is a hash of the name only. Change a field, and
  bump the endpoint's `protocol` (a mismatch is refused at connect) if old clients can still meet a new server.
- **Registered replicated components must match on both sides**, in the same order (one shared `makeRegistry()`), or
  clients ignore the snapshots (`stats().wrongSchema`).

## The network

- **An endpoint belongs to one thread**, the one that polls it. Do not send on it from another thread; send a message
  to the scene that owns it.
- **Do not keep a pointer to an endpoint that can go away.** A room's endpoint is destroyed when the room ends or the
  lobby is lost. Everything that holds it (prediction, replication client) must be dropped in `onRoomClosed`. This
  caused a use-after-free.
- **`send` can return `false`.** Not connected, too big, or the reliable queue is full. For a message that must not
  be lost, check.
- **A socket read gives one datagram per call in kronknet.** The transport loops until the socket is empty; if you write
  another transport on top of a library that does one read per call, do the same.
- **A client must not assume the server is up.** Retry to connect. A client started before its server that never
  retries shows an empty window and looks broken.
- **Loopback names are global to a `LoopbackNetwork`.** Two servers cannot listen at the same name; use distinct
  network objects in tests that run together.

## Prediction and time

- **Clocks drift.** A client that ticks 2 % faster than the room piles inputs up in the room's queue, and the
  prediction disagrees for ever. Run on real time (`Engine::run()`), or sleep to an absolute schedule in scripts, and
  keep a small input margin (`InputServerConfig::jitter`).
- **"Owned" is not "predicted".** A player owns its bullets too, but only its ship is predicted (`predictType`).
- **The client and the room must run the same movement code.** Copy-pasting it is the way to jitter. Share the
  function.
- **Do not predict what you cannot know.** Other players and server-only rules produce corrections; that is normal and
  is smoothed. Do not try to predict them.
- **Do not use `std::random_device`, the wall clock or unordered iteration in predicted code.**

## Tests

- **kronklab always exits 0.** Read the report line; `ctest` does.
- **Test names are at most 31 characters.**
- **Do not sleep, wait.** Poll a condition with a timeout (`waitFor`); never `sleep(1)` and hope.
- **Real-time tests need real time.** A test that runs a server and clients at 60 Hz takes seconds; keep them few, and
  cover the rest with fake clocks.
- **Run thread and network tests many times and under TSan.** A test that passes nine times in ten has a bug.
- **A test that cannot fail proves nothing.** Break the code, watch it fail, put the code back.

## Building

- **Do not build in the source folder** (CMake refuses). Use `build/`.
- **A server-only build** (`-DKUGE_BUILD_CLIENT=OFF`) is the cheap way to check that nothing client-side leaked into the
  server. Do it before releasing.
- **Only one copy of kronkpool is built** (the vendored one). If you add a dependency that pulls its own, you will see
  duplicate definitions.
