#pragma once

#include "backend/IRenderer2D.hpp"
#include "kronkworld/Kronkworld.hpp"
#include "render/Texture.hpp"
#include <memory>
#include <vector>

namespace kuge
{

    //! Fixed, stage Input: remembers where the entities that have a PreviousTransform3D were before
    //! the simulation moves them
    class SnapshotTransforms3D : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override;
    };

    //! Fixed, stage Late (after AnimateSprites, which clears the events): moves the Animator3D forward by
    //! one tick, plays the clip that follows one that ended, and adds to AnimationEvents the cues that
    //! were crossed. Entities are handled by increasing number, so that the events always come in the
    //! same order. An Animator3D made before a glTF model was reloaded starts again from the new one,
    //! at the beginning of the clip that played.
    class AnimateModels : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override;

        private:
            std::vector<kw::Entity> m_entities;   //!< Kept between ticks
    };

    //! Frame, stage Render: draws the entities that have a Transform3D and a Model3D or an Animator3D, seen by the
    //! k3::Camera and lit by the k3::Environment and the Light3D, with kronk3d's rasterizer, then
    //! shows the image on the whole screen. Put it before the systems that draw in 2D (sprites, interface):
    //! what they draw is over the 3D scene.
    class ModelRender : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override;

        private:
            std::shared_ptr<Texture> m_frame;   //!< The image of the rasterizer, as the 2D renderer holds it
    };

}
