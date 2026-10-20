#pragma once

#include "ClientScene.hpp"
#include "cinematic/Cinematic.hpp"
#include "ui/Font.hpp"
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A cinematic: a scene that plays a Timeline over a stage that the game builds, and hands back when it ends or is skipped
     *
     *     kuge::CinematicScene::Config config;
     *
     *     config.timeline = std::make_shared<const kuge::Timeline>(kuge::Timeline::load("data/timelines/boss_entrance.json"));
     *     config.font = font;
     *     config.setup = [&](kuge::CinematicScene& scene) {                  // the stage: entities, with the names the file uses
     *         const kw::Entity boss = scene.stage().create();
     *         scene.stage().add<kuge::Transform2D>(boss, {});
     *         scene.stage().add<kuge::Sprite>(boss, ...);
     *         scene.name("boss", boss);
     *     };
     *     config.onEvent = [](const std::string& name, const kuge::Json& key) { ... };   // "event" keys
     *     config.onDone = [](kuge::SceneContext& ctx, bool skipped) { ctx.scenes().change<Level>(...); };
     *     ctx().scenes().change<kuge::CinematicScene>(config);
     *
     * Escape (and the `skipAction` of the game) skips it: `onDone` is called once, with the world as if it had played to the end. A scene of
     * its own, never a mode of another (a pause under it would be a Transparent overlay). The same timeline can also run inside a scene:
     * make a Cinematic (cinematic/Cinematic.hpp) and call update(dt) from a system.
     *
     * In multiplayer a cinematic that changes nothing of the game runs on the client alone. One that stops the game is started by the
     * server (RoomScene::playCinematic()): the client pushes this scene when it hears CinematicStart and sends CinematicDone from onDone.
     */
    ////////////////////////////////////////////////////////////////////////////
    class CinematicScene : public ClientScene
    {
        public:
            struct Config
            {
                std::shared_ptr<const Timeline>          timeline;
                std::shared_ptr<const ComponentRegistry> registry;   //!< None: the core's and the client's components (describeCore, describeClient)
                std::shared_ptr<const IFont>             font;       //!< For the subtitles; none: they are not drawn
                std::function<void(CinematicScene&)>     setup;      //!< Builds the stage in onEnter()
                std::function<void(const std::string& name, const Json& key)> onEvent;
                //! Once, when the cinematic ends (skipped: true if the player cut it short). Leave the scene from here (ctx.scenes().change<...>()).
                std::function<void(SceneContext& ctx, bool skipped)> onDone;
                int                                      skipAction = -1;   //!< An action of the game (its number) that skips it, besides Escape
            };

            explicit CinematicScene(Config config);

            void onEnter(void) override;

            //! The world of the stage, for `setup`
            kw::World& stage(void) noexcept { return world(); }
            //! Gives a name to an entity of the stage, for the `target` of the keys
            void name(const std::string& name, kw::Entity entity) { m_names[name] = entity; }

            Cinematic& cinematic(void) noexcept { return *m_cinematic; }

        private:
            class Step;
            class Overlay;

            Config                      m_config;
            std::map<std::string, kw::Entity> m_names;
            std::unique_ptr<Cinematic>  m_cinematic;
            bool                        m_done = false;
    };

}
