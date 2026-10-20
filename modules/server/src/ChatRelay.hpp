#pragma once

#include "Endpoint.hpp"
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace kuge::server
{

    struct ChatConfig
    {
        std::size_t maxBytes = 200;        //!< The longest line, in bytes (a text field counts characters; the wire counts bytes)
        int         burst = 4;             //!< Lines a player may say at once
        double      refillSeconds = 1.0;   //!< One more line comes back this often
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  What a server checks before it relays a line of chat: a client is not trusted
     *
     * A line is let through when, once its spaces at both ends are cut, it is not empty, is at most `maxBytes` (before the cut),
     * is well-formed UTF-8, has no control character (a line break, a tab, an escape...), and the player has a line left in
     * its bucket: `burst` at the start, one more each `refillSeconds`. A line that does not pass costs nothing.
     *
     * The chat is `RoomScene::enableChat()`; use this directly to chat from somewhere else (a lobby).
     */
    ////////////////////////////////////////////////////////////////////////////
    class ChatRelay
    {
        public:
            explicit ChatRelay(ChatConfig config = {}) : m_config(config) {}

            //! The cleaned line, or nothing if `from` may not say it now
            //! @param now  Seconds, from a clock that never goes back
            std::optional<std::string> accept(net::ConnectionId from, std::string_view text, double now);

            //! The bucket of a connection that went (a new one starts full)
            void forget(net::ConnectionId from) { m_buckets.erase(from); }

            const ChatConfig& config(void) const noexcept { return m_config; }

            //! Is this text a line that could be said: not empty, not too long, valid, with no control character
            static bool clean(std::string_view text, std::size_t maxBytes);

        private:
            struct Bucket
            {
                double tokens;
                double at;
            };

            ChatConfig                     m_config;
            std::map<net::ConnectionId, Bucket> m_buckets;
    };

}
