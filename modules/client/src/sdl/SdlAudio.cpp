#include "backend/SdlBackend.hpp"
#include "audio/LowPass.hpp"
#include "sdl/SdlPath.hpp"
#include <SDL.h>
#include <SDL_mixer.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <deque>
#include <format>
#include <map>
#include <memory>
#include <mutex>

namespace
{
    using namespace kuge;

    constexpr int CHANNELS = 32;   // sounds at the same time
    static_assert(CHANNELS <= 64, "a voice number holds its channel in 6 bits");

    int mixVolume(float volume)
    {
        return static_cast<int>(std::lround(std::clamp(volume, 0.0f, 1.0f) * MIX_MAX_VOLUME));
    }

    constexpr int MIX_RATE = 44100;                        // what Mix_OpenAudio is asked for
    constexpr std::size_t STREAM_SECONDS = 5;              // what a stream holds: older samples are dropped

    // The low-pass of a channel of the mixer (stereo, 16 bits): the audio thread reads `alpha`, the game writes it
    struct Filter
    {
        std::atomic<float> alpha{1.0f};   // 1: nothing is cut
        LowPassFilter      state;

        void set(float lowpass) { alpha = lowpassAlpha(lowpass, MIX_RATE); }
    };

    void filterEffect(int, void* stream, int length, void* udata)
    {
        auto* filter = static_cast<Filter*>(udata);

        filter->state.process(static_cast<std::int16_t*>(stream), static_cast<std::size_t>(length) / 2, filter->alpha);
    }

    // Samples that arrive while they are played. The mixer asks for them (from its own thread) with
    // streamEffect, which replaces the silence of a looping chunk by them.
    struct StreamState
    {
        int                        sampleRate = MIX_RATE;
        int                        channels = 1;
        mutable std::mutex         mutex;
        std::deque<std::int16_t>   samples;     // interleaved, what was pushed and not played
        double                     phase = 0.0; // between two frames, in frames of the stream
        bool                       dry = false; // the mixer found nothing left to play

        std::size_t frames(void) const { return samples.size() / static_cast<std::size_t>(channels); }
    };

    void streamEffect(int, void* stream, int length, void* udata)
    {
        auto* state = static_cast<StreamState*>(udata);
        auto* out = static_cast<std::int16_t*>(stream);
        const int frames = length / 4;
        const double step = static_cast<double>(state->sampleRate) / MIX_RATE;
        const std::size_t channels = static_cast<std::size_t>(state->channels);
        std::lock_guard<std::mutex> lock(state->mutex);

        for (int i = 0; i < frames; ++i) {
            if (state->frames() == 0) {
                out[2 * i] = out[2 * i + 1] = 0;   // (nothing left: silence, and the voice is over)
                state->dry = true;
                continue;
            }
            // Between this frame and the next one (or this one again, at the end of what we have)
            const double t = state->phase;
            const std::size_t next = state->frames() > 1 ? channels : 0;
            const auto mix = [&](std::size_t channel) {
                const double a = state->samples[channel];
                const double b = state->samples[next + channel];

                return static_cast<std::int16_t>(a + (b - a) * t);
            };

            out[2 * i] = mix(0);
            out[2 * i + 1] = mix(channels - 1);
            state->phase += step;
            while (state->phase >= 1.0 && !state->samples.empty()) {
                state->samples.erase(state->samples.begin(), state->samples.begin() + static_cast<std::ptrdiff_t>(channels));
                state->phase -= 1.0;
            }
            if (state->frames() == 0) {
                state->phase = 0.0;
            }
        }
    }

