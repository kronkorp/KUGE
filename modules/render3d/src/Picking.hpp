#pragma once

#include "Math2D.hpp"
#include <kronk3d/scene/Camera.hpp>

namespace kuge
{

    //! A half line: where it starts and which way it goes (a unit vector)
    struct Ray3D
    {
        k3::Math::Vector3f origin;
        k3::Math::Vector3f direction;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The ray that goes from the eye of a camera through a point of the screen: what is under the mouse
     *
     * @param point   Pixels of the screen, y down (InputMap::mousePosition())
     * @param screen  The size of the screen, in the same pixels (IRenderer2D::outputSize())
     *
     * It is the inverse of what the camera shows, perspective or orthographic. To pick, ask a physics for the first
     * thing that the ray meets:
     *
     *     const Ray3D ray = screenRay(camera, input.mousePosition(), renderer.outputSize());
     *     if (const auto hit = physics.raycast(ray.origin, ray.direction, 100.0f)) { ... hit->entity ... }
     */
    ////////////////////////////////////////////////////////////////////////////
    Ray3D screenRay(const k3::Camera& camera, Vec2 point, Vec2 screen);

}
