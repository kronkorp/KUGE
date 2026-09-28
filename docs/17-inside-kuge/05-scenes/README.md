# Step 5: Scenes

A `SceneManager` is a stack of scenes and a queue of changes. This step follows a change from the moment it is
asked to the moment a scene's `onEnter` or `onExit` runs, and then a message from `send` to `onMessage`.

## Asking is not doing

```cpp
// modules/core/src/SceneManager.hpp
template<typename T, typename ...Args>
void change(Args&&... args)
{
    enqueue(Kind::Change, makeFactory<T>(std::forward<Args>(args)...));
}

// Keeps the arguments until the scene is made
template<typename T, typename ...Args>
struct FactoryOf final : Factory
{
    std::unique_ptr<Scene> make(void) override
    {
        return std::apply([](auto&... args) -> std::unique_ptr<Scene> {
            return std::make_unique<T>(std::move(args)...);
        }, m_args);
    }

    std::tuple<std::decay_t<Args>...> m_args;     // decayed: copies, never references
};
```

`change`, `push` and `pop` only append an operation to `m_pending`. The scene is not even built yet: a factory keeps
**copies** of the arguments (`std::decay_t`), and builds the scene when the operation is done. A reference you pass
becomes a copy. To share an object, pass a pointer or a `shared_ptr`.

## Doing: between two loops

`apply()` runs at the start of each loop (`SceneLoop::ready`) and at its end (`SceneLoop::settle`), never in the
middle:

```cpp
// modules/core/src/SceneManager.cpp, SceneManager::apply
while (!m_pending.empty()) {                  // (a scene entered here may ask for more: done in this same call)
    Op op = std::move(m_pending.front());

    m_pending.pop_front();
    switch (op.kind) {
        case Kind::Change:
            while (!m_stack.empty()) {
                leaveTop();                   // every scene, from the top down
            }
            enter(op.factory->make());
            break;
        case Kind::Push:
            if (!m_stack.empty()) {
                m_stack.back()->onPause();
            }
            enter(op.factory->make());
            break;
        case Kind::Pop:
            ...
            leaveTop();
            if (!m_stack.empty()) {
                m_stack.back()->onResume();
            }
            break;
    }
}
```

This is why **a scene is never destroyed while it runs**: a system can pop its own scene, and the scene is only left
once its tick, and the rest of the loop, are over.

## Entering and leaving

```cpp
// modules/core/src/SceneManager.cpp
void SceneManager::enter(std::unique_ptr<Scene> scene)
{
    // The parent is the one that spawned the loop: it is the root scene's parent
    scene->attach(SceneContext(m_engine, *this, m_time, scene->handle(), m_stack.empty() ? m_parent : SceneHandle()),
        m_mainThread);                        // ctx() is set, and the modules inject their resources (step 6)
    m_stack.push_back(std::move(scene));      // on the stack before onEnter: top() is right inside it
    m_stack.back()->onEnter();
}

void SceneManager::leaveTop(void)
{
    std::unique_ptr<Scene> scene = std::move(m_stack.back());

    m_stack.pop_back();                       // off the stack first: whatever happens, it is not left twice
    scene->onExit();
    scene->shutdown();                        // mailbox closed, systems removed (last added first)
}                                             // and here the unique_ptr destroys the scene and its World
```

A scene's constructor runs when the factory makes it, **before** `attach`. So `ctx()` and the modules' resources are
not there yet in a constructor. That is why everything is built in `onEnter`.

The **order of destruction** follows from the members of `Scene`: `onExit`, then the systems are removed (in
`shutdown`), then the derived class's members are destroyed, then the base class's `kw::World` with every component
and resource. That is why a `ReplicationClient` or a `MatchmakingClient` kept as a member of the scene can safely
point into the World's `Net`: it goes first.

## Only the top one runs

`SceneLoop::run` calls `m_scenes.top()` and nothing else (step 2). The scenes under it keep their World as it was:
no tick, no frame, no messages. The loop's tick counter (`Time::tick`) belongs to the loop, not to a scene, so a
scene pushed at tick 1 starts at tick 1, not 0.

## Messages: a mailbox per scene

```cpp
// modules/core/src/Mailbox.cpp
bool Mailbox::post(Message message)           // any thread
{
    std::lock_guard lock(m_mutex);

    if (m_closed) {
        return false;                         // the scene is gone
    }
    if (m_messages.size() >= m_capacity) {    // 4096 by default
        ++m_dropped;
        return false;                         // full: refused, not queued forever
    }
    m_messages.push_back(std::move(message));
    return true;
}

std::vector<Message> Mailbox::drain(void)     // the scene's thread
{
    std::vector<Message> taken;
    {
        std::lock_guard lock(m_mutex);
        taken.swap(m_messages);               // everything at once, the lock held only for a swap
    }
    return taken;
}
```

```cpp
// modules/core/src/Scene.cpp, Scene::deliverMessages: at the start of each loop of the scene, before its ticks
for (const Message& message : m_mailbox->drain()) {
    if (message.is<StopRequest>()) {
        m_ctx.scenes().pop();                 // SceneHandle::stop() is only a message
    } else {
        onMessage(message);
    }
}
```

