#pragma once

#include <cmath>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A point or a vector in 2D
     *
     * The y axis points down (like the screen), and angles go clockwise.
     */
    ////////////////////////////////////////////////////////////////////////////
    struct Vec2
    {
        float x = 0.0f;
        float y = 0.0f;

        constexpr Vec2(void) = default;
        constexpr Vec2(float px, float py) : x(px), y(py) {}

        constexpr Vec2  operator-(void) const { return {-x, -y}; }
        constexpr Vec2  operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
        constexpr Vec2  operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
        constexpr Vec2  operator*(float s) const { return {x * s, y * s}; }
        constexpr Vec2  operator/(float s) const { return {x / s, y / s}; }
        constexpr Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
        constexpr Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
        constexpr Vec2& operator*=(float s) { x *= s; y *= s; return *this; }

        constexpr bool operator==(const Vec2&) const = default;

        constexpr float dot(Vec2 o) const { return x * o.x + y * o.y; }
        constexpr float lengthSquared(void) const { return dot(*this); }
        float           length(void) const { return std::sqrt(lengthSquared()); }

        //! The vector with the same direction and a length of 1 (the zero
        //! vector stays the zero vector)
        Vec2 normalized(void) const
        {
            const float size = length();

            return size > 0.0f ? *this / size : Vec2{};
        }

        //! a when t is 0, b when t is 1
        static constexpr Vec2 lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }
    };

    constexpr Vec2 operator*(float s, Vec2 v) { return v * s; }

    //! An axis-aligned rectangle: its top-left corner and its size
    struct Rect
    {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;

        constexpr bool operator==(const Rect&) const = default;

        constexpr float right(void) const { return x + w; }
        constexpr float bottom(void) const { return y + h; }
        constexpr Vec2  center(void) const { return {x + w / 2, y + h / 2}; }

        constexpr bool contains(Vec2 p) const
        {
            return p.x >= x && p.x < right() && p.y >= y && p.y < bottom();
        }

        //! Rectangles that only share an edge do not intersect
        constexpr bool intersects(const Rect& o) const
        {
            return x < o.right() && o.x < right() && y < o.bottom() && o.y < bottom();
        }
    };

}
