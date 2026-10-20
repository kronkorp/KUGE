#pragma once

#include "AssetManager.hpp"
#include "ClientModule.hpp"
#include "Components3D.hpp"
#include "Module.hpp"
#include <chrono>
#include <kronk3d/scene/Animation.hpp>
#include <kronk3d/scene/Model.hpp>
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

    //! A 3D model that cannot be read
    class ModelError : public std::runtime_error
    {
        public:
            using std::runtime_error::runtime_error;
    };

    //! What draws the 3D: kronk3d's rasterizer on the CPU or on the GPU (OpenGL 3.3)
    enum class Rendering {
        Auto,       //!< The GPU if the client has an OpenGL context (WindowConfig::openGL), else the CPU
        Software,   //!< The CPU: it works anywhere, with no window and no GPU (the tests, a server with a screen shot)
        OpenGL,     //!< The GPU: Render3DError if there is no context
    };

    struct Render3DConfig
    {
        Rendering        rendering = Rendering::Auto;
        k3::AntiAliasing antiAliasing = k3::AntiAliasing::None;
        float            renderScale = 1.0f;   //!< The scene is drawn at this fraction of the screen, then enlarged
        unsigned         threads = 0;          //!< Worker threads of the rasterizer (0: one per hardware thread)
        //! Hot reload: every this many seconds, the models whose file changed are reloaded in place
        //! (0: never; see reloadAssets()). For development.
        double           watchAssets = 0.0;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  What makes a client able to show a 3D scene: kronk3d's rasterizer, on the CPU or on the GPU
     *
     * Add it after the ClientModule (it is destroyed before it):
     *
     *     auto& client = engine.addModule<kuge::ClientModule>(kuge::makeSdlBackend({.openGL = true}));
     *     engine.addModule<kuge::Render3DModule>(client);        // the GPU if there is one, else the CPU
     *
     * Either way the scene is drawn off screen, and the image is then given to the 2D renderer: it works with
     * any backend, and the sprites, the text and the interface are drawn **over** it as for a 2D game. On the GPU
     * the image is read back (a few milliseconds at 1280x720), which is what keeps all of that working. Without
     * the client (the first constructor) it is the CPU. Each scene gets,
     * as resources of its World, Ref<k3::IRasterizer>, k3::Camera and k3::Environment, and the
     * models of files: Ref<AssetManager<k3::Model>> (.obj, with its .mtl and pictures) and
     * Ref<AssetManager<k3::AnimatedModel>> (.glb and .gltf). See Scene3D for the systems that draw.
     */
    ////////////////////////////////////////////////////////////////////////////
    class Render3DModule : public Module
    {
        public:
            //! On the CPU. @throw Render3DError (if the GPU was asked for)
            explicit Render3DModule(Render3DConfig config = {});

            //! On the GPU if the client has an OpenGL context and the config does not say otherwise.
            //! @throw Render3DError
            explicit Render3DModule(ClientModule& client, Render3DConfig config = {});

            ~Render3DModule(void) override;

            //! Is the GPU drawing? And what is drawing ("software", or the name of the graphics card)
            bool                usesOpenGL(void) const noexcept { return m_gl != nullptr; }
            const std::string&  rendererName(void) const noexcept { return m_rendererName; }

            k3::IRasterizer& rasterizer(void) noexcept { return *m_rasterizer; }

            //! Wavefront OBJ: load() throws ModelError, loadAsync() reads on a worker, and the model
            //! is made by the next loop. Reloaded in place when its file changes.
            AssetManager<k3::Model>& models(void) noexcept { return m_models; }

            //! glTF 2.0 (.glb, .gltf): the nodes, skins and clips an Animator3D plays. Reloaded in place
            //! too: the Animator3D that play it start again (see ModelRevision).
            AssetManager<k3::AnimatedModel>& animatedModels(void) noexcept { return m_animatedModels; }

            //! Reloads, in place, the models whose file changed since they were loaded, and logs
            //! the ones that could not be (they keep their old content). Done by itself each
            //! `watchAssets` seconds if the config asks.
            //! @return  How many were reloaded
            std::size_t reloadAssets(void);

            void onAttach(Engine& engine) override;
            void inject(kw::World& world) override;
            void beginFrame(Engine& engine) override;

        private:
            Render3DModule(ClientModule* client, Render3DConfig config);

            IOpenGL*                         m_gl = nullptr;     //!< When the GPU draws
            std::string                      m_rendererName = "software";
            Render3DConfig                   m_config;
            std::unique_ptr<k3::IRasterizer> m_rasterizer;
            AssetManager<k3::Model>          m_models;
            AssetManager<k3::AnimatedModel>  m_animatedModels;
            ModelRevision                    m_revision;
            std::chrono::steady_clock::time_point m_lastWatch = std::chrono::steady_clock::now();
    };

}
