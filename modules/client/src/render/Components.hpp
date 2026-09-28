#pragma once

#include "Math2D.hpp"
#include "Transform2D.hpp"
#include "render/Color.hpp"
#include "render/Spritesheet.hpp"
#include "render/Texture.hpp"
#include <memory>

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

    //! What the screen shows of the world (a resource of the World)
    struct Camera2D
    {
        Vec2  position{};    //!< The point of the world at the center of the screen
        float zoom = 1.0f;

        Vec2 worldToScreen(Vec2 point, Vec2 screen) const { return (point - position) * zoom + screen * 0.5f; }
        Vec2 screenToWorld(Vec2 point, Vec2 screen) const { return (point - screen * 0.5f) / zoom + position; }
    };

    //! A white pixel, to draw plain rectangles that are turned or flipped
    //! (a resource of the World)
    struct WhitePixel
    {
        std::shared_ptr<Texture> texture;
    };


}
