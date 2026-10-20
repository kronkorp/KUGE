extern "C" {
    #include "kronklab/kronklab.h"
}
#include "NetCommands.hpp"
#include "NetMeter.hpp"
#include "SimulatedTransport.hpp"
#include "net_fixture.hpp"
#include <set>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using namespace kuge::net;
    using Conditions = LoopbackNetwork::Conditions;

    // A transport that does nothing but keep what it is given and give what it is told (reliable or not, as asked)
    class Stub final : public ITransport
    {
        public:
            explicit Stub(bool reliable) : m_reliable(reliable) {}

            void poll(std::vector<TransportEvent>& out) override
            {
                out.insert(out.end(), std::make_move_iterator(incoming.begin()), std::make_move_iterator(incoming.end()));
                incoming.clear();
            }
            bool send(ConnectionId, std::span<const std::uint8_t> packet) override
            {
                sent.emplace_back(packet.begin(), packet.end());
                return true;
            }
            void close(ConnectionId) override {}
            bool reliable(void) const noexcept override { return m_reliable; }
            std::string describe(void) const override { return "stub"; }

            std::vector<TransportEvent>               incoming;
            std::vector<std::vector<std::uint8_t>>    sent;

        private:
            bool m_reliable;
    };

    struct Wrapped
    {
        Stub*              stub;
        double             now = 0.0;
        SimulatedTransport sim;

        Wrapped(Conditions c, bool reliable = false) : stub(new Stub(reliable)), sim(std::unique_ptr<ITransport>(stub), c, [this] { return now; }) {}

        void sendNumbers(int count)
        {
            for (int i = 0; i < count; ++i) {
                const std::vector<std::uint8_t> packet{static_cast<std::uint8_t>(i & 0xFF), static_cast<std::uint8_t>(i >> 8)};

                sim.send(1, packet);
            }
        }

        void receiveNumbers(int count)
        {
            for (int i = 0; i < count; ++i) {
                stub->incoming.push_back({TransportEvent::Kind::Packet, 1, {static_cast<std::uint8_t>(i & 0xFF), static_cast<std::uint8_t>(i >> 8)}, {}});
            }
        }

        std::vector<int> arrived(void)
        {
            std::vector<TransportEvent> events;
            std::vector<int> numbers;

            sim.poll(events);
            for (const auto& event : events) {
                if (event.kind == TransportEvent::Kind::Packet) {
                    numbers.push_back(event.packet[0] | (event.packet[1] << 8));
                }
            }
            return numbers;
        }

        static std::vector<int> numbersOf(const std::vector<std::vector<std::uint8_t>>& packets)
        {
            std::vector<int> numbers;

            for (const auto& packet : packets) {
                numbers.push_back(packet[0] | (packet[1] << 8));
            }
            return numbers;
        }
    };
}

Test(simulated, loss_is_about_right)
{
    Wrapped a({.loss = 0.3, .seed = 5});

    a.sendNumbers(1000);
    const std::size_t got = a.stub->sent.size();

    Assert(got > 650 && got < 750, "30 %% lost of 1000: %zu arrived", got);
    AssertEq(a.sim.dropped() + got, 1000u, "the dropped are counted");
    Wrapped b({.loss = 0.3, .seed = 5});

    b.sendNumbers(1000);
    Assert(Wrapped::numbersOf(a.stub->sent) == Wrapped::numbersOf(b.stub->sent), "the same seed loses the same packets");
    Wrapped c({.loss = 0.3, .seed = 6});

    c.sendNumbers(1000);
    Assert(Wrapped::numbersOf(a.stub->sent) != Wrapped::numbersOf(c.stub->sent), "another seed, other ones");
    Wrapped incoming({.loss = 0.3, .seed = 5});

    incoming.receiveNumbers(1000);
    const auto heard = incoming.arrived();
    Assert(heard.size() > 650 && heard.size() < 750, "what comes in is lost too: %zu", heard.size());
}

