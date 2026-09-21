#include "Engine.hpp"
#include "Logger.hpp"
#include "LoggerLevel.hpp"
#include "TickDriver.hpp"
#include <chrono>
#include <csignal>
#include <stdexcept>
#include <thread>

namespace
{
    volatile std::sig_atomic_t g_signalled = 0;

    extern "C" void onSignal(int)
    {
        g_signalled = 1;
    }

    // SIGINT and SIGTERM end run() instead of killing the process, so that the
    // scenes are left properly. The previous handlers come back afterwards.
    class SignalGuard
    {
        public:
            SignalGuard(void)
            {
                struct sigaction action = {};

                action.sa_handler = &onSignal;
                sigemptyset(&action.sa_mask);
                g_signalled = 0;
                sigaction(SIGINT, &action, &m_oldInt);
                sigaction(SIGTERM, &action, &m_oldTerm);
            }

            ~SignalGuard(void)
            {
                sigaction(SIGINT, &m_oldInt, nullptr);
                sigaction(SIGTERM, &m_oldTerm, nullptr);
            }

            SignalGuard(const SignalGuard&)            = delete;
            SignalGuard& operator=(const SignalGuard&) = delete;

        private:
            struct sigaction m_oldInt  = {};
            struct sigaction m_oldTerm = {};
    };
}

kuge::Engine::Engine(void) : Engine(Config{})
{
}

kuge::Engine::Engine(Config config)
    : m_config(config),
      m_main(*this, config.tickRate, config.maxCatchUp, SceneHandle(), true)
{
    if (config.maxFps != 0 && config.maxFps < config.tickRate) {
        throw std::invalid_argument("Engine: maxFps must be 0 or at least tickRate");
    }
    Logger::logger().info("Engine constructed");
}

kuge::Engine::~Engine(void)
{
    stopSpawned();
    m_main.finish();
    // The last one added may use the ones before it
    while (!m_modules.empty()) {
        m_modules.pop_back();
    }
    Logger::logger().info("Engine destroyed");
}

kuge::SceneManager& kuge::Engine::scenes(void) noexcept
{
    return m_main.scenes();
}

const kuge::Time& kuge::Engine::time(void) const noexcept
{
    return m_main.time();
}

const kuge::Engine::Config& kuge::Engine::config(void) const noexcept
{
    return m_config;
}

void kuge::Engine::inject(kw::World& world, bool mainThread)
{
    for (auto& module : m_modules) {
        if (mainThread || module->sharedAcrossThreads()) {
            module->inject(world);
        }
    }
}

void kuge::Engine::stop(void) noexcept
{
    m_stop = true;
}

kuge::ThreadPool& kuge::Engine::pool(void)
{
    std::lock_guard lock(m_spawnMutex);

    if (!m_pool) {
        m_pool = std::make_unique<ThreadPool>(m_config.workers);
    }
    return *m_pool;
}

// Under m_spawnMutex
kuge::TickDriver& kuge::Engine::driver(void)
{
    if (!m_driver) {
        if (!m_pool) {
            m_pool = std::make_unique<ThreadPool>(m_config.workers);
        }
        m_driver = std::make_unique<TickDriver>(*m_pool);
    }
    return *m_driver;
}

std::size_t kuge::Engine::spawned(void)
{
    std::lock_guard lock(m_spawnMutex);
    std::size_t count = m_sideAdded.size() + m_side.size();

    for (const auto& runner : m_dedicated) {
        count += runner->finished ? 0 : 1;
    }
    return count + (m_driver ? m_driver->active() : 0);
}

kuge::SceneHandle kuge::Engine::spawnScene(RunPolicy policy, std::unique_ptr<Scene> scene, SceneHandle parent)
{
    const SceneHandle handle = scene->handle();

    if (m_stopSpawned) {
        return handle;   // the engine is ending: the scene never runs, and its mailbox closes with it
    }
    auto loop = std::make_unique<SceneLoop>(*this, m_config.tickRate, m_config.maxCatchUp, std::move(parent), policy == RunPolicy::Main);

    loop->scenes().changeTo(std::move(scene));
    reap();
    std::lock_guard lock(m_spawnMutex);

    switch (policy) {
        case RunPolicy::Main:
            m_sideAdded.push_back(std::move(loop));
            break;
        case RunPolicy::Dedicated: {
            auto runner = std::make_unique<Dedicated>();
            Dedicated& ref = *runner;

            runner->loop = std::move(loop);
            m_dedicated.push_back(std::move(runner));
            ref.thread = std::thread([this, &ref] { runDedicated(ref); });
            break;
        }
        case RunPolicy::Pooled:
            driver().add(std::move(loop));
            break;
    }
    return handle;
}

