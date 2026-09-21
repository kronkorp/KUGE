#pragma once

#include "Math2D.hpp"
#include <cstdint>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The shape of something that can be touched
     *
     * With a Transform2D, an entity that has a Collider takes part in the
     * physics. Its position is the one of the Transform2D plus the offset, and
     * the scale of the Transform2D stretches the size and the offset.
     *
     *     world.add<Collider>(wall, Collider::box(200, 20));
     *     world.add<Collider>(coin, Collider::circle(8).asTrigger());
     */
    ////////////////////////////////////////////////////////////////////////////
    struct Collider
    {
        enum class Shape : std::uint8_t { Box, Circle };

        Shape         shape   = Shape::Box;
        Vec2          size{16.0f, 16.0f};   //!< Box: width and height. Circle: size.x is the radius.
        Vec2          offset{};             //!< From the position of the entity
        std::uint32_t layer   = 1;          //!< What this is, as a set of bits
        std::uint32_t mask    = 0xFFFFFFFFu; //!< What this reacts to: the layers it meets
        bool          trigger = false;      //!< Does not stop anything: only reports who is inside

        static constexpr Collider box(float width, float height)
        {
            Collider collider;

            collider.size = {width, height};
            return collider;
        }

        static constexpr Collider circle(float radius)
        {
            Collider collider;

            collider.shape = Shape::Circle;
            collider.size = {radius, radius};
            return collider;
        }

        constexpr Collider asTrigger(void) const { Collider c = *this; c.trigger = true; return c; }
        constexpr Collider onLayer(std::uint32_t bits, std::uint32_t meets = 0xFFFFFFFFu) const
        {
            Collider c = *this;
            c.layer = bits;
            c.mask = meets;
            return c;
        }
    };

    //! Two things meet only if each one is on a layer that the other reacts to
    constexpr bool compatible(const Collider& a, const Collider& b)
    {
        return (a.layer & b.mask) != 0 && (b.layer & a.mask) != 0;
    }

    //! Which sides of a body touch a wall (a collider with no Body, or a solid
    //! tile), as of the last tick. It holds for a body that stands still: on the
    //! floor, or pressed against a wall.
    struct Contacts
    {
        bool left  = false;
        bool right = false;
        bool up    = false;
        bool down  = false;

        constexpr bool any(void) const { return left || right || up || down; }
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A thing that moves by itself
     *
     * A Collider with no Body does not move: it is a wall, a floor, a zone. A
     * body moves by its velocity (pixels per second) and is stopped by the walls
     * (the colliders that have no Body, and the solid tiles), sliding along
     * them. Bodies do not stop each other: to know who touches whom, use a
     * trigger.
     *
     * A body is a box: it is the bounding box of its collider if that is a circle.
     */
    ////////////////////////////////////////////////////////////////////////////
    struct Body
    {
        enum class Type : std::uint8_t {
            Static,      //!< Does not move (same as having no Body)
            Kinematic,   //!< Moves by its velocity only: platforms, bullets, walkers
            Dynamic,     //!< And is pulled by the gravity: characters, crates
        };

        Type     type = Type::Kinematic;
        Vec2     velocity{};
        float    gravityScale = 1.0f;   //!< Dynamic only: 0 for none, 2 for twice
        Contacts contacts;              //!< Written by the physics at each tick
    };

}
