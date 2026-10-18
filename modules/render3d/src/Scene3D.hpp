#pragma once

#include "ClientScene.hpp"
#include "Stage.hpp"
#include "Systems3D.hpp"

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A ClientScene that also shows 3D: call installClient3DSystems() instead of
     *         installClientSystems(), first in onEnter()
     *
     * Fixed, Input: SnapshotTransforms3D (and the 2D ones). Fixed, Late: PlayReplicatedClips then AnimateModels, after the
     * sprites' animations. Frame, Render: UpdateCamera, ModelRender, then the sprites, so the 2D is over the 3D.
     */
    ////////////////////////////////////////////////////////////////////////////
    class Scene3D : public ClientScene
    {
        protected:
            void installClient3DSystems(void)
            {
                addSystem(kw::Schedule::Fixed, stage::Input, std::make_unique<SnapshotTransforms3D>());
                addSystem(kw::Schedule::Frame, stage::Render, std::make_unique<UpdateCamera>());
                addSystem(kw::Schedule::Frame, stage::Render, std::make_unique<ModelRender>());
                installClientSystems();
                // After AnimateSprites, which clears AnimationEvents
                addSystem(kw::Schedule::Fixed, stage::Late, std::make_unique<PlayReplicatedClips>());
                addSystem(kw::Schedule::Fixed, stage::Late, std::make_unique<AnimateModels>());
            }
    };

}
