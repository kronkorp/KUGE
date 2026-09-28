#pragma once

#include <cstdint>
#include <filesystem>
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

    constexpr SoundId NO_SOUND = 0;
    constexpr MusicId NO_MUSIC = 0;
    constexpr VoiceId NO_VOICE = 0;

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Plays sounds and music. A backend gives one (see Backend).
     *
     * It knows nothing about buses or distance: volumes and pans arrive ready
     * (0 to 1, and -1 for the left to 1 for the right), see Audio.
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

            //! @return  NO_VOICE if all the voices are busy
            virtual VoiceId play(SoundId sound, float volume, float pan, bool loop) = 0;
            virtual void    setVoice(VoiceId voice, float volume, float pan) = 0;
            virtual void    stop(VoiceId voice) = 0;
            virtual bool    playing(VoiceId voice) const = 0;

            //! One music at a time: it replaces the one that plays
            virtual void playMusic(MusicId music, float volume, bool loop, float fadeInSeconds) = 0;
            virtual void setMusicVolume(float volume) = 0;
            virtual void stopMusic(float fadeOutSeconds) = 0;
            virtual bool musicPlaying(void) const = 0;
    };

}
