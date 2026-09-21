#pragma once

#include "Scene.hpp"
#include "Stage.hpp"
#include "render/Systems.hpp"

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A scene that draws and reads the player's actions
     *
     * Call installClientSystems() first in onEnter(), so that they run before
     * the systems of the game in the same stage:
     *
     *     class Level : public kuge::ClientScene {
     *         void onEnter() override {
     *             installClientSystems();
     *             addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, ...);
     *         }
     *     };
     */
    ////////////////////////////////////////////////////////////////////////////
    class ClientScene : public Scene
    {
        protected:
            //! Fixed, Input: SampleInput then SnapshotTransforms.
            //! Frame, Render: SpriteRender.
            void installClientSystems(void)
            {
                addSystem(kw::Schedule::Fixed, stage::Input, std::make_unique<SampleInput>());
                addSystem(kw::Schedule::Fixed, stage::Input, std::make_unique<SnapshotTransforms>());
                addSystem(kw::Schedule::Frame, stage::Render, std::make_unique<SpriteRender>());
            }
    };

}
