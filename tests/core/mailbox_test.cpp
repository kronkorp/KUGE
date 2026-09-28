extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Engine.hpp"
#include "Mailbox.hpp"
#include "SceneHandle.hpp"
#include "Stage.hpp"
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    struct Hello { std::string who; };
    struct Number { int value; };

    struct Tick : kw::ISystem
    {
        std::function<void(void)> fn;
        explicit Tick(std::function<void(void)> f) : fn(std::move(f)) {}
        bool handle(kw::World&) override { fn(); return true; }
    };

    // A scene that says what happens to it
    class Probe : public kuge::Scene
    {
        public:
            explicit Probe(std::vector<std::string>* log, std::string name = "p") : m_log(*log), m_name(std::move(name)) {}

            using kuge::Scene::ctx;

            void onEnter(void) override
            {
                m_log.push_back(m_name + ":enter");
                addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Tick>([this] { m_log.push_back(m_name + ":tick"); }));
            }
            void onExit(void) override { m_log.push_back(m_name + ":exit"); }
            void onMessage(const kuge::Message& message) override
            {
                if (const auto* number = message.as<Number>()) {
                    m_log.push_back(m_name + ":number " + std::to_string(number->value));
                } else if (const auto* hello = message.as<Hello>()) {
                    m_log.push_back(m_name + ":hello " + hello->who);
                }
            }

        private:
            std::vector<std::string>& m_log;
            std::string               m_name;
    };

    std::string joined(const std::vector<std::string>& log)
    {
        std::string all;

        for (const auto& line : log) {
            all += (all.empty() ? "" : ",") + line;
        }
        return all;
    }
}

Test(message, holds_its_type)
{
    kuge::Message text(Hello{"Ana"});
    kuge::Message number(Number{7});

    Assert(text.is<Hello>() && !text.is<Number>(), "it knows its type");
    Assert(text.as<Number>() == nullptr, "another type: nullptr");
    Assert(text.as<int>() == nullptr, "even a type that is not a message");
    AssertStrEq(text.as<Hello>()->who.c_str(), "Ana", "the value");
    AssertEq(number.as<Number>()->value, 7, "another one");
    kuge::Message plain(42);
    Assert(plain.is<int>() && *plain.as<int>() == 42, "any type, an int too");
}

Test(message, can_be_moved_only)
{
    kuge::Message message(std::make_unique<int>(5));

    Assert(message.is<std::unique_ptr<int>>(), "a type that cannot be copied");
    std::unique_ptr<int> taken = std::move(*message.as<std::unique_ptr<int>>());
    AssertEq(*taken, 5, "and it can be taken");
    kuge::Message moved(std::move(message));
    Assert(moved.is<std::unique_ptr<int>>(), "the message can be moved");
}

Test(mailbox, in_order_then_empty)
{
    kuge::Mailbox box;

    for (int i = 0; i < 5; ++i) {
        Assert(box.post(kuge::Message(Number{i})), "accepted");
    }
    AssertEq(box.size(), 5, "5 waiting");
    auto taken = box.drain();

    AssertEq(taken.size(), 5, "5 taken");
    for (int i = 0; i < 5; ++i) {
        AssertEq(taken[static_cast<std::size_t>(i)].as<Number>()->value, i, "in the order they were sent");
    }
    AssertEq(box.size(), 0, "and nothing is left");
    AssertEq(box.drain().size(), 0, "nothing more");
}

Test(mailbox, full_refuses)
{
    kuge::Mailbox box(3);

    for (int i = 0; i < 3; ++i) {
        Assert(box.post(kuge::Message(Number{i})), "room for 3");
    }
    Assert(!box.post(kuge::Message(Number{3})), "the 4th is refused");
    Assert(!box.post(kuge::Message(Number{4})), "and the 5th");
    AssertEq(box.dropped(), 2, "2 were lost, and counted");
    AssertEq(box.size(), 3, "3 kept");
    box.drain();
    Assert(box.post(kuge::Message(Number{5})), "room again once it is emptied");
}

Test(mailbox, closed_refuses)
{
    kuge::Mailbox box;

    box.post(kuge::Message(Number{1}));
    box.close();
    Assert(box.closed(), "closed");
    Assert(!box.post(kuge::Message(Number{2})), "nothing new");
    AssertEq(box.drain().size(), 1, "but what was there can be taken");
}

