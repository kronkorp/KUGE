# 12 Testing and debugging

## Running the tests

```sh
ctest --test-dir build --output-on-failure
```

| Program | Covers |
|---|---|
| `kuge_tests` | core: loop, scenes, modules, threads and messages, serializer, config, tilemaps, assets, saves, snapshots |
| `kuge_client_tests` | client: inputs, sprites, tiles, animations, audio, text, UI, the SDL backend on SDL's dummy screen, Pong and the platformer |
| `kuge_physics_tests` | physics: shapes, broad phase, scenarios, a property test, bit-for-bit replay |
| `kuge_logger_tests`, `kuge_logger_init_tests` | logger: whole lines from many threads, first use from many threads |
| `kuge_net_tests` | messages on the wire, the reliable channel alone, the loopback, endpoints, a lossy network, real TCP and UDP, scenes that talk |
| `kuge_server_tests` | joining, rooms, tokens, cut cables, the end of a game, real sockets |
| `kuge_replication_tests` | snapshots, interpolation, prediction, inputs, a whole stack through a room |
| `kuge_rtype_tests` | R-Type: the rules, a host, a dedicated server, several rooms |
| `boundary_*` | a module cannot include what it does not link |
| `*_smoke` | the real programs (Pong, the platformer, R-Type) play a scripted game and leave a picture that is checked |

Run one program directly when you work on one module: `build/tests/kuge_net_tests`.

### kronklab, the test library

```cpp
extern "C" {
    #include "kronklab/kronklab.h"
}

Test(mysuite, adds_up)
{
    AssertEq(1 + 1, 2, "one and one");
    Assert(x > 0, "x is positive: %d", x);
    AssertStrEq(name.c_str(), "Ana", "the name");
}
```

Three things to know:

- **Test names are limited to 31 characters** (a compile error otherwise).
- **kronklab's exit code is always 0**, even when tests fail. `ctest` therefore reads its report line instead
  (`cmake/RunKronklab.cmake`). If you run a test program by hand, read the `[REPORT] total [...]` line.
- `Assert`'s message is a printf format: `Assert(a == b, "got %d", a)`.

## Sanitizers

Threaded and networked code is where the worst bugs hide: races, use after free, leaks. Two sanitizers find them:

```sh
cmake -S . -B build-asan -DKUGE_SANITIZE=address     # AddressSanitizer + UndefinedBehaviorSanitizer
cmake --build build-asan -j && ctest --test-dir build-asan

cmake -S . -B build-tsan -DKUGE_SANITIZE=thread      # ThreadSanitizer
cmake --build build-tsan -j
setarch "$(uname -m)" -R build-tsan/tests/kuge_net_tests    # on a recent kernel, disable address randomization
```

The dependencies are built instrumented too. Everything is clean under both, and it stays that way: run them before
you commit anything that touches threads or the network.

Three real bugs these found while the engine was built:

- an exception thrown by a system went through the C scheduler and leaked (ASan, LeakSanitizer);
- a room's endpoint was destroyed while a prediction still pointed at it (ASan, use after free);
- kronknet counts its connections in a process-wide variable with no lock, which two servers on two threads race on
  (TSan). Every call into kronknet now goes through one lock; a test with four servers on four threads proves it.

## Testing the network with a bad network

`LoopbackNetwork` misbehaves on purpose and reproducibly:

```cpp
double now = 0.0;
kuge::net::LoopbackNetwork network(
    kuge::net::LoopbackNetwork::Conditions{.loss = 0.3, .duplicate = 0.1, .latency = 0.05, .jitter = 0.03, .seed = 7},
    [&now] { return now; });                          // and its clock is yours
```

and `EndpointConfig::clock` lets an endpoint use the same clock, so a test can make ten seconds of timeout pass
by adding 10.0 to `now`, in no time. Look at `tests/net/net_fixture.hpp` (`Sim`) and
`tests/replication/rep_fixture.hpp` (`RepSim`): both are small and copyable.

## Making a test that can fail

After writing a test, **break the code on purpose** and check that the test fails, then put it back. A short
list of mutations that were used in this project, and what caught them:

| Mutation | Caught by |
|---|---|
| never resend a reliable message | the lossy-network tests |
| deliver reliable messages out of order | the reorder test |
| compute snapshots against the newest sent instead of the acknowledged one | "every snapshot was built on one the client had" |
| skip the replay of inputs after a correction | "the prediction is right nearly always" |
| remove the smoothing of a correction | "the drawn player never jumps" |
| read one datagram per poll | "all 200 came out of one poll" |
| let the lobby overfill a room | the multi-room tests |

## Debugging a networked game

1. **Reproduce it in a test with the loopback.** Real networks are not reproducible; a seeded loopback is.
2. **Print the report.** The client scene's `ClientReport` and `endpoint.stats()`, `replication.stats()`,
   `prediction.stats()` tell you what the layers saw: packets, resends, duplicates, malformed, corrections.
3. **Look at the picture.** A scripted `--screenshot` run shows what is really drawn.
4. **Corrections climbing?** See "Reading the numbers" in [tutorial step 7](../11-make-rtype/07-making-it-feel-right/README.md).
5. **A player sees nothing?** Check, in order: did the client join (`state`)? Is the schema the same on both sides
   (`replication.stats().wrongSchema`)? Did the prefab add a `Sprite`? Is the camera looking at the right place?
6. **The log.** `Logger::logger().info(...)` is thread-safe and writes to the terminal and to `latest.log` in the
   current directory.
