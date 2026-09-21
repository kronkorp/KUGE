#pragma once

#include "SceneHandle.hpp"
#include "Time.hpp"
#include <memory>
#include <type_traits>
#include <utility>

namespace kuge
{

    class Engine;
    class Scene;
    class SceneManager;

    //! Where and how a spawned scene runs
    enum class RunPolicy {
        Main,        //!< On the main thread, in the loop of the engine, next to the main scenes
        Dedicated,   //!< On a thread of its own, with its own loop at the tick rate
        Pooled,      //!< Its ticks are given to the worker threads of the engine, when they are due
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  What a scene can reach around itself (Scene::ctx())
     *
     * Only available once the scene is entered: use it from onEnter() and after,
     * not from the constructor.
     *
     * scenes() and time() are those of the loop the scene runs in (the main one,
     * or the one of its own thread): use them only from the scene, never from
     * another thread. To reach another scene, use its SceneHandle.
     */
    ////////////////////////////////////////////////////////////////////////////
    class SceneContext
    {
        public:
            SceneContext(void) = default;
            SceneContext(Engine& engine, SceneManager& scenes, const Time& time, SceneHandle self, SceneHandle parent) noexcept
                : m_engine(&engine), m_scenes(&scenes), m_time(&time), m_self(std::move(self)), m_parent(std::move(parent)) {}

            Engine&        engine(void) const noexcept;
            SceneManager&  scenes(void) const noexcept;
            const Time&    time(void) const noexcept;

            //! This scene, to give to the ones that must talk to it
            const SceneHandle& self(void) const noexcept { return m_self; }

            //! The scene that spawned the one this is the root of (empty for the others)
            const SceneHandle& parent(void) const noexcept { return m_parent; }

            //! Starts a T (built with args, here) that runs as the policy says, with this
            //! scene as its parent
            //! @return  How to talk to it
            template<typename T, typename ...Args>
            SceneHandle spawn(RunPolicy policy, Args&&... args) const
            {
                static_assert(std::is_base_of_v<Scene, T>, "a scene must derive from kuge::Scene");
                return spawnScene(policy, std::make_unique<T>(std::forward<Args>(args)...));
            }

        private:
            SceneHandle spawnScene(RunPolicy policy, std::unique_ptr<Scene> scene) const;

            Engine*        m_engine = nullptr;
            SceneManager*  m_scenes = nullptr;
            const Time*    m_time   = nullptr;
            SceneHandle    m_self;
            SceneHandle    m_parent;
    };

}
