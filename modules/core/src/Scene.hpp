#pragma once

#include "Message.hpp"
#include "SceneContext.hpp"
#include "Time.hpp"
#include "kronkworld/Kronkworld.hpp"
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <vector>

namespace kuge
{

    class Engine;
    class SceneManager;
    class Scene;

    //! What a scene pushed over another one does to it (see Scene::overlay())
    enum class OverlayMode : std::uint8_t {
        Opaque,        //!< The scene under is paused: it does not run at all
        Transparent,   //!< The scene under is still drawn and still hears the network, but its game logic stands still
    };

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
            virtual ~Scene(void);

            Scene(const Scene&)            = delete;
            Scene& operator=(const Scene&) = delete;

            //! The scene becomes the active one. Create the systems and entities here.
            virtual void onEnter(void) {}

            //! The scene is left for good. Its systems are removed right after.
            virtual void onExit(void) {}

            //! Another scene was pushed over this one: it stops running. (Not called when that scene is
            //! Transparent: this one keeps running, see overlay().)
            virtual void onPause(void) {}

            //! The scene over this one was popped: it runs again.
            virtual void onResume(void) {}

            //! What this scene does to the one under it when it is pushed over it.
            //!
            //! Opaque (the default): the one under is paused, and nothing of it runs.
            //!
            //! Transparent, for a pause menu, a level-up choice, a warning drawn over the game: the one under keeps
            //! its **frame** (it is drawn first, so this one is drawn over it) and the systems of the
            //! Network and Replication stages (its messages wait until it is on top again, as when it is paused), so the picture stays alive and the snapshots are still read. Its
            //! Input, Simulation, Physics and Late stages stand still, whatever the schedule is, so no keyboard
            //! focus or game rule reaches it. Each scene has its own World (and its own UI): feed the overlay with
            //! SceneHandle messages.
            virtual OverlayMode overlay(void) const { return OverlayMode::Opaque; }

            //! A message that another scene sent (see SceneHandle). Called at the start of each
            //! loop of the scene, before its ticks, in the order they were sent. A scene that is
            //! paused (another is over it) gets them when it runs again.
            virtual void onMessage(const Message&) {}

        protected:
            Scene(void);

            kw::World&     world(void) noexcept;
            SceneContext&  ctx(void) noexcept;

            //! Adds a system, and remembers it to remove it when the scene is left.
            //! Fixed: at the fixed rate, Frame: once per frame (see kuge::stage).
            //! A system that throws ends the pass (the systems after it do not run) and the
            //! exception comes out of the tick or the frame of the scene.
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
            friend class SceneLoop;

            void attach(const SceneContext& context, bool mainThread);
            void deliverMessages(void);
            void fixedTick(const Time& time);
            void frame(const Time& time);
            void shutdown(void);
            void rethrowError(void);
            SceneHandle handle(void) const { return SceneHandle(m_mailbox); }

            std::shared_ptr<Mailbox>      m_mailbox;   // shared: a SceneHandle keeps it after the scene
            std::unique_ptr<kw::World>    m_world;
            SceneContext                  m_ctx;
            std::vector<kw::SystemHandle> m_systems;
            std::exception_ptr            m_error;     // what a system threw: see addSystem()
            bool                          m_paused = false;   // onPause() was called, onResume() is due
            bool                          m_frozen = false;   // under a Transparent scene: see overlay()
    };

}
