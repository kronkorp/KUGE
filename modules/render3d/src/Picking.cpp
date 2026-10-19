#include "Picking.hpp"
#include <algorithm>
#include <cmath>

kuge::Ray3D kuge::screenRay(const k3::Camera& camera, Vec2 point, Vec2 screen)
{
    using Vec3 = k3::Math::Vector3f;
    // From -1 to 1 across the screen, up is positive (the middle of the pixel)
    const float x = 2.0f * (point.x + 0.5f) / std::max(screen.x, 1.0f) - 1.0f;
    const float y = 1.0f - 2.0f * (point.y + 0.5f) / std::max(screen.y, 1.0f);
    const float aspect = screen.x / std::max(screen.y, 1.0f);
    const Vec3 forward = camera.forward();
    const Vec3 right = camera.right();
    const Vec3 up = camera.up();

    if (camera.projection == k3::Projection::Orthographic) {
        const float halfHeight = camera.orthoHeight * 0.5f;

        return {camera.position + right * (x * halfHeight * aspect) + up * (y * halfHeight), forward};
    }
    const float reach = std::tan(camera.fovY * 0.5f);

    return {camera.position, Vec3::normalize(forward + right * (x * reach * aspect) + up * (y * reach))};
}
