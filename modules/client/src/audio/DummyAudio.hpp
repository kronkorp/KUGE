#pragma once

#include "audio/IAudio.hpp"
#include <map>
#include <string>
#include <vector>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  An audio backend that plays nothing and remembers what it was
     *         asked: for tests, and for a machine that has no sound
     *
     * A sound "exists" if its file does, so that a wrong name is still found.
     * A voice plays until finish() says it ended (or stop()).
     */
    ////////////////////////////////////////////////////////////////////////////
    class DummyAudio : public IAudio
    {
        public:
            struct Voice
            {
                SoundId  sound;
                float    volume;
                float    pan;
                bool     loop;
                bool     active   = true;
                bool     reserved = false;
                float    lowpass  = 1.0f;
                StreamId stream   = NO_STREAM;   //!< A voice of a stream: it plays while the stream has frames
            };

            struct Stream
            {
                int                       sampleRate = 0;
                int                       channels   = 1;
                std::vector<std::int16_t> samples;   //!< What was pushed and not consumed
            };

            struct MusicState
            {
                MusicId music   = NO_MUSIC;
                float   volume  = 0.0f;
                bool    loop    = false;
                float   fadeIn  = 0.0f;
                float   fadeOut = 0.0f;
                bool    playing = false;
            };

            SoundId loadSound(const std::filesystem::path& file) override;
            void    destroySound(SoundId sound) noexcept override;
            MusicId loadMusic(const std::filesystem::path& file) override;
            void    destroyMusic(MusicId music) noexcept override;
            using IAudio::play;
            using IAudio::setVoice;
            VoiceId play(SoundId sound, float volume, float pan, bool loop, bool reserved) override;
            void    setVoice(VoiceId voice, float volume, float pan, float lowpass) override;
            void    reserveVoices(int count) override { m_reserved = static_cast<std::size_t>(count < 0 ? 0 : count); }
            StreamId createStream(int sampleRate, int channels) override;
            void     destroyStream(StreamId stream) noexcept override;
            void     pushSamples(StreamId stream, std::span<const std::int16_t> samples) override;
            std::size_t bufferedFrames(StreamId stream) const override;
            VoiceId  playStream(StreamId stream, float volume, float pan, bool reserved) override;
            void    stop(VoiceId voice) override;
            bool    playing(VoiceId voice) const override;
            void    playMusic(MusicId music, float volume, bool loop, float fadeInSeconds) override;
            void    setMusicVolume(float volume) override;
            void    stopMusic(float fadeOutSeconds) override;
            bool    musicPlaying(void) const override { return m_music.playing; }

            // -- What tests look at -------------------------------------------------------------
            const Voice*      voice(VoiceId id) const;
            std::size_t       activeVoices(void) const;
            const MusicState& music(void) const noexcept { return m_music; }
            std::size_t       soundCount(void) const noexcept { return m_sounds.size(); }
            std::size_t       musicCount(void) const noexcept { return m_musics.size(); }

            //! Ends a voice, as if the sound was over
            void finish(VoiceId id);

            //! The mixer takes this many frames of a stream, as if they were played
            void consume(StreamId stream, std::size_t frames);
            const Stream* stream(StreamId id) const;

            //! At most this many voices at once (default 32)
            void setMaxVoices(std::size_t count) noexcept { m_maxVoices = count; }

        private:
            std::size_t freeIn(bool reserved) const;

            std::map<SoundId, std::string> m_sounds;
            std::map<MusicId, std::string> m_musics;
            std::map<VoiceId, Voice>       m_voices;
            std::map<StreamId, Stream>     m_streams;
            StreamId                       m_nextStream = 1;
            std::size_t                    m_reserved = 0;
            MusicState                     m_music;
            SoundId                        m_nextSound = 1;
            MusicId                        m_nextMusic = 1;
            VoiceId                        m_nextVoice = 1;
            std::size_t                    m_maxVoices = 32;
    };

}
