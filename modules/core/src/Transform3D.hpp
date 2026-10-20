#pragma once

#include <kronk3d/scene/Animation.hpp>
#include <cmath>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Where something is, in the 3D world
     *
     * It is kronk3d's k3::Transform (translation, rotation as a unit quaternion
     * x y z w, scale), which already knows its matrix() and how to be mixed
     * (mix()): KUGE adds nothing but a name and a comparison. The world is
     * right-handed, y points up, the units are meters. This is what the
     * simulation moves, at the fixed rate.
     *
     *     world.add<Transform3D>(e, Transform3D{{{0, 1, 0}, axisAngle({0, 1, 0}, 1.57f), {1, 1, 1}}});
     */
    ////////////////////////////////////////////////////////////////////////////
    struct Transform3D : k3::Transform
    {
        constexpr bool operator==(const Transform3D& other) const noexcept
        {
            return translation.x == other.translation.x && translation.y == other.translation.y
                && translation.z == other.translation.z
                && rotation.x == other.rotation.x && rotation.y == other.rotation.y
                && rotation.z == other.rotation.z && rotation.w == other.rotation.w
                && scale.x == other.scale.x && scale.y == other.scale.y && scale.z == other.scale.z;
        }
    };

    //! Where it was at the previous tick (see PreviousTransform2D): add it with the same value as the Transform3D
    struct PreviousTransform3D
    {
        Transform3D value{};
    };

    //! The rotation of an angle (radians, counter-clockwise seen from the tip of the axis) around an axis
    //! (any length but zero), as the quaternion a Transform3D holds
    inline k3::Math::Vector4f axisAngle(const k3::Math::Vector3f& axis, float radians)
    {
        const float length = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);

        if (length == 0.0f)
            return {0.0f, 0.0f, 0.0f, 1.0f};
        const float k = std::sin(radians * 0.5f) / length;

        return {axis.x * k, axis.y * k, axis.z * k, std::cos(radians * 0.5f)};
    }

    //! Between two transforms: positions and scales in a line, rotations by the short way
    inline Transform3D lerp(const Transform3D& from, const Transform3D& to, float t)
    {
        Transform3D result;

        static_cast<k3::Transform&>(result) = k3::Transform::mix(from, to, t);
        return result;
    }

}
