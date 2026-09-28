#include "Loopback.hpp"
#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <random>
#include <stdexcept>

namespace kuge::net
{

    namespace
    {
        double steadySeconds(void)
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
    }

    struct LoopbackNetwork::Impl
    {
        struct Item
        {
            double                     at;      //!< When it arrives
            std::uint64_t              order;   //!< Among the ones that arrive together: the order of sending
            std::vector<std::uint8_t>  bytes;
        };

        struct Link
        {
            std::deque<Item> toServer, toClient;
            bool             clientClosed = false;
            bool             serverClosed = false;
            bool             announced    = false;   //!< The server was told about it
            bool             clientTold   = false;   //!< The client saw it start (or fail)
        };

        struct Listener
        {
            std::vector<std::shared_ptr<Link>> arriving;
            bool                               open = true;
        };

        std::mutex                                 mutex;
        std::function<double(void)>                clock;
        Conditions                                 conditions;
        std::mt19937_64                            rng;
        std::uint64_t                              nextOrder = 0;
        std::uint64_t                              sent = 0, lost = 0;
        std::map<std::string, std::shared_ptr<Listener>> listeners;

        double random(void) { return std::uniform_real_distribution<double>(0.0, 1.0)(rng); }

        // Under the mutex: puts a packet in a queue, as the conditions say
        void push(std::deque<Item>& queue, std::span<const std::uint8_t> packet)
        {
            ++sent;
            if (random() < conditions.loss) {
                ++lost;
                return;
            }
            const int copies = random() < conditions.duplicate ? 2 : 1;

            for (int i = 0; i < copies; ++i) {
                queue.push_back(Item{clock() + conditions.latency + conditions.jitter * random(), nextOrder++,
                    std::vector<std::uint8_t>(packet.begin(), packet.end())});
            }
        }

        // Under the mutex: what has arrived, by arrival time
        std::vector<Item> take(std::deque<Item>& queue)
        {
            const double now = clock();
            std::vector<Item> arrived;

            for (auto it = queue.begin(); it != queue.end();) {
                if (it->at <= now) {
                    arrived.push_back(std::move(*it));
                    it = queue.erase(it);
                } else {
                    ++it;
                }
            }
            std::sort(arrived.begin(), arrived.end(), [](const Item& a, const Item& b) {
                return a.at != b.at ? a.at < b.at : a.order < b.order;
            });
            return arrived;
        }
    };

    namespace
    {
        using Impl = LoopbackNetwork::Impl;

        class LoopbackServer final : public ITransport
        {
            public:
                LoopbackServer(std::shared_ptr<Impl> impl, std::string name, std::shared_ptr<Impl::Listener> listener)
                    : m_impl(std::move(impl)), m_name(std::move(name)), m_listener(std::move(listener)) {}

                ~LoopbackServer(void) override
                {
                    std::lock_guard lock(m_impl->mutex);

                    m_listener->open = false;
                    m_impl->listeners.erase(m_name);
                    for (auto& [id, link] : m_links) {
                        link->serverClosed = true;
                    }
                    for (auto& link : m_listener->arriving) {
                        link->serverClosed = true;
                    }
                }

