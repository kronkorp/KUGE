#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  How fast the loop of a scene stack goes: frames per second, the mean frame, the worst one in a hundred, and what a
     *         tick costs. Fed by the engine (Engine::stats()).
     *
     * Over the last `WINDOW` frames. Times are in seconds.
     */
    ////////////////////////////////////////////////////////////////////////////
    class FrameStats
    {
        public:
            static constexpr std::size_t WINDOW = 240;

            //! @param frameSeconds  How long the frame lasted (what the loop was told)
            //! @param tickSeconds   What one fixed tick of this frame cost on average (0: no tick ran)
            //! @param workSeconds   What the whole loop cost (the rest of the frame was waiting)
            void push(double frameSeconds, double tickSeconds, double workSeconds);

            std::size_t frames(void) const noexcept { return m_total; }
            double      fps(void) const;
            double      frameMean(void) const;
            double      frameWorst(void) const;          //!< The slowest frame of the window
            double      frameWorstPercent(void) const;   //!< The mean of the slowest 1 % (at least one frame)
            double      tickMean(void) const;            //!< Over the frames where a tick ran
            double      workMean(void) const;

            //! The last `count` frame times, oldest first (for a graph)
            std::vector<double> recent(std::size_t count) const;

        private:
            struct Sample { double frame, tick, work; };

            std::deque<Sample> m_samples;
            std::size_t        m_total = 0;
    };

    //! The numbers of one network connection, as plain data: the overlay shows them, a test reads them, the server logs them.
    //! `makeNetReport()` (kuge-net) fills it from an Endpoint.
    struct NetReport
    {
        double        rttSeconds = -1.0;        //!< Round trip; negative: not known yet
        double        rttLow = -1.0;            //!< Lowest and highest seen since the report was started (negative: none)
        double        rttHigh = -1.0;
        double        sentPerSecond = 0.0;      //!< Bytes a second, over the last interval
        double        receivedPerSecond = 0.0;
        double        packetsSentPerSecond = 0.0;
        double        packetsReceivedPerSecond = 0.0;
        std::uint64_t resends = 0;
        std::uint64_t duplicates = 0;
        std::uint64_t malformed = 0;
        std::uint64_t entities = 0;             //!< What the game knows to count (replicated entities); 0 if it does not say
    };

    //! One line of text for a log or a screen: "rtt 42 ms (38-61)  down 3.1 KiB/s  up 0.8 KiB/s  pkts 31/12  resends 4 dup 1 bad 0"
    std::string describe(const NetReport& report);

    //! Named numbers that any part of the program sets and an overlay reads ("audio.voices", "assets.textures"). Thread-safe.
    class Gauges
    {
        public:
            static Gauges& global(void);

            void   set(const std::string& name, double value);
            double get(const std::string& name, double fallback = 0.0) const;
            std::map<std::string, double> all(void) const;

        private:
            mutable std::mutex           m_mutex;
            std::map<std::string, double> m_values;
    };

}