Test(simulated, packets_are_late)
{
    Wrapped a({.latency = 0.5});

    a.sendNumbers(2);
    AssertEq(a.stub->sent.size(), 0u, "nothing leaves at once");
    a.now = 0.4;
    a.sim.poll(*std::make_unique<std::vector<TransportEvent>>());
    AssertEq(a.stub->sent.size(), 0u, "nor before the latency");
    a.now = 0.5;
    std::vector<TransportEvent> none;

    a.sim.poll(none);
    AssertEq(a.stub->sent.size(), 2u, "they leave when it is over, in order");
    a.receiveNumbers(2);
    AssertEq(a.arrived().size(), 0u, "what comes in is held too...");
    a.now = 1.0;
    AssertEq(a.arrived().size(), 2u, "...for the latency");
}

Test(simulated, repeats_and_reordering)
{
    Wrapped twice({.duplicate = 1.0});

    twice.sendNumbers(5);
    AssertEq(twice.stub->sent.size(), 10u, "every packet twice");
    Wrapped mixed({.latency = 0.1, .jitter = 0.5, .seed = 3});

    mixed.sendNumbers(50);
    mixed.now = 10.0;
    std::vector<TransportEvent> none;

    mixed.sim.poll(none);
    const auto order = Wrapped::numbersOf(mixed.stub->sent);

    AssertEq(order.size(), 50u, "all of them");
    Assert(!std::is_sorted(order.begin(), order.end()), "but not in the order they were sent: jitter lets them pass each other");
    AssertEq(std::set<int>(order.begin(), order.end()).size(), 50u, "each one once");
}

Test(simulated, a_reliable_one_loses_nothing)
{
    Wrapped tcp({.loss = 0.9, .duplicate = 0.9, .latency = 0.2, .jitter = 1.0, .seed = 2}, true);

    tcp.sendNumbers(100);
    tcp.now = 1.0;
    std::vector<TransportEvent> none;

    tcp.sim.poll(none);
    const auto order = Wrapped::numbersOf(tcp.stub->sent);

    AssertEq(order.size(), 100u, "a TCP-like transport keeps every packet once...");
    Assert(std::is_sorted(order.begin(), order.end()), "...in order: only the delay applies");
    AssertEq(tcp.sim.dropped(), 0u, "(none dropped)");
    Assert(tcp.sim.describe().find("simulated stub") == 0 && tcp.sim.reliable(), "it says what it wraps");
}

Test(simulated, an_endpoint_still_delivers)
{
    // The reliable channel over a transport that loses 30 % of the packets and repeats 10 %: the real thing
    Sim sim;
    auto server = sim.server();
    EndpointConfig config;

    config.simulate = Conditions{.loss = 0.3, .duplicate = 0.1, .latency = 0.02, .jitter = 0.02, .seed = 11};
    auto client = sim.client("room", config);
    std::vector<std::uint32_t> got;

    server->on<Number>([&](ConnectionId, const Number& number) { got.push_back(number.value); });
    Assert(sim.until([&] { return client->connected(); }, 60.0, *server, *client), "it connects through the bad network");
    std::uint32_t sent = 0;
    const double end = sim.now + 60.0;

    while (got.size() < 200 && sim.now < end) {
        for (int i = 0; i < 3 && sent < 200; ++i) {
            if (client->send(CLIENT_CONNECTION, Number{sent}, Channel::Reliable)) {
                ++sent;
            }
        }
        sim.now += 0.005;
        server->poll();
        client->poll();
    }
    AssertEq(got.size(), 200u, "all 200 arrive");
    bool inOrder = true;

    for (std::uint32_t i = 0; i < got.size(); ++i) {
        inOrder = inOrder && got[i] == i;
    }
    Assert(inOrder, "once and in order");
    Assert(client->stats().resends > 0, "and the endpoint had to send some again: %llu", static_cast<unsigned long long>(client->stats().resends));
    // Changed while playing: lifted
    client->setSimulation(std::nullopt);
    const auto resends = client->stats().resends;

    got.clear();
    for (std::uint32_t i = 0; i < 20; ++i) {
        client->send(CLIENT_CONNECTION, Number{1000 + i}, Channel::Reliable);
    }
    sim.run(1.0, 0.01, *server, *client);
    AssertEq(got.size(), 20u, "without the simulation the messages arrive at once");
    Assert(client->stats().resends - resends < 3, "with no more resends");
}

