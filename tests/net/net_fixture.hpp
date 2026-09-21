#pragma once

#include "Endpoint.hpp"
#include "Loopback.hpp"
#include <memory>

// Messages of the tests
struct Chat
{
    KUGE_MESSAGE(Chat, from, text)
    std::uint32_t from = 0;
    std::string   text;
};

struct Number
{
    KUGE_MESSAGE(Number, value)
    std::uint32_t value = 0;
};

struct Ping { KUGE_MESSAGE(Ping) };

// A world with its own time: tests make it pass, so that a timeout of 10 seconds takes no time at all
struct Sim
{
    double                         now = 0.0;
    kuge::net::LoopbackNetwork     network;

    explicit Sim(kuge::net::LoopbackNetwork::Conditions conditions = {})
        : network(conditions, [this] { return now; }) {}

    kuge::net::EndpointConfig config(void)
    {
        kuge::net::EndpointConfig c;

        c.clock = [this] { return now; };
        return c;
    }

    std::unique_ptr<kuge::net::Endpoint> server(const char* name = "room", kuge::net::EndpointConfig c = {})
    {
        c.clock = [this] { return now; };
        return std::make_unique<kuge::net::Endpoint>(network.listen(name), kuge::net::Role::Server, c);
    }

    std::unique_ptr<kuge::net::Endpoint> client(const char* name = "room", kuge::net::EndpointConfig c = {})
    {
        c.clock = [this] { return now; };
        return std::make_unique<kuge::net::Endpoint>(network.connect(name), kuge::net::Role::Client, c);
    }

    // Lets time pass, polling everyone every step
    template<typename... Ends>
    void run(double seconds, double step, Ends&... ends)
    {
        const double end = now + seconds;

        while (now < end) {
            now += step;
            (ends.poll(), ...);
        }
    }

    // Until done() (or the time is up)
    template<typename Done, typename... Ends>
    bool until(Done done, double seconds, Ends&... ends)
    {
        const double end = now + seconds;

        while (now < end && !done()) {
            now += 0.01;
            (ends.poll(), ...);
        }
        return done();
    }
};
