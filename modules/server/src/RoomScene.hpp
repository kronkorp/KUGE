#pragma once

#include "Chat.hpp"
#include "ChatRelay.hpp"
#include "Net.hpp"
#include "NetMeter.hpp"
#include "Scene.hpp"
#include "ServerConfig.hpp"
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kuge::server
{

    class RoomHousekeeping;

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A room of the server: one game, with its players. Derive from it,
     *         and make the game.
     *
     *     class DeathmatchRoom : public kuge::server::RoomScene
     *     {
     *         public:
     *             using RoomScene::RoomScene;
     *
     *         protected:
     *             void onRoomEnter() override
     *             {
     *                 on<Input>([this](const Player& who, const Input& input) { ... });
     *                 addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Simulate>());
     *             }
     *             void onPlayerJoined(const Player& player) override { ... send(player.networkId, Snapshot{...}); }
     *             void onPlayerLeft(const Player& player, kuge::net::DisconnectReason) override { ... }
     *     };
     *
     *     server.addRoomType<DeathmatchRoom>("deathmatch", {.maxPlayers = 8});
     *
     * The room is a scene: it runs on its own thread (or a worker), and only talks to
     * the others through messages. It listens on an address of its own; a client comes
     * with a token that the lobby gave it, says Hello, and is welcomed with a NetworkId
     * (`Player::networkId`): how the game names this player in this room. Anything the
     * clients send before that is ignored.
     *
     * The room ends when the game calls finish(): the players are told, and the scene
     * is left once their last messages had time to go. It also ends when it stays empty
     * too long (RoomTypeConfig::idleTimeout), or when the server stops.
     */
    ////////////////////////////////////////////////////////////////////////////
    class RoomScene : public Scene
    {
        public:
            //! A player who is in the room (the client said Hello with a good token)
            struct Player
            {
                std::uint32_t      playerId;     //!< Server-wide
                std::string        name;
                std::uint32_t      networkId;    //!< In this room: 1, 2, 3... (never used again)
                net::ConnectionId  connection;
            };

            explicit RoomScene(RoomInit init);
            ~RoomScene(void) override;

            void onEnter(void) final;
            void onExit(void) final;
            void onMessage(const Message& message) final;

        protected:
            // -- What the game does ---------------------------------------------------------
            //! The room listens: add the systems, and say what to do with the messages of the game
            virtual void onRoomEnter(void) {}
            virtual void onRoomExit(void) {}
            virtual void onPlayerJoined(const Player&) {}
            virtual void onPlayerLeft(const Player&, net::DisconnectReason) {}
            //! A message of another scene (the lobby's own are not given)
            virtual void onRoomMessage(const Message&) {}

            // -- What the game may use ------------------------------------------------------
            const RoomInit&                       init(void) const noexcept { return m_init; }
            net::Endpoint&                        endpoint(void) noexcept { return *m_endpoint; }
            const std::map<std::uint32_t, Player>& players(void) const noexcept { return m_players; }   //!< By network id
            const Player*                         player(net::ConnectionId connection) const;
            const Player*                         playerByNetworkId(std::uint32_t networkId) const;
            double                                now(void) const { return m_init.server.now(); }

            //! Says what to do with a message of the game, from a player of the room
            template<net::NetMessage T>
            void on(std::function<void(const Player&, const T&)> handler)
            {
                m_endpoint->on<T>([this, handler = std::move(handler)](net::ConnectionId from, const T& message) {
                    if (const Player* who = player(from)) {
                        handler(*who, message);
                    }
                });
            }

            template<net::NetMessage T>
            bool send(std::uint32_t networkId, const T& message, net::Channel channel = net::Channel::Reliable)
            {
                const Player* who = playerByNetworkId(networkId);

                return who && m_endpoint->send(who->connection, message, channel);
            }

            //! To every player (not to a client that has not said hello yet)
            template<net::NetMessage T>
            std::size_t broadcast(const T& message, net::Channel channel = net::Channel::Reliable)
            {
                std::size_t count = 0;

                for (const auto& [id, who] : m_players) {
                    count += m_endpoint->send(who.connection, message, channel) ? 1 : 0;
                }
                return count;
            }

            //! Sends a player away
            void kick(std::uint32_t networkId, net::RoomEnd reason = net::RoomEnd::Kicked);

            //! Opens or closes the room to newcomers (it is open when made). A room that is not open stays in the lists
            //! (`RoomInfo::open` is false, so a menu can grey it out) but the lobby gives no place in it: a client that asks
            //! for it by id gets `JoinError::Started`, and the automatic matchmaking (`join`) makes another room instead.
            //! The players already in it, and the ones on their way with a token, are not affected. Use it when the game
            //! starts (a match, a round, a race). Tells the lobby, which learns it a moment later: a join that is already
            //! on its way can still get in.
            void setOpen(bool open);
            bool isOpen(void) const noexcept { return m_open; }

            //! Turns the text chat on: a `ChatSend` from a player is checked by a ChatRelay (not empty, not too long, valid UTF-8,
            //! no control character, a rate limit per player) and relayed to everybody as a `ChatLine` whose `from` is the network id
            //! of the player. Call it in onRoomEnter().
            void enableChat(ChatConfig config = {});

            //! A line that passed the checks, before it is relayed: the game may refuse it (return nothing: a muted player, a
            //! command) or change it (a name, a filter of words). By default it goes as it is.
            virtual std::optional<std::string> onChatLine(const Player&, std::string text) { return text; }

            //! Ends the game: the players are told, then the room is left. Only the first call counts.
            //! The players are told `ServerConfig::closeDelay` later, not at once: the room goes on running, and
            //! what the game does as it ends (a result, a last state) is in the snapshots that leave first. A client
            //! that hears "room closed" lets go of the room, and would never have seen it.
            void finish(net::RoomEnd reason = net::RoomEnd::GameOver);
            bool finishing(void) const noexcept { return m_finishing; }

        private:
            friend class RoomHousekeeping;

            struct Expected { std::string name; std::uint32_t playerId; double expires; };
            struct WaitingHello { net::ConnectionId connection; std::uint64_t token; double since; };

            template<typename T>
            void tellLobby(T&& what)
            {
                ctx().parent().send(std::forward<T>(what));
            }

            void handleHello(net::ConnectionId from, std::uint64_t token);
            bool admit(net::ConnectionId from, std::uint64_t token);
            void reject(net::ConnectionId from, net::HelloError why);
            void housekeeping(void);
            void announceEnd(void);
            void completeFinish(void);
            void playerGone(net::ConnectionId connection, net::DisconnectReason reason);

            RoomInit                                 m_init;
            net::Endpoint*                           m_endpoint = nullptr;
            std::map<std::uint32_t, Player>          m_players;
            std::map<std::uint64_t, Expected>        m_expected;
            std::unique_ptr<ChatRelay>               m_chat;
            net::NetMeter                            m_meter{5.0};
            double                                   m_nextStats = 0.0;
            std::map<net::ConnectionId, double>      m_unknown;      //!< Connected, no hello yet: since when
            std::vector<WaitingHello>                m_waiting;      //!< Hello with a token the room may not know yet
            std::uint32_t                            m_nextNetworkId = 1;
            double                                   m_lastActive = 0.0;
            bool                                     m_open = true;
            bool                                     m_finishing = false;
            bool                                     m_ended = false;
            bool                                     m_announced = false;   //!< The players were told (RoomClosed)
            net::RoomEnd                             m_endReason = net::RoomEnd::GameOver;
            double                                   m_announceAt = 0.0;    //!< When they are told
            double                                   m_finishAt = 0.0;
    };

}