// Many threads post, one takes: nothing lost, and each sender's order kept
Test(mailbox, many_senders)
{
    constexpr int SENDERS = 8;
    constexpr int EACH    = 5000;
    kuge::Mailbox box(SENDERS * EACH);
    std::vector<std::thread> threads;

    for (int s = 0; s < SENDERS; ++s) {
        threads.emplace_back([&box, s] {
            for (int i = 0; i < EACH; ++i) {
                box.post(kuge::Message(std::make_pair(s, i)));
            }
        });
    }
    std::vector<int> next(SENDERS, 0);
    std::size_t received = 0;
    bool ordered = true;

    while (received < static_cast<std::size_t>(SENDERS * EACH)) {
        for (auto& message : box.drain()) {
            const auto* pair = message.as<std::pair<int, int>>();

            ordered = ordered && pair->second == next[static_cast<std::size_t>(pair->first)]++;
            ++received;
        }
    }
    for (auto& thread : threads) {
        thread.join();
    }
    Assert(ordered, "the messages of a sender arrive in order");
    AssertEq(box.dropped(), 0, "none lost");
}

Test(handle, empty_and_dead)
{
    kuge::SceneHandle empty;

    Assert(!empty.valid() && !empty.alive(), "an empty handle points to nothing");
    Assert(!empty.send(Number{1}) && !empty.stop(), "and sends nowhere");
    std::vector<std::string> log;
    kuge::SceneHandle handle;
    {
        kuge::Engine engine;

        engine.scenes().change<Probe>(&log);
        engine.step(0.0);
        handle = static_cast<Probe*>(engine.scenes().top())->ctx().self();
        Assert(handle.valid() && handle.alive(), "alive while the scene is");
        Assert(handle.send(Number{1}), "it accepts");
    }
    Assert(handle.valid() && !handle.alive(), "still a handle, but the scene is gone");
    Assert(!handle.send(Number{2}), "it refuses");
}

Test(scene, messages_before_ticks)
{
    std::vector<std::string> log;
    kuge::Engine engine;

    engine.scenes().change<Probe>(&log);
    engine.step(0.0);
    auto handle = static_cast<Probe*>(engine.scenes().top())->ctx().self();

    log.clear();
    handle.send(Number{1});
    handle.send(Hello{"Ana"});
    handle.send(Number{2});
    engine.step(1.0 / 60.0);
    AssertStrEq(joined(log).c_str(), "p:number 1,p:hello Ana,p:number 2,p:tick", "in order, then the tick");
    log.clear();
    engine.step(1.0 / 60.0);
    AssertStrEq(joined(log).c_str(), "p:tick", "and only once");
}

Test(scene, a_scene_talks_to_itself)
{
    std::vector<std::string> log;
    kuge::Engine engine;

    engine.scenes().change<Probe>(&log);
    engine.step(0.0);
    auto* probe = static_cast<Probe*>(engine.scenes().top());

    log.clear();
    probe->ctx().self().send(Number{9});
    engine.step(0.0);
    AssertStrEq(joined(log).c_str(), "p:number 9", "even with no tick in the loop");
}

Test(scene, a_paused_scene_waits)
{
    std::vector<std::string> log;
    kuge::Engine engine;

    engine.scenes().change<Probe>(&log, "under");
    engine.step(0.0);
    auto under = static_cast<Probe*>(engine.scenes().top())->ctx().self();

    engine.scenes().push<Probe>(&log, "over");
    engine.step(0.0);
    log.clear();
    under.send(Number{1});
    engine.step(1.0 / 60.0);
    AssertStrEq(joined(log).c_str(), "over:tick", "the scene on top runs, the other does not hear");
    engine.scenes().pop();
    log.clear();
    engine.step(0.0);
    AssertStrEq(joined(log).c_str(), "over:exit,under:number 1", "it hears when it runs again");
}

Test(scene, stop_pops_it)
{
    std::vector<std::string> log;
    kuge::Engine engine;

    engine.scenes().change<Probe>(&log);
    engine.step(0.0);
    auto handle = static_cast<Probe*>(engine.scenes().top())->ctx().self();

    Assert(handle.stop(), "asked");
    AssertEq(engine.step(1.0 / 60.0), false, "it was the last scene: the engine has nothing left to run");
    Assert(log.back() == "p:exit" && !handle.alive(), "the scene is left, and the handle is dead");
}

Test(scene, no_parent_for_main_ones)
{
    std::vector<std::string> log;
    kuge::Engine engine;

    engine.scenes().change<Probe>(&log);
    engine.step(0.0);
    Assert(!static_cast<Probe*>(engine.scenes().top())->ctx().parent().valid(), "nobody spawned it");
}
