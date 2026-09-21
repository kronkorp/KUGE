#include "Engine.hpp"
#include "Logger.hpp"
#include "LoggerLevel.hpp"
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
      m_timestep(config.tickRate, config.maxCatchUp),
      m_scenes(*this)
{
    if (config.maxFps != 0 && config.maxFps < config.tickRate) {
        throw std::invalid_argument("Engine: maxFps must be 0 or at least tickRate");
    }
    m_time.tickRate = config.tickRate;
    m_time.dt = m_timestep.dt();
    Logger::logger().info("Engine constructed");
}

kuge::Engine::~Engine(void)
{
    m_scenes.clear();
    // The last one added may use the ones before it
    while (!m_modules.empty()) {
        m_modules.pop_back();
    }
    Logger::logger().info("Engine destroyed");
}

kuge::SceneManager& kuge::Engine::scenes(void) noexcept
{
    return m_scenes;
}

const kuge::Time& kuge::Engine::time(void) const noexcept
{
    return m_time;
}

const kuge::Engine::Config& kuge::Engine::config(void) const noexcept
{
    return m_config;
}

void kuge::Engine::inject(kw::World& world)
{
    for (auto& module : m_modules) {
        module->inject(world);
    }
}

void kuge::Engine::stop(void) noexcept
{
    m_stop = true;
}

bool kuge::Engine::step(double frameSeconds)
{
    m_scenes.apply();
    Scene* scene = m_scenes.top();

    if (!scene || m_stop) {
        return false;
    }
    for (auto& module : m_modules) {
        module->beginFrame(*this);
    }
    if (m_stop) {
        return false;   // e.g. the window was closed: no need to simulate more
    }
    const std::uint32_t ticks = m_timestep.advance(frameSeconds);

    // NOTE: The transitions asked while ticking wait for the end of the loop,
    // so this scene stays the top one until then.
    for (std::uint32_t i = 0; i < ticks && !m_stop; ++i) {
        m_time.tick = m_ticksRun;
        scene->fixedTick(m_time);
        ++m_ticksRun;
    }
    m_time.tick = m_ticksRun;
    m_time.alpha = m_timestep.alpha();
    m_time.frameDt = frameSeconds;
    scene->frame(m_time);
    for (auto& module : m_modules) {
        module->endFrame(*this);
    }
    m_scenes.apply();
    return !m_stop && !m_scenes.empty();
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
            ? m_timestep.untilNextTick()
            : 1.0 / m_config.maxFps - std::chrono::duration<double>(Clock::now() - now).count();

        if (wait > 0.0) {
            std::this_thread::sleep_for(std::chrono::duration<double>(wait));
        }
    }
    m_scenes.clear();
    // NOTE: Reset here and not at the start: a stop() that comes from another
    // thread just before run() begins must not be lost.
    m_stop = false;
    return 0;
}
