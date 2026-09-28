#include "SceneContext.hpp"
#include "Engine.hpp"

kuge::Engine& kuge::SceneContext::engine(void) const noexcept
{
    return *m_engine;
}

kuge::SceneManager& kuge::SceneContext::scenes(void) const noexcept
{
    return *m_scenes;
}

const kuge::Time& kuge::SceneContext::time(void) const noexcept
{
    return *m_time;
}

kuge::SceneHandle kuge::SceneContext::spawnScene(RunPolicy policy, std::unique_ptr<Scene> scene) const
{
    return m_engine->spawnScene(policy, std::move(scene), m_self);
}
