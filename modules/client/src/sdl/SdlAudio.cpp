#include "backend/SdlBackend.hpp"
#include "sdl/SdlPath.hpp"
#include <SDL.h>
#include <SDL_mixer.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>

namespace
{
    using namespace kuge;

    constexpr int CHANNELS = 32;   // sounds at the same time
    static_assert(CHANNELS <= 64, "a voice number holds its channel in 6 bits");

    int mixVolume(float volume)
    {
        return static_cast<int>(std::lround(std::clamp(volume, 0.0f, 1.0f) * MIX_MAX_VOLUME));
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
            }

            ~SdlAudio(void) override
            {
                Mix_HaltChannel(-1);
                Mix_HaltMusic();
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
            VoiceId play(SoundId sound, float volume, float pan, bool loop) override
            {
                const auto found = m_sounds.find(sound);

                if (found == m_sounds.end()) {
                    return NO_VOICE;
                }
                const int channel = Mix_PlayChannel(-1, found->second, loop ? -1 : 0);

                if (channel < 0) {
                    return NO_VOICE;
                }
                const VoiceId voice = ++m_counter * 64 + static_cast<VoiceId>(channel);

                m_voiceOfChannel[static_cast<std::size_t>(channel)] = voice;
                apply(channel, volume, pan);
                return voice;
            }

            void setVoice(VoiceId voice, float volume, float pan) override
            {
                if (const int channel = channelOf(voice); channel >= 0) {
                    apply(channel, volume, pan);
                }
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

                return channel >= 0 && Mix_Playing(channel) != 0;
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