                void poll(std::vector<TransportEvent>& out) override
                {
                    std::lock_guard lock(m_impl->mutex);

                    for (auto& link : m_listener->arriving) {
                        const ConnectionId id = ++m_lastId;

                        link->announced = true;
                        m_links[id] = link;
                        out.push_back(TransportEvent{TransportEvent::Kind::Connected, id, {}, "loopback '" + m_name + "'"});
                    }
                    m_listener->arriving.clear();
                    for (auto it = m_links.begin(); it != m_links.end();) {
                        auto& link = *it->second;

                        for (auto& item : m_impl->take(link.toServer)) {
                            out.push_back(TransportEvent{TransportEvent::Kind::Packet, it->first, std::move(item.bytes), {}});
                        }
                        // What the client sent before it left is still read
                        if (link.clientClosed && link.toServer.empty()) {
                            out.push_back(TransportEvent{TransportEvent::Kind::Disconnected, it->first, {}, {}});
                            it = m_links.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }

                bool send(ConnectionId connection, std::span<const std::uint8_t> packet) override
                {
                    std::lock_guard lock(m_impl->mutex);
                    const auto found = m_links.find(connection);

                    if (found == m_links.end() || found->second->clientClosed || packet.size() > MAX_PACKET) {
                        return false;
                    }
                    m_impl->push(found->second->toClient, packet);
                    return true;
                }

                void close(ConnectionId connection) override
                {
                    std::lock_guard lock(m_impl->mutex);
                    const auto found = m_links.find(connection);

                    if (found != m_links.end()) {
                        found->second->serverClosed = true;
                        m_links.erase(found);
                    }
                }

                bool reliable(void) const noexcept override { return false; }
                std::string describe(void) const override { return "loopback '" + m_name + "' (server)"; }

            private:
                std::shared_ptr<Impl>                     m_impl;
                std::string                               m_name;
                std::shared_ptr<Impl::Listener>           m_listener;
                std::map<ConnectionId, std::shared_ptr<Impl::Link>> m_links;
                ConnectionId                              m_lastId = 0;
        };

        class LoopbackClient final : public ITransport
        {
            public:
                LoopbackClient(std::shared_ptr<Impl> impl, std::string name, std::shared_ptr<Impl::Link> link)
                    : m_impl(std::move(impl)), m_name(std::move(name)), m_link(std::move(link)) {}

                ~LoopbackClient(void) override
                {
                    std::lock_guard lock(m_impl->mutex);

                    m_link->clientClosed = true;
                }

                void poll(std::vector<TransportEvent>& out) override
                {
                    std::lock_guard lock(m_impl->mutex);
                    auto& link = *m_link;

                    if (!link.clientTold) {
                        link.clientTold = true;
                        if (link.serverClosed && !link.announced && link.toClient.empty()) {
                            m_over = true;   // nobody was listening
                            out.push_back(TransportEvent{TransportEvent::Kind::Disconnected, CLIENT_CONNECTION, {}, {}});
                            return;
                        }
                        out.push_back(TransportEvent{TransportEvent::Kind::Connected, CLIENT_CONNECTION, {}, "loopback '" + m_name + "'"});
                    }
                    if (m_over) {
                        return;
                    }
                    for (auto& item : m_impl->take(link.toClient)) {
                        out.push_back(TransportEvent{TransportEvent::Kind::Packet, CLIENT_CONNECTION, std::move(item.bytes), {}});
                    }
                    if (link.serverClosed && link.toClient.empty()) {
                        m_over = true;
                        out.push_back(TransportEvent{TransportEvent::Kind::Disconnected, CLIENT_CONNECTION, {}, {}});
                    }
                }

                bool send(ConnectionId connection, std::span<const std::uint8_t> packet) override
                {
                    std::lock_guard lock(m_impl->mutex);

                    if (connection != CLIENT_CONNECTION || m_over || m_link->serverClosed || m_link->clientClosed || packet.size() > MAX_PACKET) {
                        return false;
                    }
                    m_impl->push(m_link->toServer, packet);
                    return true;
                }

                void close(ConnectionId) override
                {
                    std::lock_guard lock(m_impl->mutex);

                    m_link->clientClosed = true;
                    m_over = true;
                }

                bool reliable(void) const noexcept override { return false; }
                std::string describe(void) const override { return "loopback '" + m_name + "' (client)"; }

            private:
                std::shared_ptr<Impl>       m_impl;
                std::string                 m_name;
                std::shared_ptr<Impl::Link> m_link;
                bool                        m_over = false;
        };
    }

    LoopbackNetwork::LoopbackNetwork(void) : LoopbackNetwork(Conditions{})
    {
    }

    LoopbackNetwork::LoopbackNetwork(Conditions conditions, std::function<double(void)> clock)
        : m_impl(std::make_shared<Impl>())
    {
        m_impl->clock = clock ? std::move(clock) : std::function<double(void)>(steadySeconds);
        m_impl->conditions = conditions;
        m_impl->rng.seed(conditions.seed);
    }

    LoopbackNetwork::~LoopbackNetwork(void) = default;

    LoopbackNetwork& LoopbackNetwork::global(void)
    {
        static LoopbackNetwork network;

        return network;
    }

    std::unique_ptr<ITransport> LoopbackNetwork::listen(const std::string& name)
    {
        std::lock_guard lock(m_impl->mutex);

        if (m_impl->listeners.count(name)) {
            throw std::runtime_error("loopback: '" + name + "' is already listened to");
        }
        auto listener = std::make_shared<Impl::Listener>();

        m_impl->listeners[name] = listener;
        return std::make_unique<LoopbackServer>(m_impl, name, listener);
    }

    std::unique_ptr<ITransport> LoopbackNetwork::connect(const std::string& name)
    {
        std::lock_guard lock(m_impl->mutex);
        auto link = std::make_shared<Impl::Link>();
        const auto found = m_impl->listeners.find(name);

        if (found == m_impl->listeners.end()) {
            link->serverClosed = true;
        } else {
            found->second->arriving.push_back(link);
        }
        return std::make_unique<LoopbackClient>(m_impl, name, link);
    }

    void LoopbackNetwork::setConditions(const Conditions& conditions)
    {
        std::lock_guard lock(m_impl->mutex);

        m_impl->conditions = conditions;
    }

    std::uint64_t LoopbackNetwork::packetsSent(void) const
    {
        std::lock_guard lock(m_impl->mutex);

        return m_impl->sent;
    }

    std::uint64_t LoopbackNetwork::packetsLost(void) const
    {
        std::lock_guard lock(m_impl->mutex);

        return m_impl->lost;
    }

}
