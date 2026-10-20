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
    // The top scene, and the ones a Transparent scene leaves visible under it (bottom first: the top is drawn last)
    const std::vector<Scene*> layers = m_scenes.running();

    for (std::size_t i = 0; i < layers.size(); ++i) {
        layers[i]->m_frozen = i + 1 < layers.size();
    }
    // Only the top one reads its messages: a stop asked of the scene under would pop the one over it. It gets
    // them when it is on top again, like a paused scene.
    layers.back()->deliverMessages();
    const std::uint32_t ticks = m_timestep.advance(frameSeconds);

    // NOTE: The transitions asked while ticking wait for the end of the loop,
    // so these scenes stay as they are until then.
    for (std::uint32_t i = 0; i < ticks && !stop; ++i) {
        m_time.tick = m_ticksRun;
        for (Scene* scene : layers) {
            scene->fixedTick(m_time);
        }
        ++m_ticksRun;
    }
    m_time.tick = m_ticksRun;
    m_time.alpha = m_timestep.alpha();
    m_time.frameDt = frameSeconds;
    for (Scene* scene : layers) {
        scene->frame(m_time);
    }
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
