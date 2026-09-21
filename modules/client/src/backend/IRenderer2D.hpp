#pragma once

#include "Math2D.hpp"
#include "render/Color.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace kuge
{

    //! A picture kept by the renderer. 0 is no texture.
    using TextureId = std::uint32_t;
    constexpr TextureId NO_TEXTURE = 0;

    //! Pixels: 4 bytes each (red, green, blue, alpha), row after row, from the top
    struct Image
    {
        int                        width  = 0;
        int                        height = 0;
        std::vector<std::uint8_t>  rgba;

        bool empty(void) const noexcept { return rgba.empty(); }
    };

    //! One picture to draw
    struct TextureDraw
    {
        TextureId texture = NO_TEXTURE;
        Rect      source{};                  //!< In pixels of the texture. Width 0: all of it.
        Rect      destination{};             //!< On the screen, in pixels
        float     rotation = 0.0f;           //!< Degrees, clockwise, around the pivot
        Vec2      pivot{0.5f, 0.5f};         //!< In the destination, from (0, 0) to (1, 1)
        Color     tint{};                    //!< Multiplies the colors of the texture
        bool      flipX = false;
        bool      flipY = false;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Draws in 2D, in pixels of the screen (the camera is not its concern)
     *
     * A backend gives one. Between begin() and present(), what is drawn later
     * is over what was drawn before.
     */
    ////////////////////////////////////////////////////////////////////////////
    class IRenderer2D
    {
        public:
            virtual ~IRenderer2D(void) = default;

            //! The size of the screen, in pixels
            virtual Vec2 outputSize(void) const = 0;

            //! Starts a frame: fills the screen with a color
            virtual void begin(Color clear) = 0;

            //! Shows the frame. It can wait for the screen (vsync).
            virtual void present(void) = 0;

            //! @param rgba  width * height pixels
            //! @throw std::runtime_error if the picture cannot be kept
            virtual TextureId createTexture(int width, int height, std::span<const std::uint8_t> rgba) = 0;
            virtual void destroyTexture(TextureId texture) noexcept = 0;

            virtual void fillRect(const Rect& destination, Color color) = 0;
            virtual void strokeRect(const Rect& destination, Color color) = 0;
            virtual void drawTexture(const TextureDraw& draw) = 0;

            //! What was drawn since begin(). Read it before present(). Empty
            //! if the backend cannot.
            virtual Image readPixels(void) = 0;
    };

}
