# 18 3D

KUGE makes 3D games out of the same parts as 2D ones, with three more modules and one library:

| Piece | What it is |
|---|---|
| [kronk3d](https://github.com/kronkorp/kronk3d) (`vendor/kronk3d`, v0.1.0) | The renderer: meshes, materials, lights, shadows, sky, fog, glTF and OBJ loading, skeletal animation. KUGE uses it as it is |
| `kuge-render3d` | A 3D scene in a client: `Render3DModule`, `Model3D`, `Light3D`, `Animator3D`, `CameraRig` |
| `kuge-physics3d` | Rigid bodies with [Jolt](https://github.com/jrouwe/JoltPhysics): `Physics3D`, `Collider3D`, `Body3D` |
| `kuge-replication` | `registerTransform3D`, `registerBody3D`, `registerAnimationState3D`, and `Prediction` for 3D |
| `kuge-core` | `Transform3D`, `AnimationState3D`, the components `Collider3D` and `Body3D` (no Jolt, so a server of anything can use them) |

KUGE does not wrap what kronk3d already does: the camera is a `k3::Camera`, the environment a `k3::Environment`,
a model a `k3::Model`, a light a `k3::Light`. Their documentation is kronk3d's README.

The best way to see it all together is `example/arena3d`, a small game with a server, a client and a host:
[Arena](#arena-a-game-in-three-programs), below.

## How it is drawn

`Render3DModule` owns kronk3d's **software rasterizer**: the scene is drawn in memory by the CPU, then the image
is given to the 2D renderer, which shows it on the whole screen. Two things follow:

- it works with every backend, with no OpenGL context, in the tests (the dummy backend) and on a machine with no GPU;
- sprites and the interface are drawn **over** the 3D, with the tools you already know.

The cost is the CPU (and one copy of the image per frame). `Render3DConfig::renderScale` draws at a fraction of
the screen and enlarges it; `antiAliasing` is kronk3d's (FXAA or SSAA). An OpenGL backend is possible, and
kronk3d has one; it is not wired to the client yet.

```cpp
auto& client = engine.addModule<kuge::ClientModule>(kuge::makeSdlBackend({.title = "My game", .width = 960, .height = 540}));
engine.addModule<kuge::Render3DModule>();          // after the client: it is destroyed before it
```

## A scene

A 3D scene inherits `Scene3D` and calls `installClient3DSystems()` where a 2D one calls `installClientSystems()`:

```cpp
class Level : public kuge::Scene3D {
    void onEnter() override {
        installClient3DSystems();

        auto& camera = world().getResource<k3::Camera>();        // resources of the World
        camera.position = {0, 2, 6};
        camera.lookAt({0, 0, 0});
        auto& sky = world().getResource<k3::Environment>();
        sky.ambient = {0.3f, 0.3f, 0.35f, 1};
        sky.lights.push_back(k3::Light::directional({-0.4f, -1, -0.3f}));

        const kw::Entity crate = world().create();
        world().add<kuge::Transform3D>(crate, kuge::Transform3D{});
        world().add<kuge::Model3D>(crate, kuge::Model3D{model});            // a shared_ptr<const k3::Model>
    }
};
```

The world is right-handed, **y points up**, units are meters, and a `Transform3D` is kronk3d's `k3::Transform`
(translation, rotation as a unit quaternion `x y z w`, scale). `kuge::axisAngle(axis, radians)` makes a rotation.
As in 2D, give an entity a `PreviousTransform3D` (same value) and it is drawn between its two ticks.

| Resource or component | What it does |
|---|---|
| `k3::Camera`, `k3::Environment` (resources) | The view, the sky, the fog, the ambient light, the shadows |
| `Model3D` | Draws a `k3::Model` at the `Transform3D` of the entity |
| `Light3D` | A `k3::Light` that follows its entity (at most 8 lights light the scene, the environment's first) |
| `Animator3D` | A glTF model that plays clips (below) |
| `CameraRig` | Makes the camera follow an entity, first person or orbiting (below) |
| `ViewModel3D` | What a first-person player holds (a gun): drawn in front of everything, from the camera, with a field of view of its own, so that it never goes through a wall (kronk3d's "layer") |

Systems run in this order each frame: `UpdateCamera`, `ModelRender` (draws the 3D), then the sprites and the
interface, so **2D is over 3D**.

## Models from files

`Render3DModule` has two asset managers, like the textures of the client (`Ref<AssetManager<...>>` in the World):

- `models()`: Wavefront **OBJ** (with its `.mtl` and pictures) as a `k3::Model`;
- `animatedModels()`: **glTF 2.0** (`.glb`, `.gltf`, what Blender exports) as a `k3::AnimatedModel`.

```cpp
auto tree = render.models().load("assets/tree.obj");                  // throws kuge::ModelError, naming the file
auto ticket = render.animatedModels().loadAsync("assets/hero.glb");   // read on a worker, made by the next loop
```

Asking twice for the same file gives the same model while someone holds it. With
`Render3DConfig{.watchAssets = 1.0}`, a model whose file changed is read again **in place** (a file that does not
read leaves the model as it was); `reloadAssets()` does it by hand.

## Animation

`Animator3D` is kronk3d's `k3::Animator` (skeleton, clips, crossfades) on an entity that has a `Transform3D`:

```cpp
world.add<kuge::Animator3D>(hero, kuge::Animator3D::of(model, "idle"));
auto& animator = world.get<kuge::Animator3D>(hero);
animator.play("run");                                       // fades from the clip that plays (0.2 s)
animator.play("attack", {.loop = false}, "idle");           // and goes to idle when it ends
```

`AnimateModels` moves it forward by one tick. **Cues** are what happens at a time of a clip (a footstep): give the
animator a `ClipCues` (`clip name -> [{time, name}]`) and the cues crossed arrive in `AnimationEvents`, the same
resource as the 2D ones. A static glTF is drawn by an `Animator3D` with no clip (its rest pose). The pose
changes at each tick, not at each frame.

On a network, the server has no model: it says which clip an entity plays with an `AnimationState3D`
(`{clip, loop, serial}`, replicated), and `PlayReplicatedClips` plays it on the client's `Animator3D`. A `serial`
that moves starts the same clip again. Nothing about the pose travels: every client animates by itself.

## Mouse look and cameras

`IInputSource::setRelativeMouse(true)` hides the cursor and keeps it in the window (a scene reaches it as
`Ref<IInputSource>`). `InputMap::mouseDelta()` is how far the mouse moved during the frame.

```cpp
world.add<kuge::CameraRig>(player, kuge::CameraRig{});                                  // first person, at 1.7 m
world.add<kuge::CameraRig>(player, kuge::CameraRig{.mode = kuge::CameraRig::Mode::Orbit, .distance = 5.0f});
```

`UpdateCamera` turns `yaw` and `pitch` with the mouse and places the `k3::Camera`; read the rig's `yaw` to walk
the way the player looks. There is no roll (`k3::Camera` has none). `sensitivity` is in radians per pixel (0.0013:
0.075 degrees), and `maxMove` is the most pixels that count in one frame (a jump of the mouse is not a hand).

The SDL backend looks after the mouse of a mouse look: it gives it back when the window loses the focus and takes it
again when it gets it back, and the first move after it is taken carries no delta (the cursor is brought to the
middle of the window). A game should let the player free the mouse (Arena: Esc gives it, a click takes it).

## Physics

`kuge-physics3d` runs Jolt, on one thread (scenes already run side by side). An entity with a `Transform3D` and a
`Collider3D` **is** a body, which the physics makes, moves and removes by itself:

```cpp
kuge::installPhysics3D(setup(), {.gravity = {0, -9.81f, 0}});          // in onEnter(): the resource and the system

world().add<kuge::Collider3D>(floor, kuge::Collider3D::box(50, 1, 50));                  // a floor: no Body3D
world().add<kuge::Collider3D>(crate, kuge::Collider3D::box(1, 1, 1));
world().add<kuge::Body3D>(crate, kuge::Body3D{});                                          // it falls
world().add<kuge::Collider3D>(zone, kuge::Collider3D::sphere(2).asTrigger());              // reports who is inside
```

- **Shapes**: `box`, `sphere`, `capsule` (standing along y); `asTrigger()`, `onLayer(bits, meets)`; friction and
  restitution are fields.
- **Bodies**: `Static`, `Kinematic` (moves by its velocity, pushes what it meets), `Dynamic` (gravity, pushed).
  `lockRotation` keeps a character upright (and without friction against walls: pressed against one in the air it
  falls instead of sticking), `mass` is optional, `linearDamping` and `angularDamping` slow a body down by itself (a
  crate that stops sliding), `grounded` is written by the physics.
- **The ECS is the truth**: what the game wrote in a `Transform3D` or a `velocity` since the last tick (a teleport,
  a walk) is given to Jolt; Jolt steps; the results are written back. A character walks by setting `velocity.x`
  and `velocity.z` and leaving `y` to the gravity.
- **Questions**: `physics.raycast(origin, direction, maxDistance, mask, triggers)`, `overlapSphere(center, radius)`
  and `physics.events()` (a `TriggerEvent3D` for each trigger entered or left in the last tick).

Jolt comes from vcpkg (`vcpkg.json`, feature `physics3d`) and is built the first time; `-DKUGE_BUILD_PHYSICS3D=OFF`
leaves it out. The same world gives the same result, bit for bit, on one machine; Jolt is not built to agree
across platforms.

## On a network

The replication of the 2D chapter works as it is, with components that are 3D:

```cpp
registerTransform3D(registry, Replicate::Interpolated, /*predicted=*/true);   // position, rotation, scale
registerBody3D(registry, Replicate::OnChange, true);                         // type, velocity, mass...
registerAnimationState3D(registry);                                          // the clip of a character
```

`Prediction<Input, Transform3D>` makes the player's own body answer at once, as in 2D: the client simulates its
body on a private world (the level and the player: the same `build`, `apply` and `step` functions as the room),
and puts it right when a snapshot says otherwise, sliding the correction in over `smoothing` seconds, or
snapping if it is more than `snapDistance` meters. `Prediction<Input>` is `Prediction<Input, Transform2D>`.

Which model an entity shows is what `Replicated::type` is for: the game builds the entity on the client from its
type (`onSpawn`). Where the player looks is part of the game's own input (`yaw` in Arena's `Steer`): it is not
in `ActionState`.

## Arena, a game in three programs

`example/arena3d` is a small shooter that uses all of the above: up to four players run, jump, push crates and
shoot one another on a map with ramps, platforms and cover; four hits kill, the dead come back after 2.5 seconds,
the first to eight frags wins. Like R-Type it is a server, a client and a host that share one folder:

```
example/arena3d/
  assets/   robot.glb (the Godot robot, with its clips), crate.obj, textures, make_assets.py
  common/   Arena.hpp/.cpp   what is shared: Steer (the input), the registry, the rules
            Map.hpp/.cpp     the map: boxes (some tilted into ramps), the spawns, the crates
  server/   ArenaRoom, main  a room: inputs, Jolt, bullets, deaths, replication; the server has no window
  client/   ArenaClient, Models, Script, main    the scene: assets, camera, prediction, a HUD
  host/     main             the room and the window in one process (a game that plays alone)
```

```sh
cmake -S . -B build -DBUILD_EXAMPLES=ON
build/example/kuge_arena3d_server &        # then, in other terminals:
build/example/kuge_arena3d_client          # W A S D or the arrows, the mouse turns the view, Space jumps, the left
                                           # button (or F) fires, V looks from the eyes or from behind, Esc frees the
                                           # mouse (a click takes it again)
build/example/kuge_arena3d_host            # or: both in one process
```

What to read in it:

- **The assets.** The character is **the robot of Godot's 3D platformer demo** (`robot.glb`, MIT: see
  `assets/ROBOT.md`), a skinned glTF with the clips `idle`, `walk`, `run`, `jump`, `falling`, `shooting` and
  `shooting_standing`. `client/Models.hpp` (`makeRobot`) makes a copy for each player: its size and direction, the
  glowing part (the material `player_glow`) in the colour of the player, and a clip for dying (it falls on its back,
  which the file does not have). The crate is an OBJ with its texture, the level has textures; `assets/make_assets.py`
  makes those with the standard library only (the files are always the same). The client loads them with
  `Render3DModule::animatedModels()` and `models()`.
- **The map is one list** (`common/Map.cpp`) that makes two things: colliders (`buildLevel`, for the room and for
  the prediction) and meshes (`LevelTextures::modelOf`, for the client). Ramps are boxes tilted by `ramp()`: a
  floor you see is a floor you stand on.
- **Moving** (`steerPlayer`): the room and the prediction both turn an input into a velocity, and turn the body the
  way the player looks (so the other players see it face where it aims). Jolt does the rest: gravity, ramps, walls,
  and the push of a player on a crate (a body of 70 kg on one of 60).
- **Shooting** (`fire`, `stepRules`): a bullet is not a physics body (too fast): it is an entity that the rules move
  at each tick, with a ray from where it was to where it goes (`Physics3D::raycast`). It hurts a player that is
  not its shooter, shoves a crate, and stops at anything. Killing makes a corpse (an entity that plays `death`) and
  schedules the return of the player: its body is removed, the client's prediction lets go of it and takes the
  next one.
- **The clips** (`stepRules`): the room picks the clip of each character from what it does (in the air for a few
  ticks: `jump` going up, `falling` coming down; firing: `shooting` on the move, `shooting_standing` still; moving:
  `run`; else `idle`) and says it in an `AnimationState3D`. Every client plays it (`PlayReplicatedClips`), for the
  others and for itself.
- **Prediction near crates and players** (`syncProxies`): the world that predicts a client's player has only the
  level and the player, so it would walk through a crate that the room stops it at. The client puts in it the crates
  (bodies simulated there too, put back where the server has them when they go too far) and the other players (bodies
  that stand where they are shown), and the player is stopped and pushes as on the server.
- **The view** (`client/ArenaClient.hpp`): first person by default (V changes it) with a gun held as a `ViewModel3D`
  that kicks when it fires; fog; a day that goes by in 150 seconds (`--day SECONDS`) by the clock of the server, so
  that every player has the same light: the sun and the moon are two directional lights, and the sky is one picture
  that `applySky` dims and tints. The window is 1280x720 and can be resized; the mouse sensitivity is
  `--sensitivity DEGREES_PER_PIXEL`.
- `client/ArenaClient.hpp`: `spawned()` gives each kind of entity its model, the camera rig to the player's own body,
  and `tick()` sends the input with the yaw and the pitch. The HUD is 2D sprites over the 3D: a crosshair, the health
  bar, and a square of the colour of its shirt for each frag of each player.
- `arena3d_smoke` (a test) starts the real programs on SDL's dummy screen: a client and then a host each join a
  game and play a script (turn, run, jump, shoot). The exit code says that the character of the client played
  `idle`, `run`, `jump` and `shoot`, and that its bullets were drawn; the shell looks in the picture for the pure
  cyan of the shirt, the green of the health bar and the white of the crosshair, and for hundreds of other
  colours. `kuge_arena3d_tests` plays the rules with no network and no screen: the map is free at every spawn,
  ramps are walked up, walls hold, crates are pushed, shots hurt and kill and miss when aimed too high, the dead
  come back, the match ends, each clip follows what the player does, and the assets load.

## What is not there yet

- The OpenGL backend of kronk3d (`HardwareRasterizer`) is not wired to the client: it needs a window with a
  GL context, and text and interface drawn through kronk3d's 2D overlay.
- Spatial audio, picking by ray from the mouse, particles (`k3::BillboardBatch`), instancing of many copies of a
  mesh (`drawInstances`): kronk3d has the tools, KUGE has no component for them.
- Cues have no text file (the 2D clips do); poses are not interpolated between two ticks; no roll for the camera.
- Windows (MSVC) is built by the CI like the rest; Jolt and the 3D are not checked on a machine of ours.
