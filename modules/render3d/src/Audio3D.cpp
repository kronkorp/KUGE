#include "Audio3D.hpp"
#include <algorithm>
#include <cmath>

kuge::Listener3D kuge::listenerOf(const k3::Camera& camera, float range, float panWidth) noexcept
{
    return {camera.position, camera.right(), range, panWidth, camera.forward(), 1.0f};
}

kuge::Spatial kuge::spatialize(k3::Math::Vector3f source, const Listener3D& listener, float occlusion) noexcept
{
    // First numbers, to be heard and tuned: how much the highs and the volume go for what is behind, far, or in the way
    constexpr float BEHIND_VOLUME = 0.2f;
    constexpr float BEHIND_HIGHS = 0.5f;
    constexpr float FAR_HIGHS = 0.4f;
    constexpr float WALL_VOLUME = 0.5f;
    constexpr float WALL_HIGHS = 0.7f;

    const k3::Math::Vector3f apart = source - listener.position;
    const float distance = k3::Math::Vector3f::length(apart);

    if (!(listener.range > 0.0f) || distance >= listener.range) {
        return {0.0f, 0.0f, 1.0f};
    }
    const float side = apart.x * listener.right.x + apart.y * listener.right.y + apart.z * listener.right.z;
    const float far = distance / listener.range;
    // 0 ahead and at the sides, 1 right behind (on the spot, there is no behind)
    const float behind = distance > 1e-4f
        ? std::max(0.0f, -(apart.x * listener.forward.x + apart.y * listener.forward.y + apart.z * listener.forward.z) / distance)
        : 0.0f;
    const float blocked = std::clamp(occlusion, 0.0f, 1.0f);

    Spatial result;

    result.volume = std::pow(1.0f - far, std::max(listener.curve, 0.01f)) * (1.0f - BEHIND_VOLUME * behind) * (1.0f - WALL_VOLUME * blocked);
    result.pan = listener.panWidth > 0.0f ? std::clamp(side / listener.panWidth, -1.0f, 1.0f) : 0.0f;
    result.lowpass = (1.0f - FAR_HIGHS * far) * (1.0f - BEHIND_HIGHS * behind) * (1.0f - WALL_HIGHS * blocked);
    return result;
}

kuge::Voice kuge::playAt(Audio& audio, const Sound& sound, k3::Math::Vector3f source, const Listener3D& listener, PlayOptions options,
    float occlusion)
{
    const Spatial where = spatialize(source, listener, occlusion);

    if (where.volume <= 0.0f) {
        return {};      // out of hearing: no need to take a voice
    }
    options.volume = std::clamp(options.volume, 0.0f, 1.0f) * where.volume;
    options.pan = where.pan;
    options.lowpass = std::clamp(options.lowpass, 0.0f, 1.0f) * where.lowpass;
    return audio.play(sound, options);
}

bool kuge::moveVoice(Audio& audio, Voice voice, k3::Math::Vector3f source, const Listener3D& listener, float volume, float occlusion)
{
    const Spatial where = spatialize(source, listener, occlusion);

    audio.setVoice(voice, std::clamp(volume, 0.0f, 1.0f) * where.volume, where.pan, where.lowpass);
    return where.volume > 0.0f;
}
