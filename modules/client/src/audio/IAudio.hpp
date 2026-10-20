#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>

namespace kuge
{

    //! A sound that cannot be loaded, or an audio device that cannot be opened
    class AudioError : public std::runtime_error
    {
        public:
            using std::runtime_error::runtime_error;
    };

    // A number given by the backend for what it keeps. 0 is nothing.
    using SoundId = std::uint32_t;   //!< A short sound, kept in memory
    using MusicId = std::uint32_t;   //!< A long one, played as it is read
    using VoiceId = std::uint32_t;   //!< One playing of a sound

    using StreamId = std::uint32_t;  //!< Samples that arrive while they are played (a voice from the network)

    constexpr SoundId NO_SOUND = 0;
    constexpr StreamId NO_STREAM = 0;
    constexpr MusicId NO_MUSIC = 0;
    constexpr VoiceId NO_VOICE = 0;

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Plays sounds and music. A backend gives one (see Backend).
     *
     * It knows nothing about buses or distance: volumes, pans and filters arrive ready
     * (0 to 1, -1 for the left to 1 for the right, and 1 for a sound that is not filtered
     * to 0 for one that is muffled), see Audio.
     */
    ////////////////////////////////////////////////////////////////////////////
    class IAudio
    {
        public:
            virtual ~IAudio(void) = default;

            //! @throw AudioError if the file cannot be read as a sound
            virtual SoundId loadSound(const std::filesystem::path& file) = 0;
            virtual void    destroySound(SoundId sound) noexcept = 0;

            //! @throw AudioError
            virtual MusicId loadMusic(const std::filesystem::path& file) = 0;
            virtual void    destroyMusic(MusicId music) noexcept = 0;

            //! @param reserved  Play it on one of the voices kept by reserveVoices() (and only on those)
            //! @return  NO_VOICE if all the voices (of the pool asked for) are busy
            virtual VoiceId play(SoundId sound, float volume, float pan, bool loop, bool reserved) = 0;
            VoiceId play(SoundId sound, float volume, float pan, bool loop) { return play(sound, volume, pan, loop, false); }

            //! @param lowpass  1: the sound as it is. Less: the highs are cut (down to 0: very muffled), for what is
            //!                 far, behind a wall or behind the listener.
            virtual void    setVoice(VoiceId voice, float volume, float pan, float lowpass) = 0;
            void            setVoice(VoiceId voice, float volume, float pan) { setVoice(voice, volume, pan, 1.0f); }
            virtual void    stop(VoiceId voice) = 0;
            virtual bool    playing(VoiceId voice) const = 0;

            //! Keeps this many voices for the sounds played with `reserved`: the others never take them, so a
            //! line of dialogue is not lost to a flood of effects. 0 (the default): none. Call it before playing.
            virtual void reserveVoices(int count) = 0;

            //! Samples that come while they are played: a voice from the network, a synthesiser.
            //! @param channels  1 or 2; the samples are interleaved 16 bits
            //! @throw AudioError if the rate or the channels are not usable
            virtual StreamId createStream(int sampleRate, int channels) = 0;
            virtual void     destroyStream(StreamId stream) noexcept = 0;

            //! Adds samples at the end of a stream (what it holds is limited, 5 seconds: a stream that is
            //! not played drops the oldest). Any thread that is not the audio thread.
            virtual void pushSamples(StreamId stream, std::span<const std::int16_t> samples) = 0;

            //! Frames (a sample of each channel) waiting to be played
            virtual std::size_t bufferedFrames(StreamId stream) const = 0;

            //! A voice that plays what is pushed into the stream. It ends when the stream has nothing left
            //! (play it once it has some: a few frames of jitter buffer for a voice chat), and the stream can be
            //! played again after that.
            virtual VoiceId playStream(StreamId stream, float volume, float pan, bool reserved) = 0;

            //! One music at a time: it replaces the one that plays
            virtual void playMusic(MusicId music, float volume, bool loop, float fadeInSeconds) = 0;
            virtual void setMusicVolume(float volume) = 0;
            virtual void stopMusic(float fadeOutSeconds) = 0;
            virtual bool musicPlaying(void) const = 0;
    };

}
