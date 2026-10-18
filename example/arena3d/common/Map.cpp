#include "Map.hpp"
#include "Transform3D.hpp"
#include <cmath>

namespace arena
{

    k3::Math::Vector4f multiply(const k3::Math::Vector4f& a, const k3::Math::Vector4f& b)
    {
        return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
    }

    namespace
    {
        using Quat = k3::Math::Vector4f;

        Vec3 rotate(const Quat& q, const Vec3& v)
        {
            // v + 2 * cross(q.xyz, cross(q.xyz, v) + w * v)
            const Vec3 u{q.x, q.y, q.z};
            const Vec3 t = Vec3::cross(u, v) * 2.0f;

            return v + t * q.w + Vec3::cross(u, t);
        }

        Piece block(Vec3 center, Vec3 size, Surface surface)
        {
            Piece piece;

            piece.center = center;
            piece.size = size;
            piece.surface = surface;
            return piece;
        }

        // A ramp whose upper face goes from `low` (on the ground) up to `high`: a box as long as the slope, as wide
        // as asked, tilted, and sunk by its thickness so that its upper face is the slope
        Piece ramp(Vec3 low, Vec3 high, float width, float thickness)
        {
            const Vec3 along = high - low;
            const float length = Vec3::length(along);
            const float yaw = std::atan2(along.x, along.z);
            const float slope = std::asin(along.y / length);
            const Quat rotation = multiply(kuge::axisAngle({0.0f, 1.0f, 0.0f}, yaw), kuge::axisAngle({1.0f, 0.0f, 0.0f}, -slope));
            const Vec3 up = rotate(rotation, {0.0f, 1.0f, 0.0f});
            Piece piece;

            piece.center = (low + high) * 0.5f - up * (thickness * 0.5f);
            piece.size = {width, thickness, length};
            piece.rotation = rotation;
            piece.surface = Surface::Platform;
            return piece;
        }
    }

    const std::vector<Piece>& level(void)
    {
        static const std::vector<Piece> pieces = [] {
            constexpr float H = HALF_SIZE;
            constexpr float WALL = 5.0f;
            std::vector<Piece> p;

            // The floor, and the walls around it
            p.push_back(block({0, -0.5f, 0}, {2 * H + 2, 1, 2 * H + 2}, Surface::Floor));
            p.push_back(block({0, WALL / 2, -H - 0.5f}, {2 * H + 2, WALL, 1}, Surface::Wall));
            p.push_back(block({0, WALL / 2, H + 0.5f}, {2 * H + 2, WALL, 1}, Surface::Wall));
            p.push_back(block({-H - 0.5f, WALL / 2, 0}, {1, WALL, 2 * H + 2}, Surface::Wall));
            p.push_back(block({H + 0.5f, WALL / 2, 0}, {1, WALL, 2 * H + 2}, Surface::Wall));
            // The platform in the middle, 2 m up, and a ramp on each side of it
            p.push_back(block({0, 1.0f, 0}, {10, 2, 10}, Surface::Platform));
            p.push_back(ramp({0, 0, -14}, {0, 2, -5}, 4.0f, 0.4f));
            p.push_back(ramp({0, 0, 14}, {0, 2, 5}, 4.0f, 0.4f));
            // Two walkways, 3 m up, one on each side, reached by a ramp from the south, on pillars
            for (const float side : {-1.0f, 1.0f}) {
                p.push_back(block({side * 14, 2.75f, -3}, {4, 0.5f, 20}, Surface::Platform));
                p.push_back(ramp({side * 14, 0, 18.5f}, {side * 14, 3, 7}, 4.0f, 0.4f));
                for (const float z : {-11.0f, -3.0f}) {
                    p.push_back(block({side * 14, 1.25f, z}, {0.8f, 2.5f, 0.8f}, Surface::Wall));
                }
            }
            // Low walls to hide behind, and tall pillars
            p.push_back(block({-7, 0.7f, -9}, {6, 1.4f, 0.6f}, Surface::Wall));
            p.push_back(block({7, 0.7f, -9}, {6, 1.4f, 0.6f}, Surface::Wall));
            p.push_back(block({-8, 0.7f, 9}, {0.6f, 1.4f, 6}, Surface::Wall));
            p.push_back(block({8, 0.7f, 9}, {0.6f, 1.4f, 6}, Surface::Wall));
            p.push_back(block({0, 0.7f, 17}, {8, 1.4f, 0.6f}, Surface::Wall));
            for (const float side : {-1.0f, 1.0f}) {
                p.push_back(block({side * 7, 2.5f, 0}, {1.2f, 5, 1.2f}, Surface::Wall));
            }
            return p;
        }();

        return pieces;
    }

    const std::vector<Spawn>& spawns(void)
    {
        static const std::vector<Spawn> places = [] {
            std::vector<Spawn> s;

            for (const Vec3 at : {Vec3{-17, 1, -17}, Vec3{17, 1, 17}, Vec3{17, 1, -17}, Vec3{-17, 1, 17},
                                  Vec3{0, 1, -17.5f}, Vec3{0, 1, 18.5f}, Vec3{-18, 1, 0}, Vec3{18, 1, 0}}) {
                s.push_back({at, std::atan2(-at.x, at.z)});     // (looking at the middle)
            }
            return s;
        }();

        return places;
    }

    const std::vector<Vec3>& crates(void)
    {
        static const std::vector<Vec3> places = {
            {-11, 0.5f, 0}, {-11, 0.5f, 1.2f}, {-11, 0.5f, -1.2f}, {-11, 1.5f, 0},       // a pyramid
            {11, 0.5f, 3}, {11, 0.5f, 4.2f},
            {-3, 0.5f, -12}, {3, 0.5f, -12.6f}, {-5.5f, 0.5f, -6.5f},
            {-4, 0.5f, 12}, {4.5f, 0.5f, 12.5f}, {4.5f, 1.5f, 12.5f},
            {-11, 0.5f, 14}, {13, 0.5f, -15},
        };

        return places;
    }

}
