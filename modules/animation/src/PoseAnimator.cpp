#include "PoseAnimator.hpp"
#include <algorithm>
#include <cmath>

kuge::PoseAnimator::PoseAnimator(std::shared_ptr<const k3::AnimatedModel> model) : m_model(std::move(model)) {}

bool kuge::PoseAnimator::play(std::string_view clip, const ClipPlay& options)
{
    const int index = m_model->findAnimation(clip);

    if (index < 0) {
        return false;
    }
    if (index == m_current.clip && !finished()) {
        m_current.loop = options.loop;
        m_current.speed = options.speed;
        return true;
    }
    m_previous = m_current;
    m_current = Track{index, options.speed < 0.0f ? m_model->animations[static_cast<std::size_t>(index)].duration : 0.0f, options.speed, options.loop};
    m_fadeElapsed = 0.0f;
    m_fadeDuration = m_previous.clip >= 0 ? std::max(options.fade, 0.0f) : 0.0f;
    if (m_fadeDuration <= 0.0f) {
        m_previous.clip = -1;
    }
    m_dirty = true;
    return true;
}

void kuge::PoseAnimator::advance(Track& track, float seconds) const
{
    if (track.clip < 0) {
        return;
    }
    const float duration = m_model->animations[static_cast<std::size_t>(track.clip)].duration;

    track.time += seconds * track.speed;
    if (duration <= 0.0f) {
        track.time = 0.0f;
    } else if (track.loop) {
        track.time = std::fmod(track.time, duration);
        if (track.time < 0.0f) {
            track.time += duration;
        }
    } else {
        track.time = std::clamp(track.time, 0.0f, duration);
    }
}

void kuge::PoseAnimator::update(float seconds)
{
    if (m_current.clip < 0) {
        return;
    }
    advance(m_current, seconds);
    if (m_previous.clip >= 0) {
        advance(m_previous, seconds);
        m_fadeElapsed += seconds;
        if (m_fadeElapsed >= m_fadeDuration) {
            m_previous.clip = -1;
        }
    }
    m_dirty = true;
}

bool kuge::PoseAnimator::finished(void) const noexcept
{
    if (m_current.clip < 0 || m_current.loop) {
        return false;
    }
    const float duration = m_model->animations[static_cast<std::size_t>(m_current.clip)].duration;

    return m_current.speed >= 0.0f ? m_current.time >= duration : m_current.time <= 0.0f;
}

std::string_view kuge::PoseAnimator::clip(void) const noexcept
{
    return m_current.clip < 0 ? std::string_view{} : std::string_view(m_model->animations[static_cast<std::size_t>(m_current.clip)].name);
}

const kuge::Pose& kuge::PoseAnimator::pose(void)
{
    if (m_dirty) {
        m_pose = samplePose(*m_model, m_current.clip, m_current.time);
        if (m_previous.clip >= 0 && m_fadeDuration > 0.0f) {
            m_pose = blend(samplePose(*m_model, m_previous.clip, m_previous.time), m_pose, m_fadeElapsed / m_fadeDuration);
        }
        m_dirty = false;
    }
    return m_pose;
}
