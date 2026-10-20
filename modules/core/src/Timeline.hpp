#pragma once

#include "Json.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace kuge
{

    //! A timeline file that is not one: the message begins with the name of the file and says where ("boss.json: tracks[1].keys[0]: ...")
    class TimelineError : public std::runtime_error
    {
        public:
            using std::runtime_error::runtime_error;
    };

    enum class Ease : std::uint8_t { Linear, InQuad, OutQuad, InOutQuad };

    //! 0 -> 0 and 1 -> 1 whatever the curve; x outside 0..1 is clamped
    double ease(Ease curve, double x) noexcept;

    enum class TrackKind : std::uint8_t { Transform, Animation, Camera, Fade, Audio, Text, Event };

    const char* trackKindName(TrackKind kind) noexcept;

    //! One thing that happens at `t`. With a `duration` above 0 it is a tween: it goes on for that long (a move, a fade, a line on screen)
    struct TimelineKey
    {
        double  t = 0.0;
        double  duration = 0.0;
        Ease    curve = Ease::Linear;
        Json    data;   //!< The whole key as written: what the kind of the track reads (target, to, clip, color, text, name...)
    };

    struct TimelineTrack
    {
        TrackKind                 kind = TrackKind::Event;
        std::vector<TimelineKey>  keys;   //!< By time (the ones with the same time keep the order of the file)
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A cinematic as data: a duration and tracks of keys. A file, one per cinematic:
     *
     *     { "duration": 6.0, "tracks": [
     *         { "kind": "transform", "keys": [ { "t": 0.5, "duration": 2.0, "ease": "outQuad", "target": "boss", "to": { "position": [400, 120] } } ] },
     *         { "kind": "text",      "keys": [ { "t": 1.0, "duration": 3.0, "speaker": "Boss", "text": "You came." } ] },
     *         { "kind": "event",     "keys": [ { "t": 5.5, "name": "open_gate" } ] } ] }
     *
     * Kinds and what a key needs (the others are optional):
     *   transform  target (name of an entity), to ({field: value}, of `component`, default "Transform2D")
     *   animation  target, clip
     *   camera     position [x, y] and/or zoom and/or shake {amplitude, [frequency]}; target (default: the entity "camera")
     *   fade       color [r, g, b, a] (0..1): the colour of the screen goes to it
     *   audio      sound (file) [volume, bus "effects"/"dialogue"] or music (file) [loop, fadeIn] or stopMusic [fadeOut]
     *   text       text, speaker (a subtitle for `duration`; the empty text clears)
     *   event      name, data: the game does the rest
     * `ease` is "linear" (default), "inQuad", "outQuad" or "inOutQuad". Times are in seconds; every key ends before `duration`.
     */
    ////////////////////////////////////////////////////////////////////////////
    struct Timeline
    {
        std::string                id;          //!< The name of the file without its extension
        double                     duration = 0.0;
        std::vector<TimelineTrack> tracks;

        //! @throw TimelineError naming `name` (the file) and the place: an unknown kind, a missing field, a negative time...
        static Timeline fromJson(const Json& json, const std::string& name);
        //! @throw TimelineError (also for a file that is not JSON or not there)
        static Timeline load(const std::filesystem::path& file);
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Plays a Timeline: the time comes from the delta you give, never from a clock, so the same timeline played twice does the same
     *
     *     TimelinePlayer player(timeline, [&](const TimelinePlayer::Step& step) { ... });
     *     player.update(dt);       // each frame
     *     player.skip();           // the end now: every key that was left starts and every tween ends, in order
     *
     * A key calls the step once at its time (`Start`). A tween (a duration above 0) then calls it every update while it runs (`Tween`, with
     * progress 0..1 before the curve) and once at its end (`End`, progress 1), so a tween always ends at its value whatever the size of the
     * frames. Keys of one update start in order of time, then track, then place in the track. The callback must not call seek(), skip() or
     * restart() on its own player.
     */
    ////////////////////////////////////////////////////////////////////////////
    class TimelinePlayer
    {
        public:
            struct Step
            {
                enum class Phase : std::uint8_t { Start, Tween, End };

                Phase                 phase;
                std::size_t           trackIndex;
                std::size_t           keyIndex;
                const TimelineTrack&  track;
                const TimelineKey&    key;
                double                progress;   //!< 0 at Start, in 0..1 for Tween, 1 at End (before the curve)
                double                eased;      //!< `ease(key.curve, progress)`
            };
            using Callback = std::function<void(const Step&)>;

            TimelinePlayer(std::shared_ptr<const Timeline> timeline, Callback callback);

            void update(double dt);
            void pause(void) noexcept { m_playing = false; }
            void play(void) noexcept { m_playing = true; }
            //! Back to the start, playing: the keys will start again
            void restart(void);
            //! Goes to a time (clamped). Backwards it restarts and plays up to there, so the keys on the way start again.
            void seek(double seconds);
            //! To the end at once. Nothing happens if it is finished.
            void skip(void);
            //! 2: twice as fast. 0: frozen. Not negative.
            void setSpeed(double speed) noexcept { m_speed = speed < 0.0 ? 0.0 : speed; }

            double time(void) const noexcept { return m_time; }
            bool   playing(void) const noexcept { return m_playing && !m_finished; }
            bool   finished(void) const noexcept { return m_finished; }
            const Timeline& timeline(void) const noexcept { return *m_timeline; }

        private:
            struct Running { std::size_t track; std::size_t key; };

            void advanceTo(double target);
            void fire(Step::Phase phase, std::size_t track, std::size_t key, double progress);

            std::shared_ptr<const Timeline>  m_timeline;
            Callback                         m_callback;
            std::vector<std::size_t>         m_next;      //!< By track: the next key to start
            std::vector<Running>             m_running;   //!< The tweens that have started and not ended
            double                           m_time = 0.0;
            double                           m_speed = 1.0;
            bool                             m_playing = true;
            bool                             m_finished = false;
    };

}
