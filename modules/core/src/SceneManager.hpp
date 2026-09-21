#pragma once

#include "Scene.hpp"
#include "SceneHandle.hpp"
#include "Time.hpp"
#include <cstddef>
#include <deque>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace kuge
{

    class Engine;

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The stack of scenes of an engine. Only the top one runs.
     *
     * change(), push() and pop() do not act at once: a scene is never destroyed
     * while it runs. The transitions are queued and done between two frames (see
     * apply()), in the order they were asked, including those asked by the
     * onEnter() of a scene that was just entered.
     */
    ////////////////////////////////////////////////////////////////////////////
    class SceneManager
    {
        public:
            //! @param time        The clock of the loop these scenes run in (scene ctx().time())
            //! @param parent      Who spawned the first scene (empty: nobody)
            //! @param mainThread  Do the scenes run on the main thread? Then the modules
            //!                    give them everything; if not, only what they share
            SceneManager(Engine& engine, const Time& time, SceneHandle parent = {}, bool mainThread = true) noexcept;
            ~SceneManager(void);

            SceneManager(const SceneManager&)            = delete;
            SceneManager& operator=(const SceneManager&) = delete;

            //! Leaves every scene (from the top down), then enters a T
            template<typename T, typename ...Args>
            void change(Args&&... args)
            {
                enqueue(Kind::Change, makeFactory<T>(std::forward<Args>(args)...));
            }

            //! Same as change(), with a scene that is already made
            void changeTo(std::unique_ptr<Scene> scene);

            //! Pauses the current scene and enters a T over it
            template<typename T, typename ...Args>
            void push(Args&&... args)
            {
                enqueue(Kind::Push, makeFactory<T>(std::forward<Args>(args)...));
            }

            //! Leaves the current scene and resumes the one under it. When the
            //! last one is popped, the engine has nothing left to run.
            void pop(void);

            //! Does the queued transitions
            void apply(void);

            //! Leaves every scene now, and forgets the queued transitions
            void clear(void);

            //! The scene that runs, or nullptr
            Scene*      top(void) noexcept;
            std::size_t size(void) const noexcept;
            bool        empty(void) const noexcept;

        private:
            enum class Kind { Change, Push, Pop };

            struct Factory
            {
                virtual ~Factory(void) = default;
                virtual std::unique_ptr<Scene> make(void) = 0;
            };

            // Keeps the arguments until the scene is made: they can be moved
            template<typename T, typename ...Args>
            struct FactoryOf final : Factory
            {
                explicit FactoryOf(Args&&... args) : m_args(std::forward<Args>(args)...) {}

                std::unique_ptr<Scene> make(void) override
                {
                    return std::apply([](auto&... args) -> std::unique_ptr<Scene> {
                        return std::make_unique<T>(std::move(args)...);
                    }, m_args);
                }

                std::tuple<std::decay_t<Args>...> m_args;
            };

            struct Op
            {
                Kind                     kind;
                std::unique_ptr<Factory> factory;
            };

            template<typename T, typename ...Args>
            static std::unique_ptr<Factory> makeFactory(Args&&... args)
            {
                static_assert(std::is_base_of_v<Scene, T>, "a scene must derive from kuge::Scene");
                return std::make_unique<FactoryOf<T, Args...>>(std::forward<Args>(args)...);
            }

            void enqueue(Kind kind, std::unique_ptr<Factory> factory);
            void enter(std::unique_ptr<Scene> scene);
            void leaveTop(void);

            //! A scene that was made before
            struct ReadyFactory final : Factory
            {
                explicit ReadyFactory(std::unique_ptr<Scene> scene) : m_scene(std::move(scene)) {}
                std::unique_ptr<Scene> make(void) override { return std::move(m_scene); }

                std::unique_ptr<Scene> m_scene;
            };

            Engine&                             m_engine;
            const Time&                         m_time;
            SceneHandle                         m_parent;
            bool                                m_mainThread;
            std::vector<std::unique_ptr<Scene>> m_stack;
            std::deque<Op>                      m_pending;
    };

}
