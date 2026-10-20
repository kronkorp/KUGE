#pragma once
// The map of Arena: boxes, some of them tilted into ramps. The room and the prediction make colliders of them,
// and the client makes the meshes: a floor you see is a floor you stand on.

#include <cstdint>
#include <kronk3d/math/Vector.hpp>
#include <vector>

namespace arena
{

    using Vec3 = k3::Math::Vector3f;

    //! What a piece looks like (its texture). It changes nothing for the physics.
    enum class Surface : std::uint8_t { Floor, Wall, Platform };

    struct Piece
    {
        Vec3                center;
        Vec3                size;            //!< Of the box, before its rotation
        k3::Math::Vector4f  rotation{0.0f, 0.0f, 0.0f, 1.0f};   //!< A unit quaternion
        Surface             surface = Surface::Wall;
    };

    //! Where a player appears, and where it looks (radians, as k3::Camera)
    struct Spawn
    {
        Vec3  at;
        float yaw;
    };

    constexpr float HALF_SIZE = 20.0f;      // meters: the floor goes from -20 to 20 in x and z

    //! The product of two rotations (unit quaternions): b is applied first
    k3::Math::Vector4f multiply(const k3::Math::Vector4f& a, const k3::Math::Vector4f& b);

    //! The level: a floor, high walls around it, a platform in the middle with a ramp on each side, two walkways
    //! 3 m up with a ramp each, low walls to hide behind, pillars
    const std::vector<Piece>& level(void);

    //! Eight places, far from one another, all looking at the middle
    const std::vector<Spawn>& spawns(void);

    //! Where the crates start (their middle: a crate is a cube of 1 m)
    const std::vector<Vec3>& crates(void);

}
