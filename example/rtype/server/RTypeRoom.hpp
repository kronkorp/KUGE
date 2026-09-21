#pragma once

#include "Input.hpp"
#include "RType.hpp"
#include "RoomScene.hpp"
#include "Stage.hpp"
#include <map>
#include <memory>

namespace rtype
{

    // A room of the game: the rules, the players' inputs, and the replication of the world to the clients.
    // Nothing in it is about a window or a screen.
    class RTypeRoom : public kuge::server::RoomScene
    {
        public:
            using RoomScene::RoomScene;

        protected:
            void onRoomEnter(void) override;
            void onPlayerJoined(const Player& player) override;
            void onPlayerLeft(const Player& player, kuge::net::DisconnectReason reason) override;

        private:
            class Simulate;

            void applyInputs(kw::World& world);
            void afterPhysics(kw::World& world);

            kuge::replication::ReplicationRegistry                              m_registry;
            std::unique_ptr<kuge::replication::ReplicationServer>               m_replication;
            std::unique_ptr<kuge::replication::InputServer<Steer>>              m_inputs;
            std::map<kuge::net::ConnectionId, kw::Entity>                       m_ships;
            bool                                                                m_anyShip = false;
    };

}
