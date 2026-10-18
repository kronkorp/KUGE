#pragma once

#include <cstdint>
#include <kronk3d/math/Vector.hpp>

namespace kuge
{

    //! (k3::Math::Vector3f has no comparison of its own)
    constexpr bool sameVector(const k3::Math::Vector3f& a, const k3::Math::Vector3f& b) noexcept
    {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The shape of something that can be touched, in 3D
     *
     * With a Transform3D, an entity that has a Collider3D takes part in the physics (kuge-physics3d).
     * Its position is the one of the Transform3D plus the offset (turned with it), and the scale of the
     * Transform3D stretches the size and the offset.
     *
     *     world.add<Collider3D>(floor, Collider3D::box(50, 1, 50));
     *     world.add<Collider3D>(coin, Collider3D::sphere(0.3f).asTrigger());
     */
    ////////////////////////////////////////////////////////////////////////////
    struct Collider3D
    {
        enum class Shape : std::uint8_t { Box, Sphere, Capsule };

        Shape              shape = Shape::Box;
        //! Box: width, height and depth. Sphere: size.x is the radius. Capsule: size.x is the radius and
        //! size.y the whole height (standing along y).
        k3::Math::Vector3f size{1.0f, 1.0f, 1.0f};
        k3::Math::Vector3f offset{};             //!< From the position of the entity
        std::uint32_t      layer = 1;            //!< What this is, as a set of bits
        std::uint32_t      mask = 0xFFFFFFFFu;   //!< What this reacts to: the layers it meets
        bool               trigger = false;      //!< Does not stop anything: only reports who is inside
        float              friction = 0.5f;
        float              restitution = 0.0f;   //!< 0: does not bounce. 1: bounces back as fast as it came.

        static constexpr Collider3D box(float width, float height, float depth)
        {
            Collider3D collider;
            collider.size = {width, height, depth};
            return collider;
        }

        static constexpr Collider3D sphere(float radius)
        {
            Collider3D collider;
            collider.shape = Shape::Sphere;
            collider.size = {radius, radius, radius};
            return collider;
        }

        //! Standing along y; `height` is the whole of it, so at least twice the radius
        static constexpr Collider3D capsule(float radius, float height)
        {
            Collider3D collider;
            collider.shape = Shape::Capsule;
            collider.size = {radius, height, radius};
            return collider;
        }

        constexpr Collider3D asTrigger(void) const { Collider3D c = *this; c.trigger = true; return c; }

        constexpr Collider3D onLayer(std::uint32_t bits, std::uint32_t meets = 0xFFFFFFFFu) const
        {
            Collider3D c = *this;
            c.layer = bits;
            c.mask = meets;
            return c;
        }

        constexpr bool operator==(const Collider3D& o) const noexcept
        {
            return shape == o.shape && sameVector(size, o.size) && sameVector(offset, o.offset) && layer == o.layer
                && mask == o.mask && trigger == o.trigger && friction == o.friction && restitution == o.restitution;
        }
    };

    //! Two things meet only if each one is on a layer that the other reacts to
    constexpr bool compatible(const Collider3D& a, const Collider3D& b)
    {
        return (a.layer & b.mask) != 0 && (b.layer & a.mask) != 0;
    }

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A thing that moves in 3D (a component)
     *
     * A Collider3D with no Body3D does not move: it is a floor, a wall, a zone. With one, the physics
     * writes the Transform3D and the velocity at each tick; the game can also write them (a teleport,
     * a character that walks: it sets the velocity), and the physics takes them as they are.
     */
    ////////////////////////////////////////////////////////////////////////////
    struct Body3D
    {
        enum class Type : std::uint8_t {
            Static,      //!< Does not move (same as having no Body3D)
            Kinematic,   //!< Moves by its velocity only, pushes what it meets: platforms, doors
            Dynamic,     //!< And is pulled by the gravity, pushed by what it meets: characters, crates
        };

        Type               type = Type::Dynamic;
        k3::Math::Vector3f velocity{};         //!< Meters per second
        float              gravityScale = 1.0f;
        float              mass = 0.0f;        //!< Kilograms; 0: from the size of the shape (1000 kg per cubic meter)
        float              linearDamping = 0.05f;  //!< Slows it down by itself (a crate that stops sliding)
        float              angularDamping = 0.05f; //!< Same for its turning
        //! Stays upright: a character that does not fall over. It does not rub against walls either: pressed
        //! against one in the air, it falls (a body that is driven by a velocity would stick to it).
        bool               lockRotation = false;
        bool               grounded = false;   //!< Written by the physics: stands on something that is not a trigger

        constexpr bool operator==(const Body3D& o) const noexcept
        {
            return type == o.type && sameVector(velocity, o.velocity) && gravityScale == o.gravityScale && mass == o.mass
                && linearDamping == o.linearDamping && angularDamping == o.angularDamping
                && lockRotation == o.lockRotation && grounded == o.grounded;
        }
    };

}
