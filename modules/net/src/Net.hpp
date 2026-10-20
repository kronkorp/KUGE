#pragma once

#include "Endpoint.hpp"
#include "Loopback.hpp"
#include "Ref.hpp"
#include "Scene.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kuge::net
{

    enum class Protocol : std::uint8_t {
        Tcp,   //!< A stream: reliable and ordered, made into packets
        Udp,   //!< Datagrams: fast, may be lost (an Endpoint adds the reliable channel)
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The endpoints of a scene: what it listens on, and what it
     *         connects to. A resource of the World (see installNet()).
     *
     *     kuge::net::installNet(setup());
     *     auto& net = world().getResource<kuge::net::Net>();
     *
     *     auto& server = net.listen(kuge::net::Protocol::Udp, 4242);
     *     server.on<Join>([&](kuge::net::ConnectionId from, const Join& join) { ... });
     *
     *     auto& client = net.connect(kuge::net::Protocol::Tcp, "127.0.0.1", 4242);
     *     client.onConnected([&](kuge::net::ConnectionId) { client.send(...); });
     *
     * The endpoints are polled by the system that installNet() adds, in the
     * Network stage of each fixed tick, on the thread of the scene: an endpoint
     * never leaves the thread that made it. Two scenes (two threads) that must talk
     * do it through a real address, or through a LoopbackNetwork (a client and a
     * server in the same process: a player who hosts the match).
     *
     * The endpoints are destroyed with the scene (their peers are told).
     */
    ////////////////////////////////////////////////////////////////////////////
    class Net
    {
        public:
            Net(void) = default;
            Net(const Net&)            = delete;
            Net& operator=(const Net&) = delete;

            //! Accepts connections on a port (all the addresses of the machine)
            //! @throw std::runtime_error if the port cannot be used
            Endpoint& listen(Protocol protocol, std::uint16_t port, EndpointConfig config = {});

            //! Connects to a server (IPv4: "localhost" or a dotted address). It never throws for an
            //! unreachable server: the endpoint reports a disconnection.
            Endpoint& connect(Protocol protocol, const std::string& host, std::uint16_t port, EndpointConfig config = {});

            //! Accepts connections at a name of a LoopbackNetwork (the process-wide one if not given)
            Endpoint& listen(const std::string& name, LoopbackNetwork& network = LoopbackNetwork::global(), EndpointConfig config = {});
            Endpoint& connect(const std::string& name, LoopbackNetwork& network = LoopbackNetwork::global(), EndpointConfig config = {});

            //! Ends an endpoint (its peers are told). The reference is not valid afterwards.
            void remove(Endpoint& endpoint);

            //! Polls every endpoint
            void poll(void);

            //! Same, but only the first time for a tick of the clock (Time::tick): two scenes that poll the
            //! same Net (one over the other, see Scene::overlay()) poll it once. This is what the Network
            //! stage does.
            //! @return  false if this tick was already polled
            bool pollTick(std::uint64_t tick);

            //! Endpoints that are there (one removed while polling is already not)
            std::size_t count(void) const noexcept;

        private:
            struct Slot
            {
                std::unique_ptr<Endpoint> endpoint;
                bool                      removed = false;
            };

            Endpoint& add(std::unique_ptr<ITransport> transport, Role role, EndpointConfig config);

            std::vector<std::unique_ptr<Slot>>  m_endpoints;
            bool                                m_polling = false;
            std::optional<std::uint64_t>        m_lastTick;
    };

    //! Gives a scene a Net resource and the system that polls it (Fixed, stage::Network). The Net is also
    //! the resource kuge::Ref<Net>, the same way whoever owns it.
    void installNet(SceneSetup scene);

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Same, with a Net that the game keeps: it outlives the scene, and so do its endpoints
     *
     * The connection to a lobby must survive the menu, the game and the way back. The scene only polls the
     * Net, which is the resource kuge::Ref<Net> of its World (not a Net: there is none), and the system keeps a
     * reference to it, so it is released after the scene whatever the order of destruction.
     *
     *     auto net = std::make_shared<kuge::net::Net>();                // in Main, or in a Session
     *     engine.scenes().change<Menu>(net);
     *
     *     void Menu::onEnter() { kuge::net::installNet(setup(), m_net); ... }
     *     auto& net = *world().getResource<kuge::Ref<kuge::net::Net>>();
     *
     * A MatchmakingClient(net) made over it lives as long as the Net does, not as the scene.
     */
    ////////////////////////////////////////////////////////////////////////////
    void installNet(SceneSetup scene, std::shared_ptr<Net> shared);

}
