#include "audio/Audio.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

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
        return {0.0f, 0.0f, 1.0f};
    }
    return {1.0f - distance / listener.range, listener.panWidth > 0.0f ? pan(apart.x / listener.panWidth) : 0.0f, 1.0f};
}

void kuge::Audio::setMasterVolume(float volume)
{
    m_master = unit(volume);
    for (const Playing& voice : m_voices) {
        m_backend.setVoice(voice.id, heard(voice), voice.pan, voice.lowpass);
    }
    applyMusicVolume();
}

void kuge::Audio::setBusVolume(Bus bus, float volume)
{
    m_bus[static_cast<int>(bus)] = unit(volume);
    if (bus == Bus::Music) {
        applyMusicVolume();
        return;
    }
    for (const Playing& voice : m_voices) {
        if (voice.bus == bus) {
            m_backend.setVoice(voice.id, heard(voice), voice.pan, voice.lowpass);
        }
    }
}

void kuge::Audio::applyMusicVolume(void)
{
    m_backend.setMusicVolume(musicHeard());
}

void kuge::Audio::reserveVoices(Bus bus, int count)
{
    if (bus != Bus::Dialogue) {
        throw std::invalid_argument("only the voices of the dialogue can be reserved");
    }
    m_reservedVoices = std::max(count, 0);
    m_backend.reserveVoices(m_reservedVoices);
}

void kuge::Audio::setDucking(float musicFactor, float fadeSeconds)
{
    m_duckTo = unit(musicFactor);
    m_duckSeconds = std::isnan(fadeSeconds) ? 0.0f : std::max(fadeSeconds, 0.0f);
}

kuge::Voice kuge::Audio::start(PlayOptions options, const std::function<VoiceId(float, float, float, bool)>& open)
{
    Playing voice{NO_VOICE, unit(options.volume), pan(options.pan), unit(options.lowpass),
        options.bus == Bus::Dialogue ? Bus::Dialogue : Bus::Effects, options.priority, false};

    voice.reserved = voice.bus == Bus::Dialogue && m_reservedVoices > 0;
    voice.id = open(heard(voice), voice.pan, voice.lowpass, voice.reserved);
    if (voice.id == NO_VOICE) {
        // Full: a sound that matters more takes the place of one that matters less (the quietest of the lowest priority)
        prune();
        auto victim = m_voices.end();

        for (auto it = m_voices.begin(); it != m_voices.end(); ++it) {
            const bool lessImportant = it->priority < voice.priority || (it->priority == voice.priority && heard(*it) < heard(voice));

            if (it->reserved == voice.reserved && lessImportant
                && (victim == m_voices.end() || it->priority < victim->priority
                    || (it->priority == victim->priority && heard(*it) < heard(*victim)))) {
                victim = it;
            }
        }
        if (victim == m_voices.end()) {
            return {};
        }
        m_backend.stop(victim->id);
        m_voices.erase(victim);
        voice.id = open(heard(voice), voice.pan, voice.lowpass, voice.reserved);
        if (voice.id == NO_VOICE) {
            return {};
        }
    }
    if (voice.lowpass < 1.0f) {
        m_backend.setVoice(voice.id, heard(voice), voice.pan, voice.lowpass);
    }
    m_voices.push_back(voice);
    return {voice.id};
}

kuge::Voice kuge::Audio::play(const Sound& sound, PlayOptions options)
{
    const bool loop = options.loop;

    return start(options, [&](float volume, float side, float, bool reserved) {
        return m_backend.play(sound.id(), volume, side, loop, reserved);
    });
}

kuge::Voice kuge::Audio::play(const Stream& stream, PlayOptions options)
{
    return start(options, [&](float volume, float side, float, bool reserved) {
        return m_backend.playStream(stream.id(), volume, side, reserved);
    });
}

std::shared_ptr<kuge::Stream> kuge::Audio::createStream(int sampleRate, int channels)
{
    return std::make_shared<Stream>(m_backend, m_backend.createStream(sampleRate, channels));
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

void kuge::Audio::setVoice(Voice voice, float volume, float side, float lowpass)
{
    for (Playing& playing : m_voices) {
        if (playing.id == voice.id) {
            playing.volume = unit(volume);
            playing.pan = pan(side);
            playing.lowpass = unit(lowpass);
            m_backend.setVoice(playing.id, heard(playing), playing.pan, playing.lowpass);
            return;
        }
    }
}

void kuge::Audio::playMusic(const Music& music, MusicOptions options)
{
    m_musicVolume = unit(options.volume);
    m_onMusicFinished = std::move(options.onFinished);
    m_backend.playMusic(music.id(), musicHeard(), options.loop, std::max(options.fadeIn, 0.0f));
}

void kuge::Audio::stopMusic(float fadeOut)
{
    m_backend.stopMusic(std::max(fadeOut, 0.0f));
}

// A voice that ended is told to the backend too: a stream voice is only over for us (see IAudio::playStream)
void kuge::Audio::prune(void)
{
    m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(), [this](const Playing& p) {
        if (m_backend.playing(p.id)) {
            return false;
        }
        m_backend.stop(p.id);
        return true;
    }), m_voices.end());
}

void kuge::Audio::update(double seconds)
{
    prune();
    if (m_backend.takeMusicFinished() && m_onMusicFinished) {
        const auto done = std::move(m_onMusicFinished);   // it may call playMusic(), which sets a new one

        m_onMusicFinished = nullptr;
        done();
    }
    const bool speaking = std::any_of(m_voices.begin(), m_voices.end(), [](const Playing& p) { return p.bus == Bus::Dialogue; });
    const float target = speaking ? m_duckTo : 1.0f;

    if (m_duck != target) {
        const float step = m_duckSeconds > 0.0f ? static_cast<float>(seconds) * std::fabs(1.0f - m_duckTo) / m_duckSeconds : 1.0f;

        m_duck = m_duck < target ? std::min(m_duck + step, target) : std::max(m_duck - step, target);
        applyMusicVolume();
    }
}
