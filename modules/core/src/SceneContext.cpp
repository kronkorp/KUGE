#include "SceneContext.hpp"
#include "Engine.hpp"

kuge::Engine& kuge::SceneContext::engine(void) const noexcept
{
    return *m_engine;
}

kuge::SceneManager& kuge::SceneContext::scenes(void) const noexcept
{
    return m_engine->scenes();
}

const kuge::Time& kuge::SceneContext::time(void) const noexcept
{
    return m_engine->time();
}
