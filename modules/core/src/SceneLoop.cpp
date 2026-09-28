#include "SceneLoop.hpp"

kuge::SceneLoop::SceneLoop(Engine& engine, std::uint32_t tickRate, std::uint32_t maxCatchUp, SceneHandle parent, bool mainThread)
    : m_timestep(tickRate, maxCatchUp),
      m_scenes(engine, m_time, std::move(parent), mainThread)
{
    m_time.tickRate = tickRate;
    m_time.dt = m_timestep.dt();
}

bool kuge::SceneLoop::ready(void)
{
    m_scenes.apply();
    return m_scenes.top() != nullptr;
}

void kuge::SceneLoop::run(double frameSeconds, const std::atomic<bool>& stop)
{
    Scene* scene = m_scenes.top();

    scene->deliverMessages();
    const std::uint32_t ticks = m_timestep.advance(frameSeconds);

    // NOTE: The transitions asked while ticking wait for the end of the loop,
    // so this scene stays the top one until then.
    for (std::uint32_t i = 0; i < ticks && !stop; ++i) {
        m_time.tick = m_ticksRun;
        scene->fixedTick(m_time);
        ++m_ticksRun;
    }
    m_time.tick = m_ticksRun;
    m_time.alpha = m_timestep.alpha();
    m_time.frameDt = frameSeconds;
    scene->frame(m_time);
}

void kuge::SceneLoop::settle(void)
{
    m_scenes.apply();
}

bool kuge::SceneLoop::step(double frameSeconds, const std::atomic<bool>& stop)
{
    if (!ready() || stop) {
        return false;
    }
    run(frameSeconds, stop);
    settle();
    return !stop && !m_scenes.empty();
}
