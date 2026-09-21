#pragma once

#include "Endpoint.hpp"
#include "Wire.hpp"
#include <algorithm>
#include <map>
#include <optional>
#include <vector>

namespace kuge::replication
{

    //! Inputs of a client, numbered from 1: a few of the latest go in each packet, so that a lost one is
    //! usually in the next. What the inputs are (the game's own type) is inside `data`.
    struct InputPacket
    {
        KUGE_MESSAGE(InputPacket, firstSequence, count, data)
        std::uint32_t              firstSequence = 0;
        std::uint8_t               count = 0;
        std::vector<std::uint8_t>  data;          //!< For each input: its length (2 bytes), then its bytes
    };

    struct InputServerConfig
    {
        std::size_t   maxQueued = 64;    //!< Inputs kept for a client that is slow to be applied: the oldest are dropped
        std::size_t   jitter    = 0;     //!< Inputs that must wait in the queue before the next is applied (0: as soon as it is there)
    };

    struct InputServerStats
    {
        std::uint64_t received = 0;      //!< Inputs that were new
        std::uint64_t duplicates = 0;    //!< Copies (the redundancy of the packets), or inputs that came too late to be applied
        std::uint64_t filled = 0;        //!< Ticks where the last input was used again: the next one was not there
        std::uint64_t dropped = 0;       //!< Inputs thrown away because too many waited
        std::uint64_t malformed = 0;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  What a room does with the inputs of its players: it puts them in
     *         order and gives out one per player at each tick
     *
     *     InputServer<Steer> inputs(endpoint());                       // Steer: a type that goes on the wire
     *     inputs.addClient(player.connection);                         // in onPlayerJoined
     *
     *     for (const auto& applied : inputs.collect()) {               // each tick, before the simulation
     *         steer(world, shipOf(applied.connection), applied.input);
     *         replication.setInputAck(applied.connection, applied.sequence);
     *     }
     *
     * Inputs are applied once, in order. If the next one is not there (lost, or late), the last one is used
     * again for that tick (`repeated`), and an input that comes after its turn is dropped: what the players
     * did is what the room simulated, not what arrived when. `sequence` is the number that goes back to the
     * client in the snapshots, which is how it knows which of its inputs the state includes.
     */
    ////////////////////////////////////////////////////////////////////////////
    template<typename Input>
    class InputServer
    {
        public:
            struct Applied
            {
                net::ConnectionId  connection;
                std::uint32_t      sequence;    //!< The last input that is in the state (this one, or the last before if repeated)
                Input              input;
                bool               repeated;    //!< The last input again, because the next was not there
            };

            explicit InputServer(net::Endpoint& endpoint, InputServerConfig config = {})
                : m_config(config)
            {
                endpoint.on<InputPacket>([this, alive = m_alive](net::ConnectionId from, const InputPacket& packet) {
                    if (*alive) {
                        receive(from, packet);
                    }
                });
            }

            ~InputServer(void) { *m_alive = false; }

            InputServer(const InputServer&)            = delete;
            InputServer& operator=(const InputServer&) = delete;

            void addClient(net::ConnectionId connection) { m_clients.emplace(connection, Client{}); }
            void removeClient(net::ConnectionId connection) { m_clients.erase(connection); }

            //! Once per tick: the input to apply for each client that has sent any
            std::vector<Applied> collect(void)
            {
                std::vector<Applied> applied;

                for (auto& [connection, client] : m_clients) {
                    if (!client.last) {
                        continue;    // never heard of it
                    }
                    if (!client.queue.empty() && client.queue.size() > m_config.jitter && client.queue.begin()->first == client.expected) {
                        applied.push_back(Applied{connection, client.expected, client.queue.begin()->second, false});
                        client.last = client.queue.begin()->second;
                        client.queue.erase(client.queue.begin());
                        client.applied = client.expected++;
                    } else if (!client.queue.empty() && client.queue.size() > m_config.jitter) {
                        // Later inputs are here but the next is not: it is lost. Its turn goes to the last input.
                        applied.push_back(Applied{connection, client.expected, *client.last, true});
                        client.applied = client.expected++;
                        ++m_stats.filled;
                    } else {
                        // Nothing yet: the client is late. The last input goes on, and no number is used.
                        applied.push_back(Applied{connection, client.applied, *client.last, true});
                        ++m_stats.filled;
                    }
                }
                return applied;
            }

            const InputServerStats& stats(void) const noexcept { return m_stats; }

        private:
            struct Client
            {
                std::map<std::uint32_t, Input>  queue;
                std::uint32_t                   expected = 1;     //!< The number of the input to apply next
                std::uint32_t                   applied = 0;
                std::optional<Input>            last;             //!< The last input applied: what goes on when the next is missing
                bool                            started = false;
            };

            void receive(net::ConnectionId from, const InputPacket& packet)
            {
                const auto found = m_clients.find(from);

                if (found == m_clients.end()) {
                    return;
                }
                Client& client = found->second;

                try {
                    ByteReader in(packet.data);

                    for (std::uint32_t i = 0; i < packet.count; ++i) {
                        const std::size_t size = in.read<std::uint16_t>();
                        ByteReader one(in.readBytes(size));
                        Input input;

                        net::decode(one, input);
                        if (!one.atEnd()) {
                            throw SerializerError("an input longer than its fields");
                        }
                        const std::uint32_t sequence = packet.firstSequence + i;

                        if (sequence < client.expected || client.queue.count(sequence)) {
                            ++m_stats.duplicates;
                            continue;
                        }
                        if (!client.started) {
                            // The first input that arrives sets where the numbers begin (the client may have been sending a while)
                            client.started = true;
                            client.expected = sequence;
                        }
                        client.queue.emplace(sequence, std::move(input));
                        ++m_stats.received;
                        if (!client.last) {
                            client.last = client.queue.begin()->second;   // (something to repeat before the first is applied: the first itself)
                        }
                    }
                } catch (const SerializerError&) {
                    ++m_stats.malformed;
                }
                while (client.queue.size() > m_config.maxQueued) {
                    client.queue.erase(client.queue.begin());
                    client.expected = client.queue.begin()->first;
                    ++m_stats.dropped;
                }
            }

            InputServerConfig                         m_config;
            std::map<net::ConnectionId, Client>       m_clients;
            InputServerStats                          m_stats;
            std::shared_ptr<bool>                     m_alive = std::make_shared<bool>(true);
    };

}
