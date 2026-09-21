#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <vector>

namespace kuge::net
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Messages that arrive, once each and in order, over a transport
     *         that loses, repeats and reorders packets. One per connection and
     *         direction pair. It only does the arithmetic: it sends nothing and
     *         reads no clock (the time is given), so it can be tested alone.
     *
     * Sending: queue() gives a message a number; collect() says what to put on
     * the wire now (new messages, and the ones whose ack did not come in time);
     * onAck() frees what the other side says it got.
     * Receiving: onData() takes a numbered message, takeDelivered() gives the
     * ones that are now in order, ack() and ackBits() say what to tell the other
     * side: every message below ack() is here, and bit i of ackBits() says that
     * message ack() + 1 + i is too.
     *
     * The time before a message is sent again follows the round trip time that
     * the acks show (and doubles each time it is not answered, up to a limit).
     */
    ////////////////////////////////////////////////////////////////////////////
    class ReliableChannel
    {
        public:
            struct Config
            {
                std::size_t window      = 256;     //!< Messages on their way at once (the rest wait)
                std::size_t maxQueued   = 1024;    //!< Messages that wait for a place in the window
                double      initialRto  = 0.2;     //!< Seconds before the first resend, before a round trip is known
                double      minRto      = 0.03;
                double      maxRto      = 1.0;
                bool        resend      = true;    //!< false over a transport that never loses (TCP)
            };

            //! A message to put on the wire
            struct Outgoing
            {
                std::uint32_t                     seq;
                const std::vector<std::uint8_t>*  payload;   //!< Valid until the next call
                bool                              again;     //!< Sent before
            };

            ReliableChannel(void) : ReliableChannel(Config{}) {}
            explicit ReliableChannel(Config config) : m_config(config) {}

            // -- Sending --------------------------------------------------------------------
            //! @return  false if too many messages already wait (the caller decides what to do)
            bool queue(std::vector<std::uint8_t> payload);

            //! What is to be sent at this time
            void collect(double now, std::vector<Outgoing>& out);

            //! The other side says what it received
            void onAck(std::uint32_t ack, std::uint32_t bits, double now);

            // -- Receiving ------------------------------------------------------------------
            void onData(std::uint32_t seq, std::vector<std::uint8_t> payload);
            std::vector<std::vector<std::uint8_t>> takeDelivered(void);

            std::uint32_t ack(void) const noexcept { return m_next; }
            std::uint32_t ackBits(void) const noexcept;
            bool          ackPending(void) const noexcept { return m_ackPending; }
            void          ackSent(void) noexcept { m_ackPending = false; }

            // -- State ----------------------------------------------------------------------
            std::size_t   inFlight(void) const noexcept { return m_inflight.size(); }
            std::size_t   waiting(void) const noexcept { return m_queued.size(); }
            bool          idle(void) const noexcept { return m_inflight.empty() && m_queued.empty(); }
            double        rtt(void) const noexcept { return m_hasRtt ? m_srtt : 0.0; }
            double        rto(void) const noexcept;
            std::uint64_t resends(void) const noexcept { return m_resends; }
            std::uint64_t duplicates(void) const noexcept { return m_duplicates; }

        private:
            struct Sent
            {
                std::uint32_t              seq;
                std::vector<std::uint8_t>  payload;
                double                     firstSent = 0.0;
                double                     lastSent  = 0.0;
                std::uint32_t              sends     = 0;
            };

            void acknowledge(std::uint32_t seq, double now);

            Config                                  m_config;

            std::uint32_t                           m_nextSeq = 0;
            std::deque<std::vector<std::uint8_t>>   m_queued;
            std::deque<Sent>                        m_inflight;   // by increasing seq
            std::uint32_t                           m_highestAcked = 0;
            bool                                    m_anyAcked = false;
            double                                  m_srtt = 0.0;
            double                                  m_rttvar = 0.0;
            bool                                    m_hasRtt = false;
            std::uint64_t                           m_resends = 0;

            std::uint32_t                           m_next = 0;   // every seq below it was delivered
            std::map<std::uint32_t, std::vector<std::uint8_t>> m_stash;   // received, not in order yet
            std::vector<std::vector<std::uint8_t>>  m_delivered;
            bool                                    m_ackPending = false;
            std::uint64_t                           m_duplicates = 0;
    };

}
