#pragma once

#include "Module.hpp"
#include <kronk3d/render/IRasterizer.hpp>
#include <memory>
#include <stdexcept>

namespace kuge
{

    //! The 3D renderer cannot be started
    class Render3DError : public std::runtime_error
    {
        public:
            using std::runtime_error::runtime_error;
    };

    struct Render3DConfig
    {
        k3::AntiAliasing antiAliasing = k3::AntiAliasing::None;
        float            renderScale = 1.0f;   //!< The scene is drawn at this fraction of the screen, then enlarged
        unsigned         threads = 0;          //!< Worker threads of the rasterizer (0: one per hardware thread)
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  What makes a client able to show a 3D scene: kronk3d's software rasterizer
     *
     * Add it after the ClientModule (it is destroyed before it):
     *
     *     auto& client = engine.addModule<kuge::ClientModule>(kuge::makeSdlBackend({}));
     *     engine.addModule<kuge::Render3DModule>();
     *
     * It draws in memory, the image is then given to the 2D renderer: it works with any backend,
     * with no OpenGL context, and the sprites and the interface can be drawn over it. Each scene gets,
     * as resources of its World, Ref<k3::IRasterizer>, k3::Camera and k3::Environment. See Scene3D for
     * the systems that draw.
     */
    ////////////////////////////////////////////////////////////////////////////
    class Render3DModule : public Module
    {
        public:
            //! @throw Render3DError
            explicit Render3DModule(Render3DConfig config = {});

            k3::IRasterizer& rasterizer(void) noexcept { return *m_rasterizer; }

            void inject(kw::World& world) override;

        private:
            std::unique_ptr<k3::IRasterizer> m_rasterizer;
    };

}
