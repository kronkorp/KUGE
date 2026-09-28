# Step 6: Modules

The core knows nothing about windows, sockets or sound. A **module** is how such a part plugs into the engine
without the core knowing it exists. This step shows the whole interface, when each hook is called, and how a module
lends an object to scenes.

## The interface

```cpp
// modules/core/src/Module.hpp
class Module
{
    public:
        virtual ~Module(void) = default;
        virtual void onAttach(Engine&) {}
        virtual bool sharedAcrossThreads(void) const noexcept { return false; }
        virtual void inject(kw::World&) {}
        virtual void beginFrame(Engine&) {}
        virtual void endFrame(Engine&) {}
};
```

| Hook | Called by | When |
|---|---|---|
| `onAttach` | `Engine::addModule` | once, right after the module is built |
| `inject` | `Scene::attach`, through `Engine::inject` | each time a scene is entered, **before** its `onEnter` |
| `beginFrame` | `Engine::step` | at the start of each loop, before the ticks |
| `endFrame` | `Engine::step` | at the end of each loop, after the frame and the side loops |
| `sharedAcrossThreads` | `Engine::inject` | asked for each scene that is entered |

```cpp
// modules/core/src/Engine.hpp, Engine::addModule
auto module = std::make_unique<M>(std::forward<Args>(args)...);
M& added = *module;

module->onAttach(*this);
m_modules.push_back(std::move(module));
return added;
```

## Injection: what a scene gets

```cpp
// modules/core/src/Scene.cpp, Scene::attach
m_ctx = context;
m_ctx.engine().inject(*m_world, mainThread);

// modules/core/src/Engine.cpp, Engine::inject
for (auto& module : m_modules) {
    if (mainThread || module->sharedAcrossThreads()) {
        module->inject(world);
    }
}
```

`mainThread` is `true` for the main scenes and for scenes spawned with `RunPolicy::Main`, and `false` for the
dedicated and pooled ones (step 7). A module whose objects belong to the main thread, like a window or a renderer,
keeps the default `false`, and a room running on its own thread never sees them. That is enforced at run time: the
resource is simply absent. (The build-time version of the same rule is the module boundaries of step 1.)

## `Ref<T>`: lending without copying

```cpp
// modules/core/src/Ref.hpp
template<typename T>
class Ref
{
    public:
        explicit Ref(T& target) noexcept : m_target(&target) {}
        T& get(void) const noexcept { return *m_target; }
        T& operator*(void) const noexcept { return *m_target; }
        T* operator->(void) const noexcept { return m_target; }

    private:
        T* m_target;
};
```

A resource is one value per type in a World (step 3). To give every scene **the same** input map, a module does not
copy it: it puts a `Ref<InputMap>`, a pointer, in each World. The module owns the object. It is safe because
modules outlive scenes: `~Engine` leaves every scene first, and destroys the modules last (in reverse order).

## The real one: `ClientModule`

```cpp
// modules/client/src/ClientModule.cpp
void ClientModule::inject(kw::World& world)
{
    world.addResource<Ref<IWindow>>(*m_backend.window);          // lent: one window for everyone
    world.addResource<Ref<IRenderer2D>>(*m_backend.renderer);
    world.addResource<Ref<InputMap>>(m_input);
    world.addResource<Ref<AssetManager<Texture>>>(m_textures);
    ...
    world.addResource<ActionState>();                            // values: each scene its own
    world.addResource<Camera2D>();
    world.addResource<AnimationEvents>();
}

void ClientModule::beginFrame(Engine& engine)
{
    m_textures.pump();                                  // finish the textures decoded in the background (step 8)
    m_events.clear();
    m_backend.input->poll(m_events);                    // SDL events (or the dummy's)
    for (const Event& event : m_events) {
        if (std::holds_alternative<QuitEvent>(event)) {
            engine.stop();                              // closing the window stops the engine
        }
        m_input.handle(event);                          // keys become actions (step 8)
    }
    ... hot reload, every watchAssets seconds
    m_text.beginFrame();
    m_backend.renderer->begin(m_config.clearColor);     // clear the screen
}

void ClientModule::endFrame(Engine&)
{
    ... a screenshot, if one was asked for
    m_backend.renderer->present();                      // show what the Frame systems drew
    m_mixer.update();                                   // fades, finished voices
}
```

