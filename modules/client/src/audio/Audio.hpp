#pragma once

#include "Math2D.hpp"
#include "audio/IAudio.hpp"
#include <memory>
#include <vector>

namespace kuge
{

    //! A sound loaded by the backend, given back when nobody holds it any more.
    //! Get them from the client's asset manager (client.sounds().load("boom.wav")).
    //! Like a Texture, it must not outlive the backend that made it.
    class Sound
    {
        public:
            Sound(IAudio& audio, SoundId id) noexcept : m_audio(audio), m_id(id) {}
            ~Sound(void) { m_audio.destroySound(m_id); }

            Sound(const Sound&)            = delete;
            Sound& operator=(const Sound&) = delete;

            SoundId id(void) const noexcept { return m_id; }

        private:
            IAudio& m_audio;
            SoundId m_id;
    };

    //! A music. See Sound.
    class Music
    {
        public:
            Music(IAudio& audio, MusicId id) noexcept : m_audio(audio), m_id(id) {}
            ~Music(void) { m_audio.destroyMusic(m_id); }

            Music(const Music&)            = delete;
            Music& operator=(const Music&) = delete;

            MusicId id(void) const noexcept { return m_id; }

        private:
            IAudio& m_audio;
            MusicId m_id;
    };

    //! One playing of a sound. Empty if it could not start (too far to be heard, no free voice).
    struct Voice
    {
        VoiceId id = NO_VOICE;

        explicit operator bool(void) const noexcept { return id != NO_VOICE; }
    };

    struct PlayOptions
    {
        float volume = 1.0f;   //!< 0 to 1
        float pan    = 0.0f;   //!< -1: all on the left, 1: all on the right
        bool  loop   = false;
    };

    struct MusicOptions
    {
        float volume = 1.0f;
        bool  loop   = true;
        float fadeIn = 0.0f;   //!< Seconds
    };

    //! The two groups of volumes a player can set apart, on top of the master volume
    enum class Bus
    {
        Effects,
        Music,
    };

    //! Who hears, for a sound that comes from somewhere in the world
    struct Listener
    {
        Vec2  position{};
        float range    = 600.0f;   //!< Farther than this, it is not heard at all
        float panWidth = 300.0f;   //!< Sideways, this far is all on one side
    };

    //! What a sound at a place is heard as
    struct Spatial
    {
        float volume;   //!< 1 on the spot, falling to 0 at the end of the range
        float pan;
    };

    Spatial spatialize(Vec2 source, const Listener& listener) noexcept;

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  What a game plays sounds through: volumes by bus, sounds that
     *         come from a place, and one music with a fade
     *
     *     auto boom = client.sounds().load("boom.wav");
     *     audio.play(*boom);
     *     audio.playAt(*boom, enemy.position, {.position = camera.position});
     *
     *     audio.setBusVolume(kuge::Bus::Music, 0.4f);      // a slider in the options
     *     audio.playMusic(*theme, {.fadeIn = 2.0f});
     *
     * The volume that is heard is the master volume, times the one of the bus,
     * times the one of the sound. Changing a bus changes what already plays.
     * Available to systems as kuge::Ref<Audio>.
     */
    ////////////////////////////////////////////////////////////////////////////
    class Audio
    {
        public:
            explicit Audio(IAudio& backend) noexcept : m_backend(backend) {}

            //! Volumes go from 0 to 1 (beyond is brought back)
            void  setMasterVolume(float volume);
            void  setBusVolume(Bus bus, float volume);
            float masterVolume(void) const noexcept { return m_master; }
            float busVolume(Bus bus) const noexcept { return m_bus[static_cast<int>(bus)]; }

            Voice play(const Sound& sound, PlayOptions options = {});

            //! A sound at a place: louder when near, panned by where it is
            //! @param options  volume and loop are used (the pan is worked out)
            Voice playAt(const Sound& sound, Vec2 source, const Listener& listener, PlayOptions options = {});

            void stop(Voice voice);
            bool playing(Voice voice) const;
            void setVoice(Voice voice, float volume, float pan);

            void playMusic(const Music& music, MusicOptions options = {});
            void stopMusic(float fadeOut = 0.0f);
            bool musicPlaying(void) const { return m_backend.musicPlaying(); }

            //! Forgets the voices that ended. The client calls it once per loop.
            void update(void);

            //! Voices that are playing, as far as the last update() knows
            std::size_t voices(void) const noexcept { return m_voices.size(); }

        private:
            struct Playing
            {
                VoiceId id;
                float   volume;   //!< Its own, before the buses
                float   pan;
            };

            float heard(float volume) const noexcept
            {
                return m_master * m_bus[static_cast<int>(Bus::Effects)] * volume;
            }
            void applyMusicVolume(void);

            IAudio&               m_backend;
            float                 m_master = 1.0f;
            float                 m_bus[2] = {1.0f, 1.0f};
            float                 m_musicVolume = 1.0f;
            std::vector<Playing>  m_voices;
    };

}
