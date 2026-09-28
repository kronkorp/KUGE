#pragma once

#include "audio/DummyAudio.hpp"
#include "backend/Backend.hpp"
#include "backend/dummy/DummyFont.hpp"
#include <map>
#include <string>
#include <vector>

namespace kuge
{

    //! A window that is only a size and a title
    class DummyWindow : public IWindow
    {
        public:
            explicit DummyWindow(Vec2 size) noexcept : m_size(size) {}

            Vec2 size(void) const override { return m_size; }
            void setTitle(const std::string& title) override { m_title = title; }

            const std::string& title(void) const noexcept { return m_title; }
            void resize(Vec2 size) noexcept { m_size = size; }

        private:
            Vec2        m_size;
            std::string m_title;
    };

    //! Inputs that the test decides: push() what "happened", the client polls it
    class DummyInput : public IInputSource
    {
        public:
            void push(const Event& event) { m_pending.push_back(event); }
            void poll(std::vector<Event>& out) override
            {
                out.insert(out.end(), m_pending.begin(), m_pending.end());
                m_pending.clear();
            }

        private:
            std::vector<Event> m_pending;
        };

    //! A renderer that draws nothing and remembers what it was asked to draw
    class DummyRenderer : public IRenderer2D
    {
        public:
            struct Call {
                enum class Kind { Fill, Stroke, Texture };

                Kind        kind;
                Rect        destination;
                Color       color;
                TextureDraw texture;   //!< Kind::Texture only
            };

            struct TextureInfo {
                int                        width;
                int                        height;
                std::vector<std::uint8_t>  rgba;
            };

            explicit DummyRenderer(Vec2 size) noexcept : m_size(size) {}

            Vec2 outputSize(void) const override { return m_size; }
            void begin(Color clear) override;
            void present(void) override;
            TextureId createTexture(int width, int height, std::span<const std::uint8_t> rgba) override;
            void destroyTexture(TextureId texture) noexcept override;
            void fillRect(const Rect& destination, Color color) override;
            void strokeRect(const Rect& destination, Color color) override;
            void drawTexture(const TextureDraw& draw) override;
            Image readPixels(void) override { return {}; }

            //! What was drawn since begin()
            const std::vector<Call>& current(void) const noexcept { return m_current; }

            //! What was drawn in the last frame that was presented
            const std::vector<Call>& lastFrame(void) const noexcept { return m_last; }

            std::size_t presented(void) const noexcept { return m_presented; }
            Color       clearColor(void) const noexcept { return m_clear; }
            void        resize(Vec2 size) noexcept { m_size = size; }

            //! Textures that exist
            std::size_t textureCount(void) const noexcept { return m_textures.size(); }
            const TextureInfo* texture(TextureId id) const;

        private:
            Vec2                          m_size;
            Color                         m_clear{};
            std::vector<Call>             m_current;
            std::vector<Call>             m_last;
            std::size_t                   m_presented = 0;
            std::map<TextureId, TextureInfo> m_textures;
            TextureId                     m_next = 1;
    };

    //! A backend for tests, and the way to reach its parts once the client owns them
    struct DummyBackend
    {
        Backend        backend;
        DummyWindow*   window;
        DummyInput*    input;
        DummyRenderer* renderer;
        DummyAudio*    audio;
        DummyFontLoader* fonts;
    };

    DummyBackend makeDummyBackend(Vec2 size = {640.0f, 480.0f});

}
