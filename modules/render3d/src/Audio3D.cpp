#include "Audio3D.hpp"
#include <algorithm>
#include <cmath>

kuge::Listener3D kuge::listenerOf(const k3::Camera& camera, float range, float panWidth) noexcept
{
    return {camera.position, camera.right(), range, panWidth};
}

kuge::Spatial kuge::spatialize(k3::Math::Vector3f source, const Listener3D& listener) noexcept
{
    const k3::Math::Vector3f apart = source - listener.position;
    const float distance = k3::Math::Vector3f::length(apart);

    if (!(listener.range > 0.0f) || distance >= listener.range) {
        return {0.0f, 0.0f};
    }
    const float side = apart.x * listener.right.x + apart.y * listener.right.y + apart.z * listener.right.z;

    return {1.0f - distance / listener.range, listener.panWidth > 0.0f ? std::clamp(side / listener.panWidth, -1.0f, 1.0f) : 0.0f};
}

kuge::Voice kuge::playAt(Audio& audio, const Sound& sound, k3::Math::Vector3f source, const Listener3D& listener, PlayOptions options)
{
    const Spatial where = spatialize(source, listener);

    if (where.volume <= 0.0f) {
        return {};      // out of hearing: no need to take a voice
    }
    options.volume = std::clamp(options.volume, 0.0f, 1.0f) * where.volume;
    options.pan = where.pan;
    return audio.play(sound, options);
}