    class SdlAudio : public IAudio
    {
        public:
            SdlAudio(void)
            {
                if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
                    throw AudioError(std::format("cannot start the audio: {}", SDL_GetError()));
                }
                // Formats beyond WAV are a bonus: not having a decoder is not a reason to stop
                Mix_Init(MIX_INIT_OGG | MIX_INIT_MP3 | MIX_INIT_FLAC);
                if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) != 0) {
                    const std::string why = Mix_GetError();

                    Mix_Quit();
                    SDL_QuitSubSystem(SDL_INIT_AUDIO);
                    throw AudioError(std::format("cannot open the audio device: {}", why));
                }
                Mix_AllocateChannels(CHANNELS);
                m_voiceOfChannel.fill(NO_VOICE);
                // A second of silence, for the voices of the streams to loop on
                m_silence.assign(static_cast<std::size_t>(MIX_RATE) * 4, 0);
                m_silentChunk = Mix_QuickLoad_RAW(m_silence.data(), static_cast<Uint32>(m_silence.size()));
            }

            ~SdlAudio(void) override
            {
                Mix_HaltChannel(-1);
                Mix_HaltMusic();
                if (m_silentChunk) {
                    Mix_FreeChunk(m_silentChunk);
                }
                for (auto& [id, chunk] : m_sounds) {
                    Mix_FreeChunk(chunk);
                }
                for (auto& [id, music] : m_musics) {
                    Mix_FreeMusic(music);
                }
                Mix_CloseAudio();
                Mix_Quit();
                SDL_QuitSubSystem(SDL_INIT_AUDIO);
            }

            SdlAudio(const SdlAudio&)            = delete;
            SdlAudio& operator=(const SdlAudio&) = delete;

            SoundId loadSound(const std::filesystem::path& file) override
            {
                Mix_Chunk* chunk = Mix_LoadWAV(sdlPath(file).c_str());

                if (!chunk) {
                    throw AudioError(std::format("cannot load the sound '{}': {}", file.string(), Mix_GetError()));
                }
                m_sounds[++m_lastSound] = chunk;
                return m_lastSound;
            }

            void destroySound(SoundId sound) noexcept override
            {
                const auto found = m_sounds.find(sound);

                if (found == m_sounds.end()) {
                    return;
                }
                // A chunk that plays must not be freed under the mixer
                for (int channel = 0; channel < CHANNELS; ++channel) {
                    if (Mix_GetChunk(channel) == found->second) {
                        Mix_HaltChannel(channel);
                    }
                }
                Mix_FreeChunk(found->second);
                m_sounds.erase(found);
            }

            MusicId loadMusic(const std::filesystem::path& file) override
            {
                Mix_Music* music = Mix_LoadMUS(sdlPath(file).c_str());

                if (!music) {
                    throw AudioError(std::format("cannot load the music '{}': {}", file.string(), Mix_GetError()));
                }
                m_musics[++m_lastMusic] = music;
                return m_lastMusic;
            }

            void destroyMusic(MusicId music) noexcept override
            {
                const auto found = m_musics.find(music);

                if (found == m_musics.end()) {
                    return;
                }
                if (m_playingMusic == music) {
                    Mix_HaltMusic();
                    m_playingMusic = NO_MUSIC;
                }
                Mix_FreeMusic(found->second);
                m_musics.erase(found);
            }

            // A voice is a channel and a counter: when the channel is used again
            // for another sound, the old voice is not that channel any more
            using IAudio::play;
            using IAudio::setVoice;

            VoiceId play(SoundId sound, float volume, float pan, bool loop, bool reserved) override
            {
                const auto found = m_sounds.find(sound);

                if (found == m_sounds.end()) {
                    return NO_VOICE;
                }
                const int channel = playOn(found->second, loop, reserved);

                if (channel < 0) {
                    return NO_VOICE;
                }
                m_streamOfChannel[static_cast<std::size_t>(channel)] = NO_STREAM;
                return start(channel, volume, pan);
            }

            void setVoice(VoiceId voice, float volume, float pan, float lowpass) override
            {
                if (const int channel = channelOf(voice); channel >= 0) {
                    m_filters[static_cast<std::size_t>(channel)].set(lowpass);
                    apply(channel, volume, pan);
                }
            }

            void reserveVoices(int count) override
            {
                m_reserved = std::clamp(count, 0, CHANNELS - 1);
                Mix_ReserveChannels(m_reserved);
            }

            StreamId createStream(int sampleRate, int channels) override
            {
                if (sampleRate <= 0 || (channels != 1 && channels != 2)) {
                    throw AudioError("a stream needs a positive rate and one or two channels");
                }
                auto state = std::make_unique<StreamState>();

                state->sampleRate = sampleRate;
                state->channels = channels;
                m_streams[++m_lastStream] = std::move(state);
                return m_lastStream;
            }

            void destroyStream(StreamId stream) noexcept override
            {
                const auto found = m_streams.find(stream);

                if (found == m_streams.end()) {
                    return;
                }
                // The mixer must not be using it: its channel goes first
                for (int channel = 0; channel < CHANNELS; ++channel) {
                    if (m_streamOfChannel[static_cast<std::size_t>(channel)] == stream) {
                        Mix_HaltChannel(channel);
                        m_streamOfChannel[static_cast<std::size_t>(channel)] = NO_STREAM;
                    }
                }
                m_streams.erase(found);
            }

            void pushSamples(StreamId stream, std::span<const std::int16_t> samples) override
            {
                const auto found = m_streams.find(stream);

                if (found == m_streams.end()) {
                    return;
                }
                StreamState& state = *found->second;
                const std::size_t limit = static_cast<std::size_t>(state.sampleRate) * static_cast<std::size_t>(state.channels) * STREAM_SECONDS;
                std::lock_guard<std::mutex> lock(state.mutex);

                state.samples.insert(state.samples.end(), samples.begin(), samples.end());
                if (state.samples.size() > limit) {
                    // Whole frames, the oldest first: a voice that is late catches up instead of growing a delay
                    const std::size_t channels = static_cast<std::size_t>(state.channels);
                    const std::size_t extra = (state.samples.size() - limit + channels - 1) / channels * channels;

                    state.samples.erase(state.samples.begin(), state.samples.begin() + static_cast<std::ptrdiff_t>(extra));
                }
            }

            std::size_t bufferedFrames(StreamId stream) const override
            {
                const auto found = m_streams.find(stream);

                if (found == m_streams.end()) {
                    return 0;
                }
                std::lock_guard<std::mutex> lock(found->second->mutex);
                return found->second->frames();
            }

            VoiceId playStream(StreamId stream, float volume, float pan, bool reserved) override
            {
                const auto found = m_streams.find(stream);

                if (found == m_streams.end() || !m_silentChunk || bufferedFrames(stream) == 0) {
                    return NO_VOICE;
                }
                for (int channel = 0; channel < CHANNELS; ++channel) {
                    if (m_streamOfChannel[static_cast<std::size_t>(channel)] == stream && Mix_Playing(channel)) {
                        std::lock_guard<std::mutex> lock(found->second->mutex);

                        if (!found->second->dry) {
                            return NO_VOICE;   // one voice per stream: the one that plays takes what comes
                        }
                        Mix_HaltChannel(channel);   // (the old one ran dry: it makes room for the new one)
                    }
                }
                const int channel = playOn(m_silentChunk, true, reserved);

                if (channel < 0) {
                    return NO_VOICE;
                }
                {
                    std::lock_guard<std::mutex> lock(found->second->mutex);
                    found->second->dry = false;
                }
                m_streamOfChannel[static_cast<std::size_t>(channel)] = stream;
                // Before the filter and the panning (they come after it in the chain)
                Mix_RegisterEffect(channel, streamEffect, nullptr, found->second.get());
                return start(channel, volume, pan);
            }

            void stop(VoiceId voice) override
            {
                if (const int channel = channelOf(voice); channel >= 0) {
                    Mix_HaltChannel(channel);
                }
            }

            bool playing(VoiceId voice) const override
            {
                const int channel = channelOf(voice);

                if (channel < 0 || Mix_Playing(channel) == 0) {
                    return false;
                }
                // A voice of a stream is over when the mixer found nothing left (the silence it loops on is not a sound)
                if (const StreamId stream = m_streamOfChannel[static_cast<std::size_t>(channel)]; stream != NO_STREAM) {
                    const auto found = m_streams.find(stream);

                    if (found == m_streams.end()) {
                        return false;
                    }
                    std::lock_guard<std::mutex> lock(found->second->mutex);
                    return !found->second->dry;
                }
                return true;
            }

            void playMusic(MusicId music, float volume, bool loop, float fadeInSeconds) override
            {
                const auto found = m_musics.find(music);

                if (found == m_musics.end()) {
                    return;
                }
                Mix_VolumeMusic(mixVolume(volume));
                if (fadeInSeconds > 0.0f) {
                    Mix_FadeInMusic(found->second, loop ? -1 : 1, static_cast<int>(fadeInSeconds * 1000.0f));
                } else {
                    Mix_PlayMusic(found->second, loop ? -1 : 1);
                }
                m_playingMusic = music;
            }

            void setMusicVolume(float volume) override { Mix_VolumeMusic(mixVolume(volume)); }

            void stopMusic(float fadeOutSeconds) override
            {
                if (fadeOutSeconds > 0.0f) {
                    Mix_FadeOutMusic(static_cast<int>(fadeOutSeconds * 1000.0f));
                } else {
                    Mix_HaltMusic();
                }
            }

            bool musicPlaying(void) const override { return Mix_PlayingMusic() != 0; }

        private:
            // The first free channel of the pool: the reserved ones, or the others
            int playOn(Mix_Chunk* chunk, bool loop, bool reserved)
            {
                if (!reserved) {
                    return Mix_PlayChannel(-1, chunk, loop ? -1 : 0);   // (SDL_mixer leaves the reserved ones alone)
                }
                for (int channel = 0; channel < m_reserved; ++channel) {
                    if (Mix_Playing(channel) == 0) {
                        return Mix_PlayChannel(channel, chunk, loop ? -1 : 0);
                    }
                }
                return -1;
            }

            // The voice of a channel that just started: its filter and its place
            VoiceId start(int channel, float volume, float pan)
            {
                const VoiceId voice = ++m_counter * 64 + static_cast<VoiceId>(channel);
                Filter& filter = m_filters[static_cast<std::size_t>(channel)];

                m_voiceOfChannel[static_cast<std::size_t>(channel)] = voice;
                filter.set(1.0f);
                filter.state = {};
                Mix_RegisterEffect(channel, filterEffect, nullptr, &filter);
                apply(channel, volume, pan);
                return voice;
            }

            int channelOf(VoiceId voice) const
            {
                if (voice == NO_VOICE) {
                    return -1;
                }
                const int channel = static_cast<int>(voice & 63u);

                return channel < CHANNELS && m_voiceOfChannel[static_cast<std::size_t>(channel)] == voice ? channel : -1;
            }

            // 0 to 255 for each ear: a pan of -1 is all on the left
            static void apply(int channel, float volume, float pan)
            {
                const float side = std::clamp(pan, -1.0f, 1.0f);
                const float left = side <= 0.0f ? 1.0f : 1.0f - side;
                const float right = side >= 0.0f ? 1.0f : 1.0f + side;

                Mix_Volume(channel, mixVolume(volume));
                Mix_SetPanning(channel, static_cast<Uint8>(std::lround(left * 255.0f)), static_cast<Uint8>(std::lround(right * 255.0f)));
            }

            std::map<SoundId, Mix_Chunk*>              m_sounds;
            std::map<MusicId, Mix_Music*>              m_musics;
            std::array<VoiceId, CHANNELS>              m_voiceOfChannel{};
            std::array<StreamId, CHANNELS>             m_streamOfChannel{};
            std::array<Filter, CHANNELS>               m_filters;
            std::map<StreamId, std::unique_ptr<StreamState>> m_streams;
            std::vector<std::uint8_t>                  m_silence;
            Mix_Chunk*                                 m_silentChunk = nullptr;
            StreamId                                   m_lastStream = 0;
            int                                        m_reserved = 0;
            SoundId                                    m_lastSound = 0;
            MusicId                                    m_lastMusic = 0;
            MusicId                                    m_playingMusic = NO_MUSIC;
            VoiceId                                    m_counter = 0;
    };
}

std::unique_ptr<kuge::IAudio> kuge::makeSdlAudio(void)
{
    return std::make_unique<SdlAudio>();
}
