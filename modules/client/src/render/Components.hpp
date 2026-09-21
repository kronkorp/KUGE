#pragma once

#include "Math2D.hpp"
#include "render/Color.hpp"
#include "render/Texture.hpp"
#include <cmath>
#include <memory>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Where something is, in the world (units are pixels at zoom 1)
     *
     * y points down. The rotation is in degrees, clockwise. This is what the
     * simulation moves, at the fixed rate.
     */
    ////////////////////////////////////////////////////////////////////////////
    struct Transform2D
    {
        Vec2  position{};
        float rotation = 0.0f;
        Vec2  scale{1.0f, 1.0f};

        constexpr bool operator==(const Transform2D&) const = default;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Where it was at the previous tick: what makes something move
     *         smoothly on a screen that refreshes faster than the simulation
     *
     * Add it to an entity that has a Transform2D, with the same value (or it
     * would slide in from the origin on the first frame). The client keeps it
     * up to date, and draws the entity between the two, following how far the
     * frame is between two ticks.
     *
     *     world.add<Transform2D>(e, Transform2D{{100, 50}});
     *     world.add<PreviousTransform2D>(e, PreviousTransform2D{{{100, 50}}});
     */
    ////////////////////////////////////////////////////////////////////////////
    struct PreviousTransform2D
    {
        Transform2D value{};
    };

    //! What to draw for an entity that has a Transform2D
    struct Sprite
    {
        //! Without a texture, a plain rectangle of the color of the tint
        std::shared_ptr<Texture> texture;
        Rect   source{};                 //!< Part of the texture, in pixels. Width 0: all of it.
        Vec2   size{};                   //!< In the world. (0, 0): the size of the picture.
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

    //! Between two angles in degrees, by the short way (350 to 10 goes through 0)
    inline float lerpAngle(float from, float to, float t)
    {
        const float delta = std::fmod(to - from + 180.0f, 360.0f);

        return from + ((delta < 0.0f ? delta + 360.0f : delta) - 180.0f) * t;
    }

    inline Transform2D lerp(const Transform2D& from, const Transform2D& to, float t)
    {
        return {Vec2::lerp(from.position, to.position, t), lerpAngle(from.rotation, to.rotation, t),
            Vec2::lerp(from.scale, to.scale, t)};
    }

}
