#pragma once

#include "Math2D.hpp"
#include <optional>

namespace kuge
{

    //! A box with sides along the axes, by its corners
    struct Aabb
    {
        Vec2 min{};
        Vec2 max{};

        static constexpr Aabb fromCenter(Vec2 center, Vec2 half) { return {center - half, center + half}; }

        constexpr Vec2 center(void) const { return (min + max) * 0.5f; }
        constexpr Vec2 half(void) const { return (max - min) * 0.5f; }

        //! Boxes that only touch along an edge do not overlap
        constexpr bool overlaps(const Aabb& o) const
        {
            return min.x < o.max.x && o.min.x < max.x && min.y < o.max.y && o.min.y < max.y;
        }

        constexpr Aabb expanded(float by) const { return {{min.x - by, min.y - by}, {max.x + by, max.y + by}}; }
    };

    struct Circle
    {
        Vec2  center{};
        float radius = 0.0f;
    };

    bool overlaps(const Aabb& box, const Circle& circle);
    bool overlaps(const Circle& a, const Circle& b);

    //! Where a ray meets a shape
    struct RayHit
    {
        float distance = 0.0f;   //!< From the origin
        Vec2  normal{};          //!< Points out of the shape, where it was hit. Zero if the ray starts inside.
    };

    //! direction must be of length 1. Only hits within maxDistance count.
    std::optional<RayHit> rayAabb(Vec2 origin, Vec2 direction, float maxDistance, const Aabb& box);
    std::optional<RayHit> rayCircle(Vec2 origin, Vec2 direction, float maxDistance, const Circle& circle);

    //! How far to move a box that overlaps a circle, along an axis (0: x, 1: y),
    //! in the direction of sign (+1 or -1), for it not to overlap any more.
    //! Nothing if they do not overlap. The result is signed: it can be added to
    //! the position of the box.
    std::optional<float> pushOutAlongAxis(const Aabb& box, const Circle& circle, int axis, float sign);

}
