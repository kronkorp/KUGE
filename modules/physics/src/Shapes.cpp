#include "Shapes.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    float axisOf(kuge::Vec2 v, int axis) { return axis == 0 ? v.x : v.y; }
}

bool kuge::overlaps(const Aabb& box, const Circle& circle)
{
    // The point of the box closest to the center of the circle
    const float x = std::clamp(circle.center.x, box.min.x, box.max.x);
    const float y = std::clamp(circle.center.y, box.min.y, box.max.y);
    const Vec2 gap{circle.center.x - x, circle.center.y - y};

    return gap.lengthSquared() < circle.radius * circle.radius;
}

bool kuge::overlaps(const Circle& a, const Circle& b)
{
    const float reach = a.radius + b.radius;

    return (a.center - b.center).lengthSquared() < reach * reach;
}

std::optional<kuge::RayHit> kuge::rayAabb(Vec2 origin, Vec2 direction, float maxDistance, const Aabb& box)
{
    float enter = 0.0f;
    float leave = maxDistance;
    Vec2 normal{};
    bool inside = true;

    for (int axis = 0; axis < 2; ++axis) {
        const float o = axisOf(origin, axis);
        const float d = axisOf(direction, axis);
        const float low = axisOf(box.min, axis);
        const float high = axisOf(box.max, axis);

        if (d == 0.0f) {
            if (o < low || o > high) {
                return std::nullopt;   // parallel to this slab, and outside it
            }
            continue;
        }
        float near = (low - o) / d;
        float far = (high - o) / d;
        float side = -1.0f;   // the ray enters through the low face

        if (near > far) {
            std::swap(near, far);
            side = 1.0f;
        }
        if (near > 0.0f) {
            inside = false;
        }
        if (near > enter) {
            enter = near;
            normal = axis == 0 ? Vec2{side, 0.0f} : Vec2{0.0f, side};
        }
        leave = std::min(leave, far);
        if (enter > leave) {
            return std::nullopt;
        }
    }
    if (inside) {
        return RayHit{0.0f, {}};
    }
    return RayHit{enter, normal};
}

std::optional<kuge::RayHit> kuge::rayCircle(Vec2 origin, Vec2 direction, float maxDistance, const Circle& circle)
{
    const Vec2 toCenter = circle.center - origin;

    if (toCenter.lengthSquared() < circle.radius * circle.radius) {
        return RayHit{0.0f, {}};
    }
    // Distance along the ray to the point closest to the center, then back off
    // by the half chord
    const float along = toCenter.dot(direction);
    const float gap2 = toCenter.lengthSquared() - along * along;
    const float half2 = circle.radius * circle.radius - gap2;

    if (along < 0.0f || half2 < 0.0f) {
        return std::nullopt;
    }
    const float distance = along - std::sqrt(half2);

    if (distance > maxDistance) {
        return std::nullopt;
    }
    return RayHit{distance, ((origin + direction * distance) - circle.center) / circle.radius};
}

std::optional<float> kuge::pushOutAlongAxis(const Aabb& box, const Circle& circle, int axis, float sign)
{
    if (!overlaps(box, circle)) {
        return std::nullopt;
    }
    // Across the axis, how far the center of the circle is from the box: that
    // gives the half width of the circle at the level of the box
    const int across = 1 - axis;
    const float c = axisOf(circle.center, across);
    const float gap = std::max({axisOf(box.min, across) - c, 0.0f, c - axisOf(box.max, across)});
    const float half = std::sqrt(std::max(circle.radius * circle.radius - gap * gap, 0.0f));
    const float center = axisOf(circle.center, axis);

    // Moving towards +: the far side of the box stops at the near side of the circle
    return sign > 0.0f ? (center - half) - axisOf(box.max, axis) : (center + half) - axisOf(box.min, axis);
}
