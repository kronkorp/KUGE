#pragma once

#include <cstdint>
#include <string>
#include <vector>
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

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A part of a body that a shot can hit, with its own value (a component of many)
     *
     * A head, a body and legs are three hitboxes of one player: a ray that hits the head is worth more
     * (`damageScale`). The room reads the part that was hit (Physics3D::shoot()); the client never decides.
     * A part is placed from the entity, or from one of its bones (`bone`, see Hitboxes::bones).
     */
    ////////////////////////////////////////////////////////////////////////////
    struct Hitbox
    {
        enum class Shape : std::uint8_t { Box, Sphere, Capsule };

        std::string        name;                          //!< "head", "body"...: for the game's rules and its logs
        Shape              shape = Shape::Capsule;
        //! As Collider3D::size: a box (width, height, depth), a sphere (x is the radius), a capsule standing along y (x is
        //! the radius, y the whole height)
        k3::Math::Vector3f size{0.3f, 1.0f, 0.3f};
        k3::Math::Vector3f offset{};                      //!< From the entity (or from its bone), in the space of the entity
        std::string        bone;                          //!< A name in Hitboxes::bones; empty: follows the entity
        float              damageScale = 1.0f;            //!< 2 for a head, 0.5 for a leg
    };

    //! Where a bone is, in the space of the entity: what the animation (kuge-animation) writes, when the room samples the pose
    struct BonePlace
    {
        std::string        name;
        k3::Math::Vector3f translation{};
        k3::Math::Vector4f rotation{0.0f, 0.0f, 0.0f, 1.0f};   //!< Unit quaternion x y z w
    };

    //! The parts of an entity that a shot can hit (with a Transform3D), in the order the game gave them: the index of the
    //! part that was hit is the index in `parts`
    struct Hitboxes
    {
        std::vector<Hitbox>    parts;
        std::vector<BonePlace> bones;   //!< Only needed by parts that name a bone
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A character that walks (a component): a capsule moved by Physics3D::moveCharacter(), not a rigid body
     *
     * Its Transform3D::translation is where its **feet** are. The settings are the game's; `velocity`, `onGround`
     * and `crouching` are written by moveCharacter() (the client and the room call it with the same inputs, so
     * they agree). Do not give it a Collider3D or a Body3D: it is not a body of the physics.
     */
    ////////////////////////////////////////////////////////////////////////////
    struct Character3D
    {
        float         radius = 0.35f;
        float         height = 1.8f;              //!< Standing, feet to top
        float         crouchHeight = 1.0f;
        float         stepHeight = 0.4f;          //!< It climbs a step this high without jumping
        float         maxSlope = 50.0f;           //!< Degrees. A steeper slope is a wall: it cannot be climbed.
        float         jumpSpeed = 5.0f;           //!< Meters per second up
        float         gravityScale = 1.0f;
        std::uint32_t mask = 0xFFFFFFFFu;         //!< The layers it meets

        k3::Math::Vector3f velocity{};            //!< Written: how it moved during the last call
        bool          onGround = false;           //!< Written: stands on something it can stand on
        bool          crouching = false;          //!< Written: it is crouched (it stays so under a ceiling)
    };

}
