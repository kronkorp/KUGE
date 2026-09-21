#include "Scene.hpp"
#include "Engine.hpp"

kuge::Scene::Scene(void) : m_world(std::make_unique<kw::World>())
{
    m_world->addResource<Time>();
}

kw::World& kuge::Scene::world(void) noexcept
{
    return *m_world;
}

kuge::SceneContext& kuge::Scene::ctx(void) noexcept
{
    return m_ctx;
}

kw::SystemHandle kuge::Scene::addSystem(
    kw::Schedule schedule,
    kw::StageId stage,
    std::unique_ptr<kw::ISystem> system,
    std::size_t delay,
    std::size_t interval
)
{
    auto handle = m_world->scheduleSystem(schedule, stage, std::move(system), delay, interval);

    m_systems.push_back(handle);
    return handle;
}

bool kuge::Scene::removeSystem(const kw::SystemHandle& handle)
{
    return m_world->removeSystem(handle);
}

void kuge::Scene::attach(const SceneContext& context)
{
    m_ctx = context;
    m_ctx.engine().inject(*m_world);
}

void kuge::Scene::fixedTick(const Time& time)
{
    m_world->getResource<Time>() = time;
    m_world->runOnce(kw::Schedule::Fixed);
}

void kuge::Scene::frame(const Time& time)
{
    m_world->getResource<Time>() = time;
    m_world->runOnce(kw::Schedule::Frame);
}

// Removing the systems while the resources are still there lets a system
// release what it holds before the World goes away
void kuge::Scene::shutdown(void)
{
    for (auto it = m_systems.rbegin(); it != m_systems.rend(); ++it) {
        m_world->removeSystem(*it);
    }
    m_systems.clear();
}
