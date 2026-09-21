#include "Net.hpp"
#include "SocketTransport.hpp"
#include "Stage.hpp"
#include <algorithm>

namespace kuge::net
{

    Endpoint& Net::add(std::unique_ptr<ITransport> transport, Role role, EndpointConfig config)
    {
        auto slot = std::make_unique<Slot>();

        slot->endpoint = std::make_unique<Endpoint>(std::move(transport), role, std::move(config));
        m_endpoints.push_back(std::move(slot));
        return *m_endpoints.back()->endpoint;
    }

    Endpoint& Net::listen(Protocol protocol, std::uint16_t port, EndpointConfig config)
    {
        return add(protocol == Protocol::Tcp ? makeTcpServer(port) : makeUdpServer(port), Role::Server, std::move(config));
    }

    Endpoint& Net::connect(Protocol protocol, const std::string& host, std::uint16_t port, EndpointConfig config)
    {
        return add(protocol == Protocol::Tcp ? makeTcpClient(host, port) : makeUdpClient(host, port), Role::Client, std::move(config));
    }

    Endpoint& Net::listen(const std::string& name, LoopbackNetwork& network, EndpointConfig config)
    {
        return add(network.listen(name), Role::Server, std::move(config));
    }

    Endpoint& Net::connect(const std::string& name, LoopbackNetwork& network, EndpointConfig config)
    {
        return add(network.connect(name), Role::Client, std::move(config));
    }

    std::size_t Net::count(void) const noexcept
    {
        return static_cast<std::size_t>(std::count_if(m_endpoints.begin(), m_endpoints.end(), [](const auto& slot) { return !slot->removed; }));
    }

    void Net::remove(Endpoint& endpoint)
    {
        for (auto& slot : m_endpoints) {
            if (slot->endpoint.get() == &endpoint) {
                slot->removed = true;
            }
        }
        if (!m_polling) {
            std::erase_if(m_endpoints, [](const auto& slot) { return slot->removed; });
        }
    }

    void Net::poll(void)
    {
        // A handler may add or remove endpoints: only the ones that were there are polled, and the
        // removed ones go when the loop is over
        const std::size_t count = m_endpoints.size();

        m_polling = true;
        try {
            for (std::size_t i = 0; i < count; ++i) {
                if (!m_endpoints[i]->removed) {
                    m_endpoints[i]->endpoint->poll();
                }
            }
        } catch (...) {
            m_polling = false;
            throw;
        }
        m_polling = false;
        std::erase_if(m_endpoints, [](const auto& slot) { return slot->removed; });
    }

    namespace
    {
        class PollNet final : public kw::ISystem
        {
            public:
                bool handle(kw::World& world) override
                {
                    world.getResource<Net>().poll();
                    return true;
                }
        };
    }

    void installNet(SceneSetup scene)
    {
        scene.world().addResource<Net>();
        scene.addSystem(kw::Schedule::Fixed, stage::Network, std::make_unique<PollNet>());
    }

}
