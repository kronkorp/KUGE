#pragma once

#include "AssetManager.hpp"
#include "Module.hpp"
#include "Ref.hpp"
#include "backend/Backend.hpp"
#include "input/InputMap.hpp"
#include "render/Color.hpp"
#include "render/Components.hpp"
#include "render/Texture.hpp"
#include <memory>
#include <optional>
#include <vector>

namespace kuge
{

    struct ClientConfig
    {
        Color clearColor{24, 24, 32, 255};   //!< What the screen is filled with each frame
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  What makes an engine a client: a window, its inputs, 2D drawing
     *
     *     kuge::Engine engine({.mode = kuge::Engine::Mode::Windowed});
     *     auto& client = engine.addModule<kuge::ClientModule>(kuge::makeSdlBackend({}));
     *     client.input().bind(Action::Shoot, kuge::Key::Space);
     *
     * Each loop, before the ticks, it reads what happened outside (closing the
     * window stops the engine) and clears the screen; after the frame it shows
     * what was drawn. Each scene gets, as resources of its World:
     *  - kuge::Ref<IWindow>, Ref<IRenderer2D>, Ref<InputMap>,
     *    Ref<AssetManager<Texture>> (the textures of files);
     *  - ActionState (what the player asked for at the last tick, see SampleInput);
     *  - Camera2D, WhitePixel, and AnimationEvents (the cues of the animations).
     * See ClientScene for the systems that use them.
     */
    ////////////////////////////////////////////////////////////////////////////
    class ClientModule : public Module
    {
        public:
            explicit ClientModule(Backend backend, ClientConfig config = {});

            IWindow&                  window(void) noexcept { return *m_backend.window; }
            IRenderer2D&              renderer(void) noexcept { return *m_backend.renderer; }
            InputMap&                 input(void) noexcept { return m_input; }
            AssetManager<Texture>&    textures(void) noexcept { return m_textures; }

            //! Keeps the image of the next frame before it is shown (see takeScreenshot())
            void requestScreenshot(void) noexcept { m_screenshotRequested = true; }

            //! The image asked for, once. Empty if the backend cannot read it.
            std::optional<Image> takeScreenshot(void);

            // Module
            void onAttach(Engine& engine) override;
            void inject(kw::World& world) override;
            void beginFrame(Engine& engine) override;
            void endFrame(Engine& engine) override;

        private:
            Backend                   m_backend;
            ClientConfig              m_config;
            InputMap                  m_input;
            std::vector<Event>        m_events;
            std::shared_ptr<Texture>  m_white;
            AssetManager<Texture>     m_textures;
            bool                      m_screenshotRequested = false;
            std::optional<Image>      m_screenshot;
    };

}
