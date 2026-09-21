#pragma once

#include "SceneContext.hpp"
#include "Time.hpp"
#include "kronkworld/Kronkworld.hpp"
#include <cstddef>
#include <memory>
#include <vector>

namespace kuge
{

    class Engine;
    class SceneManager;
    class Scene;

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  What a function that installs something in a scene may use: its
     *         World, its context, and adding systems (see Scene::setup())
     *
     *     void installPhysics(kuge::SceneSetup scene, PhysicsConfig config = {});
     */
    ////////////////////////////////////////////////////////////////////////////
    class SceneSetup
    {
        public:
            kw::World&    world(void) noexcept;
            SceneContext& ctx(void) noexcept;

            //! Same as Scene::addSystem()
            kw::SystemHandle addSystem(
                kw::Schedule                 schedule,
                kw::StageId                  stage,
                std::unique_ptr<kw::ISystem> system,
                std::size_t                  delay    = 1,
                std::size_t                  interval = 1
            );

        private:
            friend class Scene;

            explicit SceneSetup(Scene& scene) noexcept : m_scene(scene) {}

            Scene& m_scene;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A part of a game: a menu, a level, a lobby, a room...
     *
     * A scene owns its own kw::World and says what it does (its systems), never
     * where or when it runs: the engine calls it. Nothing is shared between two
     * scenes, whatever thread they run on.
     *
     * Derive from it, add the systems in onEnter(), and give the scene to the
     * engine with engine.scenes().change<MyScene>(args...).
     */
    ////////////////////////////////////////////////////////////////////////////
    class Scene
    {
        public:
            virtual ~Scene(void) = default;

            Scene(const Scene&)            = delete;
            Scene& operator=(const Scene&) = delete;

            //! The scene becomes the active one. Create the systems and entities here.
            virtual void onEnter(void) {}

            //! The scene is left for good. Its systems are removed right after.
            virtual void onExit(void) {}

            //! Another scene was pushed over this one: it stops running.
            virtual void onPause(void) {}

            //! The scene over this one was popped: it runs again.
            virtual void onResume(void) {}

        protected:
            Scene(void);

            kw::World&     world(void) noexcept;
            SceneContext&  ctx(void) noexcept;

            //! Adds a system, and remembers it to remove it when the scene is left.
            //! Fixed: at the fixed rate, Frame: once per frame (see kuge::stage).
            kw::SystemHandle addSystem(
                kw::Schedule             schedule,
                kw::StageId              stage,
                std::unique_ptr<kw::ISystem> system,
                std::size_t              delay    = 1,
                std::size_t              interval = 1
            );

            bool removeSystem(const kw::SystemHandle& handle);

            //! Lets a function install systems and resources in this scene
            SceneSetup setup(void) noexcept { return SceneSetup(*this); }

        private:
            friend class Engine;
            friend class SceneManager;
            friend class SceneSetup;

            void attach(const SceneContext& context);
            void fixedTick(const Time& time);
            void frame(const Time& time);
            void shutdown(void);

            std::unique_ptr<kw::World>    m_world;
            SceneContext                  m_ctx;
            std::vector<kw::SystemHandle> m_systems;
    };

}