Test(netmeter, rates_and_rtt)
{
    Sim sim;
    auto server = sim.server();
    auto client = sim.client();
    NetMeter meter(1.0);

    server->on<Number>([](ConnectionId, const Number&) {});
    Assert(sim.until([&] { return client->connected(); }, 5.0, *server, *client), "connected");
    meter.update(*client, CLIENT_CONNECTION, sim.now);
    for (int second = 0; second < 3; ++second) {
        for (int i = 0; i < 100; ++i) {
            client->send(CLIENT_CONNECTION, Number{1}, i % 10 == 0 ? Channel::Reliable : Channel::Unreliable);   // (a reliable one is acknowledged: a round trip)
            sim.run(0.01, 0.01, *server, *client);
        }
        meter.update(*client, CLIENT_CONNECTION, sim.now);
    }
    const kuge::NetReport& report = meter.report();

    Assert(report.sentPerSecond > 100.0 && report.sentPerSecond < 20000.0, "bytes a second: %f", report.sentPerSecond);
    Assert(report.packetsSentPerSecond >= 90.0, "packets a second: %f", report.packetsSentPerSecond);
    Assert(report.receivedPerSecond > 0.0, "and what came back (keep-alives)");
    Assert(report.rttSeconds >= 0.0 && report.rttLow >= 0.0 && report.rttHigh >= report.rttLow, "a round trip with its low and high");
    NetMeter server_meter;

    server_meter.update(*server, std::nullopt, sim.now);
    AssertEq(server_meter.report().rttSeconds, -1.0, "a server endpoint has no single round trip");
    Assert(kuge::describe(report).find("rtt ") == 0 && kuge::describe(report).find("KiB/s") != std::string::npos, "one line for a log: %s", kuge::describe(report).c_str());
}

Test(netcommands, change_the_network)
{
    Sim sim;
    auto server = sim.server();
    auto client = sim.client();
    kuge::Cvars cvars;
    kuge::Commands commands(cvars);
    Endpoint* current = client.get();

    registerNetCommands(commands, [&] { return current; });
    Assert(sim.until([&] { return client->connected(); }, 5.0, *server, *client), "connected");
    Assert(commands.run("net_lag 100").find("lag 100 ms") != std::string::npos, "net_lag answers with the state");
    std::vector<std::uint32_t> got;

    server->on<Number>([&](ConnectionId, const Number& number) { got.push_back(number.value); });
    client->send(CLIENT_CONNECTION, Number{1}, Channel::Reliable);
    sim.run(0.04, 0.01, *server, *client);
    AssertEq(got.size(), 0u, "100 ms of round trip is 50 ms each way: not there at 40 ms");
    sim.run(0.04, 0.01, *server, *client);
    AssertEq(got.size(), 1u, "but at 80");
    commands.run("net_loss 100");
    client->send(CLIENT_CONNECTION, Number{2}, Channel::Unreliable);
    sim.run(0.5, 0.01, *server, *client);
    AssertEq(got.size(), 1u, "all lost: nothing arrives");
    Assert(commands.run("net_loss 500").find("net_loss <%>") == 0 && commands.run("net_lag fast").find("net_lag <ms>") == 0, "bad values say how to use it");
    Assert(commands.run("net_sim off").find("real network") != std::string::npos, "off");
    client->send(CLIENT_CONNECTION, Number{3}, Channel::Reliable);
    sim.run(0.1, 0.01, *server, *client);
    Assert(!got.empty() && got.back() == 3, "the real network is back");
    Assert(commands.run("net_stats").find("rtt") == 0, "net_stats");
    current = nullptr;
    AssertStrEq(commands.run("net_lag 10").c_str(), "not connected", "no endpoint: it says so");
}