A `SceneHandle` is a `shared_ptr<Mailbox>`. It can outlive the scene: the scene closes the mailbox when it is left
(`shutdown`) or destroyed, after which `alive()` is false and `send()` returns false. A paused scene does not drain
its mailbox, so it gets its messages when it runs again. Step 7 shows the same mailbox between two threads.

## Lab

`example/lab/lab05_scenes.cpp`:

```cpp
// Lab 5: the stack of scenes. When a change is asked, when it is done, and when a message arrives.
#include "Engine.hpp"
#include "Logger.hpp"
#include "Scene.hpp"
#include "Stage.hpp"
#include <cstdio>
#include <functional>
#include <string>

namespace
{
    struct Note { std::string text; };

    kuge::SceneHandle g_menu;   // the menu's handle, kept to write to it from outside

    class Say final : public kw::ISystem
    {
        public:
            explicit Say(std::function<void(kw::World&)> fn) : m_fn(std::move(fn)) {}
            bool handle(kw::World& world) override { m_fn(world); return true; }

        private:
            std::function<void(kw::World&)> m_fn;
    };

    class Logged : public kuge::Scene
    {
        public:
            explicit Logged(std::string name) : m_name(std::move(name)) {}

            void onEnter(void) override
            {
                say("onEnter");
                if (m_name == "menu") {
                    g_menu = ctx().self();
                }
                addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Say>([this](kw::World& w) {
                    const auto tick = w.getResource<kuge::Time>().tick;

                    std::printf("    %s ticks (tick %llu of the loop)\n", m_name.c_str(), static_cast<unsigned long long>(tick));
                    if (m_name == "menu" && tick == 0) {
                        ctx().scenes().push<Logged>("pause");
                        std::printf("    menu asked push(pause), and goes on: nothing changes during a loop\n");
                    }
                }));
            }
            void onExit(void) override { say("onExit"); }
            void onPause(void) override { say("onPause"); }
            void onResume(void) override { say("onResume"); }
            void onMessage(const kuge::Message& message) override
            {
                if (const auto* note = message.as<Note>()) {
                    std::printf("    %s got \"%s\" (at the start of its loop, before its ticks)\n", m_name.c_str(), note->text.c_str());
                }
            }

        private:
            void say(const char* what) { std::printf("    %s %s\n", m_name.c_str(), what); }

            std::string m_name;
    };
}

int main()
{
    Logger::logger().enable(false);
    kuge::Engine engine({.mode = kuge::Engine::Mode::Headless, .tickRate = 60});
    const auto loop = [&engine](int n) {
        std::printf("loop %d\n", n);
        engine.step(1.0 / 60.0);
    };

    engine.scenes().change<Logged>("menu");
    std::printf("asked change(menu): the stack is still empty (%zu scene)\n", engine.scenes().size());
    loop(1);
    std::printf("send \"hello\" to the menu, which is paused: %s\n", g_menu.send(Note{"hello"}) ? "posted" : "refused");
    loop(2);
    engine.scenes().pop();
    std::printf("asked pop()\n");
    loop(3);
    engine.scenes().change<Logged>("game");
    std::printf("asked change(game)\n");
    loop(4);
    std::printf("the menu's handle now: alive=%s, send=%s\n", g_menu.alive() ? "yes" : "no", g_menu.send(Note{"late"}) ? "posted" : "refused");
}
```

What it prints (`./build/example/lab05_scenes`):

```text
asked change(menu): the stack is still empty (0 scene)
loop 1
    menu onEnter
    menu ticks (tick 0 of the loop)
    menu asked push(pause), and goes on: nothing changes during a loop
    menu onPause
    pause onEnter
send "hello" to the menu, which is paused: posted
loop 2
    pause ticks (tick 1 of the loop)
asked pop()
loop 3
    pause onExit
    menu onResume
    menu got "hello" (at the start of its loop, before its ticks)
    menu ticks (tick 2 of the loop)
asked change(game)
loop 4
    menu onExit
    game onEnter
    game ticks (tick 3 of the loop)
the menu's handle now: alive=no, send=refused
    game onExit
```

## Reading the output

- After `change(menu)` the stack is still empty: the scene does not even exist yet.
- **Loop 1** enters the menu at its start, and the menu ticks. During that tick it asks for `push(pause)`, and the
  tick goes on. The push is done at the **end** of the loop: the menu gets `onPause`, then the pause is entered.
- The message sent to the paused menu is **posted**: it waits in the mailbox.
- **Loop 2**: only the pause runs. It shows tick 1, because the counter belongs to the loop.
- **Loop 3** starts by doing the `pop()`: the pause is left and the menu resumes. The menu then drains its mailbox
  and gets "hello" **before** its tick.
- **Loop 4**: `change(game)` leaves the menu (its mailbox closes) and enters the game.
- The menu's old handle now says `alive=no`, and sending to it is refused.
- The last `game onExit` comes from the engine's destructor, when `main` returns: the scenes are always left.

Next: [Step 6: Modules](../06-modules/README.md).
