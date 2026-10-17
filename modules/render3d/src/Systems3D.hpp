#pragma once

#include "backend/IRenderer2D.hpp"
#include "kronkworld/Kronkworld.hpp"
#include "render/Texture.hpp"
#include <memory>

namespace kuge
{

    //! Fixed, stage Input: remembers where the entities that have a PreviousTransform3D were before
    //! the simulation moves them
    class SnapshotTransforms3D : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override;
    };

    //! Frame, stage Render: draws the entities that have a Transform3D and a Model3D, seen by the
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
