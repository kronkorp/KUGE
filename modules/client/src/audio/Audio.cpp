#include "audio/Audio.hpp"
#include <algorithm>
#include <cmath>

namespace
{
    float unit(float value) noexcept
    {
        return std::isnan(value) ? 0.0f : std::clamp(value, 0.0f, 1.0f);
    }

    float pan(float value) noexcept
    {
        return std::isnan(value) ? 0.0f : std::clamp(value, -1.0f, 1.0f);
    }
}

kuge::Spatial kuge::spatialize(Vec2 source, const Listener& listener) noexcept
{
    const Vec2 apart = source - listener.position;
    const float distance = apart.length();

    if (!(listener.range > 0.0f) || distance >= listener.range) {
        return {0.0f, 0.0f};
    }
    return {1.0f - distance / listener.range, listener.panWidth > 0.0f ? pan(apart.x / listener.panWidth) : 0.0f};
}

void kuge::Audio::setMasterVolume(float volume)
{
    m_master = unit(volume);
    for (const Playing& voice : m_voices) {
        m_backend.setVoice(voice.id, heard(voice.volume), voice.pan);
    }
    applyMusicVolume();
}

void kuge::Audio::setBusVolume(Bus bus, float volume)
{
    m_bus[static_cast<int>(bus)] = unit(volume);
    if (bus == Bus::Effects) {
        for (const Playing& voice : m_voices) {
            m_backend.setVoice(voice.id, heard(voice.volume), voice.pan);
        }
    } else {
        applyMusicVolume();
    }
}

void kuge::Audio::applyMusicVolume(void)
{
    m_backend.setMusicVolume(m_master * m_bus[static_cast<int>(Bus::Music)] * m_musicVolume);
}

kuge::Voice kuge::Audio::play(const Sound& sound, PlayOptions options)
{
    const float volume = unit(options.volume);
    const float side = pan(options.pan);
    const VoiceId id = m_backend.play(sound.id(), heard(volume), side, options.loop);

    if (id == NO_VOICE) {
        return {};
    }
    m_voices.push_back({id, volume, side});
    return {id};
}

kuge::Voice kuge::Audio::playAt(const Sound& sound, Vec2 source, const Listener& listener, PlayOptions options)
{
    const Spatial where = spatialize(source, listener);

    if (where.volume <= 0.0f) {
        return {};   // out of hearing: no need to take a voice
    }
    options.volume = unit(options.volume) * where.volume;
    options.pan = where.pan;
    return play(sound, options);
}

void kuge::Audio::stop(Voice voice)
{
    if (voice) {
        m_backend.stop(voice.id);
        m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(), [&](const Playing& p) { return p.id == voice.id; }),
            m_voices.end());
    }
}

bool kuge::Audio::playing(Voice voice) const
{
    return voice && m_backend.playing(voice.id);
}

void kuge::Audio::setVoice(Voice voice, float volume, float side)
{
    for (Playing& playing : m_voices) {
        if (playing.id == voice.id) {
            playing.volume = unit(volume);
            playing.pan = pan(side);
            m_backend.setVoice(playing.id, heard(playing.volume), playing.pan);
            return;
        }
    }
}

void kuge::Audio::playMusic(const Music& music, MusicOptions options)
{
    m_musicVolume = unit(options.volume);
    m_backend.playMusic(music.id(), m_master * m_bus[static_cast<int>(Bus::Music)] * m_musicVolume, options.loop,
        std::max(options.fadeIn, 0.0f));
}

void kuge::Audio::stopMusic(float fadeOut)
{
    m_backend.stopMusic(std::max(fadeOut, 0.0f));
}

void kuge::Audio::update(void)
{
    m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(), [this](const Playing& p) { return !m_backend.playing(p.id); }),
        m_voices.end());
}