// The thread of a dedicated scene: the same loop as run(), on its own
void kuge::Engine::runDedicated(Dedicated& runner)
{
    using Clock = std::chrono::steady_clock;
    auto last = Clock::now();

    try {
        while (true) {
            const auto now = Clock::now();
            const double frame = std::chrono::duration<double>(now - last).count();

            last = now;
            if (!runner.loop->step(frame, m_stopSpawned)) {
                break;
            }
            if (m_wake.waitFor(runner.loop->untilNextTick())) {
                break;
            }
        }
    } catch (const std::exception& e) {
        Logger::logger().error("A scene threw and is stopped: {}", e.what());
    } catch (...) {
        Logger::logger().error("A scene threw and is stopped");
    }
    try {
        runner.loop->finish();   // left by the thread that ran it
    } catch (...) {
        Logger::logger().error("A scene threw while being left");
    }
    runner.finished = true;
}

// Joins the threads that are over
void kuge::Engine::reap(void)
{
    std::vector<std::unique_ptr<Dedicated>> over;

    {
        std::lock_guard lock(m_spawnMutex);

        for (auto it = m_dedicated.begin(); it != m_dedicated.end();) {
            if ((*it)->finished) {
                over.push_back(std::move(*it));
                it = m_dedicated.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& runner : over) {
        if (runner->thread.joinable() && runner->thread.get_id() != std::this_thread::get_id()) {
            runner->thread.join();
        }
    }
}

// Every spawned scene is left, on the thread that ran it, and its thread is over
void kuge::Engine::stopSpawned(void)
{
    std::vector<std::unique_ptr<Dedicated>> dedicated;
    std::vector<std::unique_ptr<SceneLoop>> side;
    std::unique_ptr<TickDriver> driver;

    m_stopSpawned = true;
    m_wake.request();
    {
        // Taken out under the lock, and joined without it: a scene that spawns
        // in the meantime must not wait for a thread that waits for the lock
        std::lock_guard lock(m_spawnMutex);

        dedicated = std::move(m_dedicated);
        m_dedicated.clear();
        side = std::move(m_side);
        m_side.clear();
        for (auto& added : m_sideAdded) {
            side.push_back(std::move(added));
        }
        m_sideAdded.clear();
        driver = std::move(m_driver);
    }
    for (auto& runner : dedicated) {
        if (runner->thread.joinable()) {
            runner->thread.join();
        }
    }
    driver.reset();   // leaves its scenes on the pool, then ends its thread
    for (auto& loop : side) {
        loop->finish();
    }
    m_stopSpawned = false;
    m_wake.reset();
}

void kuge::Engine::stepSideLoops(double frameSeconds)
{
    {
        std::lock_guard lock(m_spawnMutex);

        for (auto& added : m_sideAdded) {
            m_side.push_back(std::move(added));
        }
        m_sideAdded.clear();
    }
    // (A scene of these can spawn another: it waits in m_sideAdded for the next loop)
    for (std::size_t i = 0; i < m_side.size();) {
        SceneLoop& loop = *m_side[i];

        if (loop.step(frameSeconds, m_stop)) {
            ++i;
        } else {
            loop.finish();
            m_side.erase(m_side.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
}

bool kuge::Engine::step(double frameSeconds)
{
    if (!m_main.ready() || m_stop) {
        return false;
    }
    for (auto& module : m_modules) {
        module->beginFrame(*this);
    }
    if (m_stop) {
        return false;   // e.g. the window was closed: no need to simulate more
    }
    m_main.run(frameSeconds, m_stop);
    stepSideLoops(frameSeconds);
    for (auto& module : m_modules) {
        module->endFrame(*this);
    }
    m_main.settle();
    reap();
    return !m_stop && !m_main.scenes().empty();
}

int kuge::Engine::run(void)
{
    using Clock = std::chrono::steady_clock;
    SignalGuard signals;
    auto last = Clock::now();

    while (true) {
        const auto now = Clock::now();
        const double frame = std::chrono::duration<double>(now - last).count();

        last = now;
        if (g_signalled) {
            stop();
        }
        if (!step(frame)) {
            break;
        }
        // Nothing to do until the next tick (or the next frame, if they are
        // more frequent): don't burn a core waiting for it
        const double wait = m_config.maxFps == 0
            ? m_main.untilNextTick()
            : 1.0 / m_config.maxFps - std::chrono::duration<double>(Clock::now() - now).count();

        if (wait > 0.0) {
            std::this_thread::sleep_for(std::chrono::duration<double>(wait));
        }
    }
    stopSpawned();
    m_main.finish();
    // NOTE: Reset here and not at the start: a stop() that comes from another
    // thread just before run() begins must not be lost.
    m_stop = false;
    return 0;
}
