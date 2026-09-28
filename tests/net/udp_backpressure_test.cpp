extern "C" {
    #include "kronklab/kronklab.h"
}
#include "SocketTransport.hpp"
#include "net_fixture.hpp"
#include <cerrno>
#include <chrono>
#include <random>
#include <sys/socket.h>
#include <thread>
#include <vector>

// NOTE: kronklab test names are limited to 31 characters.
//
// A socket whose send buffer is full says "try again" (EAGAIN). That never happens on the loopback
// interface of a test machine, so this program is linked with send() and sendto() wrapped
// (-Wl,--wrap=send,--wrap=sendto): a test says how many of the next datagrams meet a full buffer.

namespace
{
    int g_fullSends = 0;     // the next send() / sendto() calls that fail with EAGAIN
}

extern "C" {
    ssize_t __real_send(int fd, const void* data, size_t size, int flags);
    ssize_t __real_sendto(int fd, const void* data, size_t size, int flags, const struct sockaddr* to, socklen_t length);

    ssize_t __wrap_send(int fd, const void* data, size_t size, int flags)
    {
        if (g_fullSends > 0) {
            --g_fullSends;
            errno = EAGAIN;
            return -1;
        }
        return __real_send(fd, data, size, flags);
    }

    ssize_t __wrap_sendto(int fd, const void* data, size_t size, int flags, const struct sockaddr* to, socklen_t length)
    {
        if (g_fullSends > 0) {
            --g_fullSends;
            errno = EAGAIN;
            return -1;
        }
        return __real_sendto(fd, data, size, flags, to, length);
    }
}

namespace
{
    using namespace kuge::net;
    using Clock = std::chrono::steady_clock;

    // A UDP server on a free port, and a client connected to it
    struct Pair
    {
        std::unique_ptr<Endpoint> server;
        std::unique_ptr<Endpoint> client;

        Pair(void)
        {
            thread_local std::mt19937 rng(std::random_device{}());

            for (int attempt = 0; attempt < 200 && !server; ++attempt) {
                const auto port = static_cast<std::uint16_t>(20000 + rng() % 30000);

                try {
                    server = std::make_unique<Endpoint>(makeUdpServer(port), Role::Server);
                    client = std::make_unique<Endpoint>(makeUdpClient("127.0.0.1", port), Role::Client);
                } catch (const std::runtime_error&) {
                }
            }
        }

        template<typename Done>
        bool until(Done done, double seconds = 5.0)
        {
            const auto end = Clock::now() + std::chrono::duration<double>(seconds);

            while (!done() && Clock::now() < end) {
                server->poll();
                client->poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return done();
        }
    };
}

Test(udp_backpressure, server_to_client_under_load)
{
    Pair net;
    std::vector<std::uint32_t> received;

    net.client->on<Number>([&](ConnectionId, const Number& number) { received.push_back(number.value); });
    Assert(net.until([&] { return net.client->connected() && net.server->connected(); }), "connected");
    // The first datagram meets a full buffer. The two after it must not be glued to it in one datagram
    // (the other side would read three messages as one, drop it, and never see the first one again).
    g_fullSends = 1;
    for (std::uint32_t i = 1; i <= 3; ++i) {
        Assert(net.server->send(net.server->connections().front(), Number{i}), "sent %u", i);
    }
    Assert(net.until([&] { return received.size() >= 3; }), "the three messages arrive (%zu did)", received.size());
    Assert(received == std::vector<std::uint32_t>({1, 2, 3}), "once each, and in order");
    AssertEq(net.client->stats().malformed, 0, "and nothing arrived that made no sense");
}

Test(udp_backpressure, client_to_server_under_load)
{
    Pair net;
    std::vector<std::uint32_t> received;

    net.server->on<Number>([&](ConnectionId, const Number& number) { received.push_back(number.value); });
    Assert(net.until([&] { return net.client->connected() && net.server->connected(); }), "connected");
    g_fullSends = 1;
    for (std::uint32_t i = 1; i <= 3; ++i) {
        Assert(net.client->send(CLIENT_CONNECTION, Number{i}), "sent %u", i);
    }
    Assert(net.until([&] { return received.size() >= 3; }), "the three messages arrive (%zu did)", received.size());
    Assert(received == std::vector<std::uint32_t>({1, 2, 3}), "once each, and in order");
    AssertEq(net.server->stats().malformed, 0, "and nothing arrived that made no sense");
}
