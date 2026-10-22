#pragma once

#include "Math2D.hpp"
#include "audio/IAudio.hpp"
#include <functional>
#include <memory>
#include <span>
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

            //! Puts another sound in this one (a reload): what plays the old one stops
            void replace(SoundId id) noexcept { m_audio.destroySound(m_id); m_id = id; }

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

            //! Puts another music in this one (a reload)
            void replace(MusicId id) noexcept { m_audio.destroyMusic(m_id); m_id = id; }

        private:
            IAudio& m_audio;
            MusicId m_id;
    };

    //! Samples that arrive while they are played (a voice from the network, a synthesiser). Made by
    //! Audio::createStream(), played by Audio::play(stream): one stream per speaker, each with its own voice.
    class Stream
    {
        public:
            Stream(IAudio& audio, StreamId id) noexcept : m_audio(audio), m_id(id) {}
            ~Stream(void) { m_audio.destroyStream(m_id); }

            Stream(const Stream&)            = delete;
            Stream& operator=(const Stream&) = delete;

            StreamId id(void) const noexcept { return m_id; }

            //! Samples at the end (interleaved, 16 bits). See IAudio::pushSamples().
            void push(std::span<const std::int16_t> samples) { m_audio.pushSamples(m_id, samples); }

            //! How many frames are waiting to be played
            std::size_t buffered(void) const { return m_audio.bufferedFrames(m_id); }

        private:
            IAudio&  m_audio;
            StreamId m_id;
    };

    //! One playing of a sound. Empty if it could not start (too far to be heard, no free voice).
    struct Voice
    {
        VoiceId id = NO_VOICE;

        explicit operator bool(void) const noexcept { return id != NO_VOICE; }
    };

    //! The groups of volumes a player can set apart, on top of the master volume
    enum class Bus
    {
        Effects,
        Music,
        Dialogue,   //!< Spoken lines: they have their own volume, reserved voices and can duck the music
    };

    struct PlayOptions
    {
        float volume  = 1.0f;   //!< 0 to 1
        float pan     = 0.0f;   //!< -1: all on the left, 1: all on the right
        bool  loop    = false;
        float lowpass = 1.0f;   //!< 1: as it is. Less: the highs are cut, down to 0 (far away, behind a wall or the listener)
        //! Effects (the default) or Dialogue; Music is not for voices and plays as an effect
        Bus   bus     = Bus::Effects;
        //! When all the voices are busy, a sound takes the place of one of lower priority, or of the same priority that is
        //! quieter. A shot of the player is not lost to a far footstep.
        int   priority = 0;
    };

    struct MusicOptions
    {
        float volume = 1.0f;
        bool  loop   = true;
        float fadeIn = 0.0f;   //!< Seconds
        //! Called from Audio::update() (the thread of the game) when the music ends by itself: never for a looping one,
        //! nor after stopMusic() or another playMusic(). It may start the next music.
        std::function<void()> onFinished;
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
        float lowpass = 1.0f;   //!< 1 in 2D; 3D lowers it for what is behind the listener or behind a wall
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

            //! A stream, as a voice (its buses, its priority and its reserved voices are those of a sound).
            //! It ends when the stream has nothing left to play.
            Voice play(const Stream& stream, PlayOptions options = {});

            //! @throw AudioError if the rate or the number of channels cannot be played
            std::shared_ptr<Stream> createStream(int sampleRate, int channels = 1);

            //! Keeps `count` voices for the lines of dialogue (Bus::Dialogue): effects never take them, so a flood of
            //! effects does not drop a spoken line. When they are all busy, a line of higher priority takes
            //! the place of the quietest one of lower priority, otherwise it is not played (an empty Voice). 0: none.
            //! Say it once, at the start.
            void reserveVoices(Bus bus, int count);

            //! While a line of dialogue plays, the music is multiplied by `musicFactor` (0 to 1; 1: no ducking, the default),
            //! reaching it, and coming back, over `fadeSeconds`. The volume the player set is not touched.
            void setDucking(float musicFactor, float fadeSeconds);

            //! A sound at a place: louder when near, panned by where it is
            //! @param options  volume and loop are used (the pan is worked out)
            Voice playAt(const Sound& sound, Vec2 source, const Listener& listener, PlayOptions options = {});

            void stop(Voice voice);
            bool playing(Voice voice) const;
            void setVoice(Voice voice, float volume, float pan, float lowpass = 1.0f);

            void playMusic(const Music& music, MusicOptions options = {});
            void stopMusic(float fadeOut = 0.0f);
            bool musicPlaying(void) const { return m_backend.musicPlaying(); }

            //! A paused music is still musicPlaying(); it keeps its place until resumeMusic()
            void   pauseMusic(void) { m_backend.pauseMusic(); }
            void   resumeMusic(void) { m_backend.resumeMusic(); }
            bool   musicPaused(void) const { return m_backend.musicPaused(); }

            //! In seconds, for a progress bar. The length is 0 when the format does not tell.
            double musicPosition(void) const { return m_backend.musicPosition(); }
            double musicLength(void) const { return m_backend.musicLength(); }
            void   seekMusic(double seconds) { m_backend.seekMusic(seconds); }

            //! Forgets the voices that ended, tells MusicOptions::onFinished, and moves the ducking on by the seconds that passed. The client
            //! calls it once per loop.
            void update(double seconds = 0.0);

            //! Voices that are playing, as far as the last update() knows
            std::size_t voices(void) const noexcept { return m_voices.size(); }

        private:
            struct Playing
            {
                VoiceId id;
                float   volume;   //!< Its own, before the buses
                float   pan;
                float   lowpass;
                Bus     bus;
                int     priority;
                bool    reserved;
            };

            float heard(const Playing& voice) const noexcept
            {
                return m_master * m_bus[static_cast<int>(voice.bus)] * voice.volume;
            }
            void  applyMusicVolume(void);
            void  prune(void);
            float musicHeard(void) const noexcept
            {
                return m_master * m_bus[static_cast<int>(Bus::Music)] * m_musicVolume * m_duck;
            }
            Voice start(PlayOptions options, const std::function<VoiceId(float volume, float pan, float lowpass, bool reserved)>& open);

            IAudio&               m_backend;
            float                 m_master = 1.0f;
            float                 m_bus[3] = {1.0f, 1.0f, 1.0f};
            float                 m_musicVolume = 1.0f;
            int                   m_reservedVoices = 0;
            float                 m_duckTo = 1.0f;      //!< The factor of the music while a line plays
            float                 m_duckSeconds = 0.0f;
            float                 m_duck = 1.0f;        //!< Where the music is now, between 1 and m_duckTo
            std::vector<Playing>  m_voices;
            std::function<void()> m_onMusicFinished;
    };

}
