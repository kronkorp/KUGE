#include "audio/DummyAudio.hpp"
#include <algorithm>
#include <format>

kuge::SoundId kuge::DummyAudio::loadSound(const std::filesystem::path& file)
{
    if (!std::filesystem::exists(file)) {
        throw AudioError(std::format("cannot open the sound '{}'", file.string()));
    }
    m_sounds[m_nextSound] = file.string();
    return m_nextSound++;
}

void kuge::DummyAudio::destroySound(SoundId sound) noexcept
{
    m_sounds.erase(sound);
    for (auto& [id, voice] : m_voices) {
        if (voice.sound == sound) {
            voice.active = false;
        }
    }
}

kuge::MusicId kuge::DummyAudio::loadMusic(const std::filesystem::path& file)
{
    if (!std::filesystem::exists(file)) {
        throw AudioError(std::format("cannot open the music '{}'", file.string()));
    }
    m_musics[m_nextMusic] = file.string();
    return m_nextMusic++;
}

void kuge::DummyAudio::destroyMusic(MusicId music) noexcept
{
    m_musics.erase(music);
    if (m_music.music == music) {
        m_music.playing = false;
    }
}

kuge::VoiceId kuge::DummyAudio::play(SoundId sound, float volume, float pan, bool loop)
{
    if (m_sounds.count(sound) == 0 || activeVoices() >= m_maxVoices) {
        return NO_VOICE;
    }
    m_voices[m_nextVoice] = Voice{sound, volume, pan, loop};
    return m_nextVoice++;
}

void kuge::DummyAudio::setVoice(VoiceId voice, float volume, float pan)
{
    const auto found = m_voices.find(voice);

    if (found != m_voices.end()) {
        found->second.volume = volume;
        found->second.pan = pan;
    }
}

void kuge::DummyAudio::stop(VoiceId voice)
{
    finish(voice);
}

bool kuge::DummyAudio::playing(VoiceId voice) const
{
    const auto found = m_voices.find(voice);

    return found != m_voices.end() && found->second.active;
}

void kuge::DummyAudio::playMusic(MusicId music, float volume, bool loop, float fadeInSeconds)
{
    if (m_musics.count(music) == 0) {
        return;
    }
    m_music = MusicState{music, volume, loop, fadeInSeconds, 0.0f, true};
}

void kuge::DummyAudio::setMusicVolume(float volume)
{
    m_music.volume = volume;
}

void kuge::DummyAudio::stopMusic(float fadeOutSeconds)
{
    m_music.fadeOut = fadeOutSeconds;
    m_music.playing = false;
}

const kuge::DummyAudio::Voice* kuge::DummyAudio::voice(VoiceId id) const
{
    const auto found = m_voices.find(id);

    return found == m_voices.end() ? nullptr : &found->second;
}

std::size_t kuge::DummyAudio::activeVoices(void) const
{
    return static_cast<std::size_t>(std::count_if(m_voices.begin(), m_voices.end(), [](const auto& v) { return v.second.active; }));
}

void kuge::DummyAudio::finish(VoiceId id)
{
    const auto found = m_voices.find(id);

    if (found != m_voices.end()) {
        found->second.active = false;
    }
}
