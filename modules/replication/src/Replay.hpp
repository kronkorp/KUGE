#pragma once

#include "ReplicationClient.hpp"
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace kuge::replication
{

    //! A replay that cannot be used, and why (a file of another game, of another version of the components...)
    class ReplayError : public std::runtime_error
    {
        public:
            using std::runtime_error::runtime_error;
    };

    struct RecorderConfig
    {
        std::uint32_t tickRate = 60;
        std::uint16_t protocol = 0;          //!< Of the game (EndpointConfig::protocol): a replay of another one is refused
        std::string   info;                  //!< Anything the game wants back: the map, the mode, the date
        double        keyframeSeconds = 2.0; //!< A whole snapshot this often: where a replay can start or jump to
        //! 0: everything is kept (a match recorded to be saved). Above: only the last seconds (a killcam), written when asked for.
        //! Older parts go whole, from one keyframe to the next, so a bit more than this is kept.
        double        ringSeconds = 0.0;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Keeps what a client was shown, to show it again: a replay, a killcam
     *
     *     Recorder recorder(registry, {.tickRate = 60, .protocol = 3, .info = "dust", .ringSeconds = 8.0});
     *     recorder.attach(replication);                              // it follows every snapshot the client applies
     *     recorder.event(tick, KILL, bytes);                         // anything the game wants back at that time
     *     ...
     *     recorder.save("kill.replay");                              // or bytes(): the last 8 seconds
     *
     * What it keeps is the world as the client built it: a whole snapshot (a **keyframe**) every `keyframeSeconds` and, between
     * them, the changes from one snapshot to the next. That is the replication's own format, and "the same world always gives the
     * same bytes". It is a recording of what **this player** was sent: with a visibility filter, that is what it could see.
     * What a replay shows (a free camera, the killer's point of view, a slow motion) is the game's: the engine gives back the
     * world, and the events the game wrote down.
     */
    ////////////////////////////////////////////////////////////////////////////
    class Recorder
    {
        public:
            Recorder(const ReplicationRegistry& registry, RecorderConfig config);

            //! Follows the snapshots this client applies. The recorder must outlive the client's use of it.
            void attach(ReplicationClient& client);

            //! One more snapshot (what attach() does for you)
            void record(std::uint32_t tick, const WorldState& state);

            //! Something the game wants to find again at that tick (a kill, a round change, the angles of a player)
            void event(std::uint32_t tick, std::uint16_t type, std::span<const std::uint8_t> bytes);

            //! The recording as a file would hold it: from the first keyframe kept
            std::vector<std::uint8_t> bytes(void) const;

            //! @throw ReplayError if the file cannot be written
            void save(const std::filesystem::path& file) const;

            std::size_t entries(void) const noexcept;

        private:
            struct Entry
            {
                std::uint8_t               kind;    //!< 0 keyframe, 1 snapshot, 2 event
                std::uint32_t              tick;
                std::uint16_t              type;    //!< Events
                std::vector<std::uint8_t>  data;
            };

            const ReplicationRegistry& m_registry;
            RecorderConfig             m_config;
            std::deque<std::deque<Entry>> m_segments;   //!< From one keyframe to the next
            WorldState                 m_previous;
            std::uint32_t              m_lastKeyframe = 0;
            std::uint32_t              m_lastTick = 0;      //!< Of the last snapshot recorded
            bool                       m_any = false;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Gives a recording back to a ReplicationClient, on a clock that can be paused, slowed and moved
     *
     *     Replayer replay(registry, recorder.bytes(), client, {.protocol = 3});      // @throw ReplayError: another game, other components
     *     replay.onEvent([](std::uint32_t tick, std::uint16_t type, std::span<const std::uint8_t> bytes) { ... });
     *     ...
     *     replay.update(frameSeconds);        // each frame INSTEAD of client.update(): it plays, and updates the client by the replay's time
     *     replay.setSpeed(0.5);  replay.pause(true);  replay.seek(tick);
     *
     * The client is a normal ReplicationClient on a World of its own, with its hooks (sprites, models...) as in a game. `seek` goes to the
     * keyframe at or before the tick (the client is reset, so the hooks of onDestroy run) and plays forward from there, so a jump
     * is instant but lands on the world of that tick. Speed 0.5 is smooth: the interpolation runs on the replay's time.
     */
    ////////////////////////////////////////////////////////////////////////////
    class Replayer
    {
        public:
            struct Config
            {
                std::uint16_t protocol = 0;   //!< Must be that of the recording
            };

            using EventHook = std::function<void(std::uint32_t tick, std::uint16_t type, std::span<const std::uint8_t> bytes)>;

            Replayer(const ReplicationRegistry& registry, std::vector<std::uint8_t> file, ReplicationClient& client, Config config);

            //! @throw ReplayError
            static Replayer load(const ReplicationRegistry& registry, const std::filesystem::path& file, ReplicationClient& client, Config config);

            void onEvent(EventHook hook) { m_event = std::move(hook); }

            void update(double frameSeconds);
            void setSpeed(double speed) noexcept { m_speed = speed < 0.0 ? 0.0 : speed; }
            void pause(bool paused) noexcept { m_paused = paused; }
            bool paused(void) const noexcept { return m_paused; }

            //! Goes to a tick of the recording (clamped to it)
            void seek(double tick);

            double        tick(void) const noexcept { return m_tick; }
            std::uint32_t firstTick(void) const noexcept { return m_first; }
            std::uint32_t lastTick(void) const noexcept { return m_last; }
            bool          finished(void) const noexcept { return m_next >= m_entries.size(); }
            std::uint32_t tickRate(void) const noexcept { return m_tickRate; }
            const std::string& info(void) const noexcept { return m_info; }

        private:
            struct Entry
            {
                std::uint8_t               kind;
                std::uint32_t              tick;
                std::uint16_t              type;
                std::vector<std::uint8_t>  data;
            };

            void feedUpTo(double tick);
            void feed(const Entry& entry);

            const ReplicationRegistry& m_registry;
            ReplicationClient&         m_client;
            std::vector<Entry>         m_entries;
            std::size_t                m_next = 0;
            std::uint32_t              m_schema = 0;
            std::uint32_t              m_tickRate = 60;
            std::string                m_info;
            std::uint32_t              m_first = 0;
            std::uint32_t              m_last = 0;
            std::uint32_t              m_previousTick = 0;   //!< Of the last snapshot fed: the base of the next
            double                     m_tick = 0.0;
            double                     m_speed = 1.0;
            bool                       m_paused = false;
            EventHook                  m_event;
            bool                       m_quiet = false;     //!< Seeking: the events on the way are not told again
    };

}
