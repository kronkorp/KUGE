#pragma once

#include "AssetManager.hpp"
#include "Module.hpp"
#include "audio/Audio.hpp"
#include "Ref.hpp"
#include "backend/Backend.hpp"
#include "input/InputMap.hpp"
#include "render/Color.hpp"
#include "render/Components.hpp"
#include "render/Texture.hpp"
#include "ui/TextRenderer.hpp"
#include <map>
#include <memory>
#include <optional>
#include <string>
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
     *    Ref<AssetManager<Texture>> (the textures of files), Ref<Audio> with
     *    Ref<AssetManager<Sound>> and Ref<AssetManager<Music>>;
     *  - ActionState (what the player asked for at the last tick, see SampleInput);
     *  - Camera2D, WhitePixel, and AnimationEvents (the cues of the animations);
     *  - Ref<TextRenderer>, to draw text (see also loadFont() and the interface, ui/Ui.hpp).
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
            Audio&                    audio(void) noexcept { return m_mixer; }
            AssetManager<Sound>&      sounds(void) noexcept { return m_sounds; }
            AssetManager<Music>&      musics(void) noexcept { return m_musics; }
            TextRenderer&             text(void) noexcept { return m_text; }

            //! A font of the backend at a size, shared while somebody holds it
            //! @throw FontError if the backend has no fonts, or the file is not one
            std::shared_ptr<IFont> loadFont(const std::filesystem::path& file, int pointSize);

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
            Audio                     m_mixer;      // after the backend: what it plays goes first
            AssetManager<Sound>       m_sounds;
            AssetManager<Music>       m_musics;
            TextRenderer              m_text;
            std::map<std::pair<std::string, int>, std::weak_ptr<IFont>> m_fonts;
            bool                      m_screenshotRequested = false;
            std::optional<Image>      m_screenshot;
    };

}
