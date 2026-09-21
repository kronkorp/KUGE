#pragma once

#include "Endpoint.hpp"
#include "Loopback.hpp"
#include "Scene.hpp"
#include <cstdint>
#include <memory>
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

            //! Polls every endpoint (what the Network stage does)
            void poll(void);

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
    };

    //! Gives a scene a Net resource and the system that polls it (Fixed, stage::Network)
    void installNet(SceneSetup scene);

}
