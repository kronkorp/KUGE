# Step 2: A project

For now a game lives **inside the repository**, in `example/`, and links the engine's modules as CMake targets.
(A `find_package(kuge)` for games outside the repository is on the list of things still to do.)

## The libraries and programs

`example/rtype/CMakeLists.txt`:

```cmake
# The shared code: the vocabulary, the arena, the rules
add_library(rtype-common STATIC common/Rules.cpp)
target_include_directories(rtype-common PUBLIC common)
target_link_libraries(rtype-common PUBLIC kuge-replication)

# The room: what a server runs
add_library(rtype-room STATIC server/RTypeRoom.cpp)
target_include_directories(rtype-room PUBLIC server)
target_link_libraries(rtype-room PUBLIC rtype-common kuge-server)

# The dedicated server program
add_executable(kuge_rtype_server server/main.cpp)
target_link_libraries(kuge_rtype_server PRIVATE rtype-room)

if(TARGET kuge-client)
    # The client scene is a header that the client and the host both include
    add_library(rtype-client INTERFACE)
    target_include_directories(rtype-client INTERFACE client)
    target_link_libraries(rtype-client INTERFACE rtype-common kuge-client)

    add_executable(kuge_rtype_client client/main.cpp)
    target_link_libraries(kuge_rtype_client PRIVATE rtype-client)

    # The host needs both worlds
    add_executable(kuge_rtype_host host/main.cpp)
    target_link_libraries(kuge_rtype_host PRIVATE rtype-client rtype-room)
endif()
```

What this says, in plain words:

- `rtype-common` links `kuge-replication`, which brings the network, the physics and the core with it. It links
  **no client and no server** module.
- `rtype-room` adds `kuge-server` to `rtype-common`. **That is all a server program links.** A machine with no SDL
  can build it.
- `rtype-client` adds `kuge-client` (SDL) to `rtype-common`.
- Only the host links both.

`if(TARGET kuge-client)` is what makes the client programs disappear when the engine is configured with
`-DKUGE_BUILD_CLIENT=OFF`, while the server still builds.

And in `example/CMakeLists.txt`:

```cmake
if(TARGET kuge-server AND TARGET kuge-replication)
    add_subdirectory(rtype)
endif()
```

## Check it as you go

Build only the pieces you have. After step 4 you can build `rtype-common` alone; after step 5 the server:

```sh
cmake --build build --target kuge_rtype_server
```

A very useful habit: build a **server-only** configuration now and then, to be sure nothing client-side slipped in:

```sh
cmake -S . -B build-server -DKUGE_BUILD_CLIENT=OFF
cmake --build build-server -j
```

If that builds and its tests pass, the separation is real.

Next: [Step 3: The shared vocabulary](../03-the-shared-vocabulary/README.md).