The split between a `Ref` and a value is deliberate. What must be one object (the window, the keys, the assets) is
lent. What is per scene (its camera, the actions sampled by its ticks) is a value in each World. A pause scene
pushed over a level has its own camera, but reads the same keys.

`ClientModule::onAttach` throws if the engine is not `Windowed`. That check is how a server can never, by mistake,
end up with a window.

## Lab

`example/lab/lab06_modules.cpp`:

```cpp
// Lab 6: what a module gives to the scenes, and to which of them
#include "Engine.hpp"
#include "Logger.hpp"
#include "Ref.hpp"
#include "Scene.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <string>
#include <thread>

namespace
{
    struct Counter { int frames = 0; };

    // A module that counts the loops of the engine, and lends its counter to the scenes
    class CounterModule final : public kuge::Module
    {
        public:
            explicit CounterModule(bool shared) : m_shared(shared) {}

            bool sharedAcrossThreads(void) const noexcept override { return m_shared; }
            void inject(kw::World& world) override { world.addResource<kuge::Ref<Counter>>(m_counter); }
            void beginFrame(kuge::Engine&) override { ++m_counter.frames; }

        private:
            Counter m_counter;
            bool    m_shared;
    };

    std::atomic<bool> g_entered{false};

    class Reader final : public kuge::Scene
    {
        public:
            explicit Reader(std::string where) : m_where(std::move(where)) {}

            void onEnter(void) override
            {
                try {
                    Counter& counter = *world().getResource<kuge::Ref<Counter>>();

                    std::printf("  %-26s has Ref<Counter> (frames so far: %d)\n", m_where.c_str(), counter.frames);
                } catch (const std::exception&) {
                    std::printf("  %-26s has no Ref<Counter>\n", m_where.c_str());
                }
                g_entered = true;
            }

        private:
            std::string m_where;
    };

    void tryWith(bool shared)
    {
        kuge::Engine engine({.mode = kuge::Engine::Mode::Headless, .tickRate = 60});

        std::printf("sharedAcrossThreads() = %s\n", shared ? "true" : "false");
        engine.addModule<CounterModule>(shared);
        engine.scenes().change<Reader>("a main scene");
        engine.step(1.0 / 60.0);
        engine.step(1.0 / 60.0);
        g_entered = false;
        engine.spawn<Reader>(kuge::RunPolicy::Dedicated, "a scene on its own thread");
        while (!g_entered) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

int main()
{
    Logger::logger().enable(false);
    tryWith(false);
    tryWith(true);
}
```

What it prints (`./build/example/lab06_modules`):

```text
sharedAcrossThreads() = false
  a main scene               has Ref<Counter> (frames so far: 0)
  a scene on its own thread  has no Ref<Counter>
sharedAcrossThreads() = true
  a main scene               has Ref<Counter> (frames so far: 0)
  a scene on its own thread  has Ref<Counter> (frames so far: 2)
```

## Reading the output

- The main scene gets `Ref<Counter>` in both cases. It is entered at the start of the first loop, before the first
  `beginFrame`, so it sees 0 frames.
- With `sharedAcrossThreads() == false`, the scene on its own thread has **no** `Ref<Counter>`: `getResource` threw,
  because `inject` was never called for its World.
- With `true`, it gets it, and sees **2** frames: the same `Counter` object the module incremented during the two
  loops before. A `Ref` points at the module's object; nothing was copied.
- Saying `true` makes a promise the engine cannot check: that the object can be used from several threads at once.
  `Counter` here is only read once, after the main thread stopped writing to it. A real shared module must protect
  its object itself (with a mutex or atomics).

Next: [Step 7: Threads](../07-threads/README.md).
