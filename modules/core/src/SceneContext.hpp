#pragma once

#include "Time.hpp"

namespace kuge
{

    class Engine;
    class SceneManager;

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  What a scene can reach around itself (Scene::ctx())
     *
     * Only available once the scene is entered: use it from onEnter() and after,
     * not from the constructor.
     */
    ////////////////////////////////////////////////////////////////////////////
    class SceneContext
    {
        public:
            SceneContext(void) = default;
            explicit SceneContext(Engine& engine) noexcept : m_engine(&engine) {}

            Engine&        engine(void) const noexcept;
            SceneManager&  scenes(void) const noexcept;
            const Time&    time(void) const noexcept;

        private:
            Engine* m_engine = nullptr;
    };

}
