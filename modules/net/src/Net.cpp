#include "Net.hpp"
#include "SocketTransport.hpp"
#include "Stage.hpp"
#include "Time.hpp"
#include <algorithm>
#include <stdexcept>

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

    bool Net::pollTick(std::uint64_t tick)
    {
        if (m_lastTick == tick) {
            return false;
        }
        m_lastTick = tick;
        poll();
        return true;
    }

    namespace
    {
        class PollNet final : public kw::ISystem
        {
            public:
                explicit PollNet(std::shared_ptr<Net> keep = {}) : m_keep(std::move(keep)) {}

                bool handle(kw::World& world) override
                {
                    world.getResource<Ref<Net>>()->pollTick(world.getResource<Time>().tick);
                    return true;
                }

            private:
                std::shared_ptr<Net> m_keep;   // (a Net of the game: it is not released before the system)
        };
    }

    void installNet(SceneSetup scene)
    {
        Net& net = scene.world().addResource<Net>();

        scene.world().addResource<Ref<Net>>(net);
        scene.addSystem(kw::Schedule::Fixed, stage::Network, std::make_unique<PollNet>());
    }

    void installNet(SceneSetup scene, std::shared_ptr<Net> shared)
    {
        if (!shared) {
            throw std::invalid_argument("installNet: no Net to share");
        }
        scene.world().addResource<Ref<Net>>(*shared);
        scene.addSystem(kw::Schedule::Fixed, stage::Network, std::make_unique<PollNet>(std::move(shared)));
    }

}
