#pragma once

#include "Math2D.hpp"
#include "backend/IRenderer2D.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A picture in the renderer. It is given back to it when the last
     *         one who holds the texture lets go.
     *
     * Get them from the AssetManager of the client (files) or fromPixels(). A
     * texture must not outlive the renderer: the scenes (which hold the
     * textures) are destroyed before the client module, so this holds as long
     * as nothing keeps a texture in a global.
     */
    ////////////////////////////////////////////////////////////////////////////
    class Texture
    {
        public:
            Texture(IRenderer2D& renderer, TextureId id, int width, int height) noexcept;
            ~Texture(void);

            Texture(const Texture&)            = delete;
            Texture& operator=(const Texture&) = delete;

            TextureId id(void) const noexcept { return m_id; }
            int       width(void) const noexcept { return m_width; }
            int       height(void) const noexcept { return m_height; }
            Vec2      size(void) const noexcept { return {static_cast<float>(m_width), static_cast<float>(m_height)}; }

            //! @param rgba  width * height pixels of 4 bytes
            static std::shared_ptr<Texture> fromPixels(
                IRenderer2D& renderer, int width, int height, std::span<const std::uint8_t> rgba);

            //! @throw ImageError
            static std::shared_ptr<Texture> fromFile(IRenderer2D& renderer, const std::filesystem::path& path);

            //! Puts another picture in this texture: whoever holds it draws the new one.
            //! The id and the size change. If the renderer cannot keep the new picture
            //! (it throws), the texture is left as it was.
            void replace(int width, int height, std::span<const std::uint8_t> rgba);

            //! replace() with the picture of a file. @throw ImageError (the texture is left as it was)
            void reloadFromFile(const std::filesystem::path& path);

        private:
            IRenderer2D& m_renderer;
            TextureId    m_id;
            int          m_width;
            int          m_height;
    };

}
