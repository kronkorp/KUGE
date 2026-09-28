#pragma once

#include "ui/Font.hpp"
#include "render/Texture.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Draws text: it makes a picture of each text once, keeps it while
     *         the text keeps being drawn, and draws that picture
     *
     * The client calls beginFrame() once per loop: a text that was not drawn for
     * a while is forgotten. Available to systems as Ref<TextRenderer>.
     */
    ////////////////////////////////////////////////////////////////////////////
    class TextRenderer
    {
        public:
            explicit TextRenderer(IRenderer2D& renderer) noexcept : m_renderer(renderer) {}

            //! Draws text with its top-left corner at position, in pixels of the screen
            void draw(const IFont& font, std::string_view text, Vec2 position, Color color, int wrapWidth = 0);

            //! Number of texts that are kept
            std::size_t cached(void) const noexcept { return m_cache.size(); }

            void beginFrame(void);

        private:
            struct Key
            {
                std::uint64_t font;
                std::string   text;
                int           wrap;

                bool operator<(const Key& o) const
                {
                    if (font != o.font) { return font < o.font; }
                    if (wrap != o.wrap) { return wrap < o.wrap; }
                    return text < o.text;
                }
            };

            struct Entry
            {
                std::shared_ptr<Texture> texture;   //!< null for an empty text
                std::uint64_t            lastUsed;
            };

            static constexpr std::uint64_t KEEP_FRAMES = 120;
            static constexpr std::size_t   MAX_TEXTS   = 1024;

            IRenderer2D&        m_renderer;
            std::map<Key, Entry> m_cache;
            std::uint64_t       m_frame = 0;
    };

}
