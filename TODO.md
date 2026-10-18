# TODO

Known bugs, most serious first. Found in the code review of 2026-09-28. The ones that were fixed are under "Done",
each with its pull request.

## Windows

- [ ] **The unit tests do not run on Windows.** kronklab runs each test in a `fork()` and finds the tests with the
  ELF linker's `__start_`/`__stop_` symbols: neither exists there. Porting it (a child process per test, and MSVC's
  sections) would also need the tests' own POSIX parts (`unistd.h`, raw sockets, `--wrap` at link time) made
  portable. Until then, Windows runs the boundaries and the three games played for real (`*_smoke`).

## 3D

What the 3D chapter ([docs/18-3d](docs/18-3d/README.md)) says is not there yet, and what to do in kronk3d:

- [ ] **kronk3d's `Logger` has no namespace.** It is a copy of KUGE's, with the same global names: linked together
  they define the same symbols with other layouts (a heap overflow before the fix of #41, a link error before
  Arena). The root `CMakeLists.txt` renames them for the compiler in kronk3d's logger and in `Log.cpp` (a macro per
  name), which is a patch of ours over a submodule. In kronk3d: put the logger in a namespace, publish `v0.1.1`,
  bump `vendor/kronk3d`, and take the renames out.
- [ ] **kronk3d's `Log.hpp` (`setLogSink`) is not a public header**, though its README documents it:
  `kuge-render3d` takes it from kronk3d's sources (include path in
  `modules/render3d/CMakeLists.txt`). Move it to `include/kronk3d/` in kronk3d.
- [ ] **OpenGL.** kronk3d's `HardwareRasterizer` is not wired to the client: it needs a window with a GL context
  (the SDL backend has none), a `glLoader` from `SDL_GL_GetProcAddress`, and text and interface drawn through
  kronk3d's 2D overlay (`draw2D`) because `SDL_Renderer` and a GL context do not share a window well. Nothing
  needed from kronk3d is known to be missing.
- [ ] **Nobody has measured a frame.** The software rasterizer at 1280x720 (the copy of the image included) is not
  timed; `drawInstances` (many copies of a mesh) is not used by `ModelRender`.
- [ ] Small things kronk3d has the tools for: spatial audio (`IAudio` takes a volume and a pan, nothing computes
  them from a position), picking by ray from the mouse, particles (`k3::BillboardBatch`), a text file for the cues
  of the 3D clips (the 2D ones have one), interpolating poses between two ticks, a camera with roll.
- [ ] **Windows is not checked by hand.** The CI builds and runs the 3D tests' smoke programs with MSVC; Jolt and
  the 3D examples have not been seen on a Windows machine.

## Done

- [x] `vendor/kronknet` is bumped to its `main` (`4c42839`): kronknet #41 and #42 are in, and so are Windows and
  IPv6 (#33).
- [x] A spawned `RunPolicy::Main` scene that throws is logged and stopped, and no longer brings down `Engine::step()`
  (#3). (The engine's own main scenes still let an exception out of `step()`: that is tested and on purpose.)
- [x] The README's keybinds example had a `# comment` after a value, which `ConfigFile` documents as part of the
  value: the example is fixed, the parser is kept (#4). Comments after values would need a rule and an escape.
- [x] `SceneHandle::stop()` twice popped two scenes (#5).
- [x] Spawning during shutdown could outlive `run()` (#6).
- [x] `writeFile` now syncs the file, then the folder (#7).
- [x] The logger's one-argument overloads no longer read the text as a format string (#8).
- [x] Two engines in one process no longer leave a dead signal handler behind, and one signal ends both (#9).
- [x] `Prediction` lets go of the player's entity when the server or the game removes it (#10).
- [x] R-Type removes the previous game's entities when it joins the next (#11).
- [x] Replication no longer stalls after a record too big to send, and a client asks for everything after a diff it
  cannot read (#12). (An entity bigger than a packet is still never shown: it is counted in `oversized`.)
- [x] `RoomClosed` no longer overtakes the room's last snapshots: a room runs `closeDelay` after `finish()` (#13).
- [x] A full server answers `Reject`, so the client sees `Refused` (#14).
- [x] Reliable messages lost when kronknet glued UDP datagrams (`11314dd`, kronknet `3aa3ad3`).
- [x] Use-after-free when a handler destroys its `Endpoint` (`11314dd`).
- [x] R-Type: a dead player's ship id could steer another player's ship (`11314dd`).
- [x] kronknet: new UDP connection used before its null check (fixed upstream in the epoll refactor).
