#pragma once

#include "Math2D.hpp"
#include "Transform2D.hpp"
#include "render/Color.hpp"
#include "render/Spritesheet.hpp"
#include "render/Texture.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>

namespace kuge
{

    //! What to draw for an entity that has a Transform2D
    struct Sprite
    {
        //! Without a texture, a plain rectangle of the color of the tint
        std::shared_ptr<Texture> texture;
        Rect   source{};                 //!< Part of the texture, in pixels. Width 0: all of it.
        //! With a sheet, the sprite is its cell number frame: texture and source are not used
        std::shared_ptr<const Spritesheet> sheet;
        int    frame = 0;                //!< A cell of the sheet (out of range: the first one)
        Vec2   size{};                   //!< In the world. (0, 0): the size of the picture (of a cell, with a sheet).
        Vec2   pivot{0.5f, 0.5f};        //!< The point of the sprite at the position, and what it turns around
        Color  tint{};
        int    layer = 0;                //!< Higher layers are over the lower ones
        float  z = 0.0f;                 //!< Order inside a layer
        bool   flipX = false;
        bool   flipY = false;
        bool   visible = true;
    };

    //! How a Camera2D puts its world in a window (see Camera2D::view()): where it draws, and how big a unit is
    struct CameraView
    {
        Vec2 position{};   //!< The point of the world at the center of `area`
        Rect area{};       //!< Where the world is drawn, in pixels of the window (the viewport without the bars)
        Vec2 scale{1.0f, 1.0f};   //!< Pixels of the window per unit of the world, along x and along y

        Vec2 center(void) const { return {area.x + area.w * 0.5f, area.y + area.h * 0.5f}; }
        Vec2 worldToScreen(Vec2 point) const
        {
            return {(point.x - position.x) * scale.x + center().x, (point.y - position.y) * scale.y + center().y};
        }
        Vec2 screenToWorld(Vec2 point) const
        {
            return {(point.x - center().x) / scale.x + position.x, (point.y - center().y) / scale.y + position.y};
        }
    };

    //! What the screen shows of the world (a resource of the World)
    //!
    //! Without a `logicalSize`, the zoom alone says how much is shown: the window divided by the zoom. With one,
    //! the world is drawn in a screen of that size that `fit` puts in the viewport, so every window shows the
    //! same part of the world (see view()). `logicalSize` is in pixels at a zoom of 1.
    struct Camera2D
    {
        //! How a logical screen is put in the viewport
        enum class Fit : std::uint8_t {
            Stretch,            //!< Fills it, whatever the shape (the pixels are not square any more)
            Letterbox,          //!< As big as it can be with the same shape: bars on two sides
            IntegerLetterbox,   //!< Like Letterbox, with a whole number of pixels for each logical pixel (pixel art
                                //!< stays sharp); smaller than the logical size, it is a Letterbox
            FitHeight,          //!< The height fills it: a wider window shows more at the sides, a narrower one less
        };

        Vec2  position{};    //!< The point of the world at the center of the screen
        float zoom = 1.0f;
        Vec2  logicalSize{};                 //!< The screen the game draws in (0, 0: the viewport, as it is)
        Fit   fit = Fit::Letterbox;
        std::optional<Rect> viewport;        //!< Where in the window it draws (none: all of it). Two cameras, split screen.

        //! Where the world goes in a window of this size
        CameraView view(Vec2 window) const
        {
            const Rect port = viewport.value_or(Rect{0.0f, 0.0f, window.x, window.y});
            CameraView result;

            result.position = position;
            result.area = port;
            result.scale = {zoom, zoom};
            if (!(logicalSize.x > 0.0f) || !(logicalSize.y > 0.0f) || !(port.w > 0.0f) || !(port.h > 0.0f)) {
                return result;
            }
            const float across = port.w / logicalSize.x;
            const float down = port.h / logicalSize.y;

            switch (fit) {
                case Fit::Stretch:
                    result.scale = {across * zoom, down * zoom};
                    break;
                case Fit::FitHeight:
                    result.scale = {down * zoom, down * zoom};
                    break;
                case Fit::Letterbox:
                case Fit::IntegerLetterbox: {
                    float fitted = std::min(across, down);

                    if (fit == Fit::IntegerLetterbox && fitted >= 1.0f) {
                        fitted = std::floor(fitted);
                    }
                    result.area = {port.x + (port.w - logicalSize.x * fitted) * 0.5f, port.y + (port.h - logicalSize.y * fitted) * 0.5f,
                        logicalSize.x * fitted, logicalSize.y * fitted};
                    result.scale = {fitted * zoom, fitted * zoom};
                    break;
                }
            }
            return result;
        }

        //! How much of the world is shown, in units of the world
        Vec2 visibleSize(Vec2 window) const
        {
            const CameraView shown = view(window);

            return {shown.area.w / shown.scale.x, shown.area.h / shown.scale.y};
        }

        Vec2 worldToScreen(Vec2 point, Vec2 window) const { return view(window).worldToScreen(point); }
        Vec2 screenToWorld(Vec2 point, Vec2 window) const { return view(window).screenToWorld(point); }
    };

    //! A white pixel, to draw plain rectangles that are turned or flipped
    //! (a resource of the World)
    struct WhitePixel
    {
        std::shared_ptr<Texture> texture;
    };


}
