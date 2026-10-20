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

// The voices are two pools: `m_reserved` for the reserved sounds, the rest of `m_maxVoices` for the others
std::size_t kuge::DummyAudio::freeIn(bool reserved) const
{
    const std::size_t size = reserved ? m_reserved : (m_maxVoices > m_reserved ? m_maxVoices - m_reserved : 0);
    const auto used = static_cast<std::size_t>(std::count_if(m_voices.begin(), m_voices.end(), [&](const auto& v) {
        return v.second.active && v.second.reserved == reserved;
    }));

    return used < size ? size - used : 0;
}

kuge::VoiceId kuge::DummyAudio::play(SoundId sound, float volume, float pan, bool loop, bool reserved)
{
    if (m_sounds.count(sound) == 0 || freeIn(reserved) == 0) {
        return NO_VOICE;
    }
    m_voices[m_nextVoice] = Voice{sound, volume, pan, loop, true, reserved};
    return m_nextVoice++;
}

void kuge::DummyAudio::setVoice(VoiceId voice, float volume, float pan, float lowpass)
{
    const auto found = m_voices.find(voice);

    if (found != m_voices.end()) {
        found->second.volume = volume;
        found->second.pan = pan;
        found->second.lowpass = lowpass;
    }
}

kuge::StreamId kuge::DummyAudio::createStream(int sampleRate, int channels)
{
    if (sampleRate <= 0 || (channels != 1 && channels != 2)) {
        throw AudioError("a stream needs a positive rate and one or two channels");
    }
    m_streams[m_nextStream] = Stream{sampleRate, channels, {}};
    return m_nextStream++;
}

void kuge::DummyAudio::destroyStream(StreamId stream) noexcept
{
    m_streams.erase(stream);
}

void kuge::DummyAudio::pushSamples(StreamId stream, std::span<const std::int16_t> samples)
{
    const auto found = m_streams.find(stream);

    if (found != m_streams.end()) {
        found->second.samples.insert(found->second.samples.end(), samples.begin(), samples.end());
    }
}

std::size_t kuge::DummyAudio::bufferedFrames(StreamId stream) const
{
    const auto found = m_streams.find(stream);

    return found == m_streams.end() ? 0 : found->second.samples.size() / static_cast<std::size_t>(found->second.channels);
}

kuge::VoiceId kuge::DummyAudio::playStream(StreamId stream, float volume, float pan, bool reserved)
{
    if (bufferedFrames(stream) == 0 || freeIn(reserved) == 0) {
        return NO_VOICE;
    }
    Voice voice{NO_SOUND, volume, pan, false, true, reserved};

    voice.stream = stream;
    m_voices[m_nextVoice] = voice;
    return m_nextVoice++;
}

void kuge::DummyAudio::consume(StreamId stream, std::size_t frames)
{
    const auto found = m_streams.find(stream);

    if (found != m_streams.end()) {
        auto& samples = found->second.samples;
        const std::size_t count = std::min(samples.size(), frames * static_cast<std::size_t>(found->second.channels));

        samples.erase(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(count));
    }
}

const kuge::DummyAudio::Stream* kuge::DummyAudio::stream(StreamId id) const
{
    const auto found = m_streams.find(id);

    return found == m_streams.end() ? nullptr : &found->second;
}

void kuge::DummyAudio::stop(VoiceId voice)
{
    finish(voice);
}

bool kuge::DummyAudio::playing(VoiceId voice) const
{
    const auto found = m_voices.find(voice);

    if (found == m_voices.end() || !found->second.active) {
        return false;
    }
    return found->second.stream == NO_STREAM || bufferedFrames(found->second.stream) > 0;   // a stream plays while it has frames
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
