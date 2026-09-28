extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Reliable.hpp"
#include <algorithm>
#include <random>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using Channel = kuge::net::ReliableChannel;

    std::vector<std::uint8_t> number(std::uint32_t n)
    {
        return {static_cast<std::uint8_t>(n), static_cast<std::uint8_t>(n >> 8), static_cast<std::uint8_t>(n >> 16), static_cast<std::uint8_t>(n >> 24)};
    }

    std::uint32_t valueOf(const std::vector<std::uint8_t>& bytes)
    {
        return bytes[0] | bytes[1] << 8 | bytes[2] << 16 | static_cast<std::uint32_t>(bytes[3]) << 24;
    }

    // Two channels and a network between them that loses, repeats and delays packets
    struct Pair
    {
        struct Flight { double at; bool toReceiver; std::uint32_t seq; std::vector<std::uint8_t> payload; std::uint32_t ack, bits; };

        Channel                    sender, receiver;
        std::vector<Flight>        flights;
        std::mt19937               rng;
        double                     loss, duplicate, latency, jitter;
        double                     now = 0.0;
        std::vector<std::uint32_t> got;

        Pair(Channel::Config config, double lossRate, double dup, double delay, double spread, unsigned seed)
            : sender(config), receiver(config), rng(seed), loss(lossRate), duplicate(dup), latency(delay), jitter(spread) {}

        double random(void) { return std::uniform_real_distribution<double>(0.0, 1.0)(rng); }

        void put(Flight flight)
        {
            if (random() < loss) {
                return;
            }
            for (int copies = random() < duplicate ? 2 : 1; copies > 0; --copies) {
                Flight f = flight;

                f.at = now + latency + jitter * random();
                flights.push_back(std::move(f));
            }
        }

        // One step of time: what is due arrives, then both sides send what they have to
        void step(double dt)
        {
            now += dt;
            std::vector<Flight> due;

            for (auto it = flights.begin(); it != flights.end();) {
                if (it->at <= now) { due.push_back(std::move(*it)); it = flights.erase(it); } else { ++it; }
            }
            for (auto& f : due) {
                if (f.toReceiver) {
                    receiver.onData(f.seq, std::move(f.payload));
                } else {
                    sender.onAck(f.ack, f.bits, now);
                }
            }
            for (auto& message : receiver.takeDelivered()) {
                got.push_back(valueOf(message));
            }
            std::vector<Channel::Outgoing> out;

            sender.collect(now, out);
            for (const auto& o : out) {
                put(Flight{0, true, o.seq, *o.payload, 0, 0});
            }
            if (receiver.ackPending()) {
                receiver.ackSent();
                put(Flight{0, false, 0, {}, receiver.ack(), receiver.ackBits()});
            }
        }
    };

    bool everythingArrived(Pair& pair, std::uint32_t count, double seconds)
    {
        for (double t = 0; t < seconds && pair.got.size() < count; t += 0.005) {
            pair.step(0.005);
        }
        return pair.got.size() == count;
    }

    bool inOrderOnce(const std::vector<std::uint32_t>& got, std::uint32_t count)
    {
        if (got.size() != count) {
            return false;
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            if (got[i] != i) {
                return false;
            }
        }
        return true;
    }
}

Test(reliable, in_order_no_loss)
{
    Pair pair(Channel::Config{}, 0, 0, 0.01, 0, 1);

    for (std::uint32_t i = 0; i < 100; ++i) { pair.sender.queue(number(i)); }
    Assert(everythingArrived(pair, 100, 5.0), "all arrived");
    Assert(inOrderOnce(pair.got, 100), "in order, once");
    for (int i = 0; i < 20; ++i) { pair.step(0.01); }   // the last acks come back
    AssertEq(pair.sender.resends(), 0, "nothing was sent again");
    Assert(pair.sender.idle(), "and the sender has nothing left in flight");
}

Test(reliable, reordered_and_repeated)
{
    Channel receiver;

    // 0..9 in a shuffled order, each some times
    std::vector<std::uint32_t> order = {5, 1, 0, 3, 3, 2, 9, 4, 4, 7, 6, 8, 0, 9, 1};
    std::vector<std::uint32_t> got;

    for (const auto seq : order) {
        receiver.onData(seq, number(seq));
        for (auto& message : receiver.takeDelivered()) {
            got.push_back(valueOf(message));
        }
    }
    Assert(inOrderOnce(got, 10), "delivered once each, in order");
    AssertEq(receiver.ack(), 10, "ack is what comes next");
    Assert(receiver.duplicates() >= 5, "and the copies were counted: %llu", static_cast<unsigned long long>(receiver.duplicates()));
}

Test(reliable, ack_bits_say_what_is_beyond)
{
    Channel receiver;

    receiver.onData(0, number(0));
    receiver.onData(2, number(2));    // 1 is missing
    receiver.onData(3, number(3));
    receiver.onData(6, number(6));
    AssertEq(receiver.ack(), 1, "everything below 1 is here");
    // bit i is message ack + 1 + i: 2 -> bit 0, 3 -> bit 1, 6 -> bit 4
    AssertEq(receiver.ackBits(), 0b10011u, "and the bits tell 2, 3 and 6");
}

Test(reliable, acks_free_what_they_say)
{
    Channel sender;
    std::vector<Channel::Outgoing> out;

    for (std::uint32_t i = 0; i < 6; ++i) { sender.queue(number(i)); }
    sender.collect(0.0, out);
    AssertEq(sender.inFlight(), 6, "6 on their way");
    sender.onAck(2, 0b101, 0.1);      // 0 and 1, then 3 and 5
    AssertEq(sender.inFlight(), 2, "2 and 4 remain");
    sender.onAck(6, 0, 0.1);
    Assert(sender.idle(), "all acknowledged");
}

Test(reliable, lost_messages_are_sent_again)
{
    Pair pair(Channel::Config{}, 0.3, 0, 0.02, 0.01, 7);

    for (std::uint32_t i = 0; i < 300; ++i) { pair.sender.queue(number(i)); }
    Assert(everythingArrived(pair, 300, 60.0), "all 300 arrive, over a network that loses 30%%");
    Assert(inOrderOnce(pair.got, 300), "in order, once");
    Assert(pair.sender.resends() > 0, "some had to be sent again");
}

Test(reliable, everything_at_once)
{
    // Loss, repeats and reordering (jitter larger than the gap between packets)
    Pair pair(Channel::Config{}, 0.25, 0.2, 0.03, 0.08, 99);

    for (std::uint32_t i = 0; i < 500; ++i) { pair.sender.queue(number(i)); }
    Assert(everythingArrived(pair, 500, 120.0), "all 500 arrive");
    Assert(inOrderOnce(pair.got, 500), "in order, once each, whatever the network did");
}

Test(reliable, many_seeds)
{
    for (unsigned seed = 1; seed <= 20; ++seed) {
        Pair pair(Channel::Config{}, 0.4, 0.1, 0.02, 0.05, seed);

        for (std::uint32_t i = 0; i < 100; ++i) { pair.sender.queue(number(i)); }
        Assert(everythingArrived(pair, 100, 120.0) && inOrderOnce(pair.got, 100), "seed %u: 40%% loss", seed);
    }
}

Test(reliable, the_window_limits)
{
    Channel::Config config;
    config.window = 8;
    config.maxQueued = 20;
    Channel sender(config);
    std::vector<Channel::Outgoing> out;
    int accepted = 0;

    for (std::uint32_t i = 0; i < 50; ++i) { accepted += sender.queue(number(i)) ? 1 : 0; }
    AssertEq(accepted, 20, "the queue takes 20 and says no to the rest");
    sender.collect(0.0, out);
    AssertEq(out.size(), 8, "only a window of 8 is sent");
    AssertEq(sender.waiting(), 12, "the others wait");
    sender.onAck(4, 0, 0.1);
    out.clear();
    sender.collect(0.1, out);
    AssertEq(out.size(), 4, "4 places freed, 4 more go");
    AssertEq(out[0].seq, 8, "with the next numbers");
}

Test(reliable, the_wait_follows_the_rtt)
{
    Channel sender;
    std::vector<Channel::Outgoing> out;

    for (int round = 0; round < 20; ++round) {
        sender.queue(number(0));
        out.clear();
        sender.collect(round * 1.0, out);
        sender.onAck(sender.inFlight() ? out[0].seq + 1 : 0, 0, round * 1.0 + 0.05);   // acked after 50 ms
    }
    Assert(sender.rtt() > 0.045 && sender.rtt() < 0.055, "the rtt is 50 ms: %f", sender.rtt());
    Assert(sender.rto() >= 0.05 && sender.rto() < 0.15, "and the wait is about that: %f", sender.rto());
}

Test(reliable, waits_get_longer)
{
    Channel sender;
    std::vector<Channel::Outgoing> out;
    std::vector<double> times;

    sender.queue(number(0));
    for (double t = 0.0; t < 6.0; t += 0.01) {
        out.clear();
        sender.collect(t, out);
        if (!out.empty()) {
            times.push_back(t);
        }
    }
    Assert(times.size() >= 4, "it goes on trying");
    Assert(times[2] - times[1] > times[1] - times[0] + 0.05, "each wait is longer than the one before: %f then %f", times[1] - times[0], times[2] - times[1]);
    Assert(times.back() - times[times.size() - 2] <= 1.05, "up to a limit (1 s)");
}

Test(reliable, no_resend_when_told)
{
    Channel::Config config;
    config.resend = false;
    Channel sender(config);
    std::vector<Channel::Outgoing> out;

    sender.queue(number(0));
    sender.collect(0.0, out);
    out.clear();
    for (double t = 0.1; t < 10.0; t += 0.1) {
        sender.collect(t, out);
    }
    Assert(out.empty(), "a transport that never loses needs no resend");
    AssertEq(sender.resends(), 0, "none");
}

Test(reliable, a_message_overtaken_goes_fast)
{
    Channel sender;
    std::vector<Channel::Outgoing> out;

    // The rtt is known (50 ms)
    sender.queue(number(0));
    sender.collect(0.0, out);
    sender.onAck(1, 0, 0.05);
    for (std::uint32_t i = 1; i <= 4; ++i) { sender.queue(number(i)); }
    out.clear();
    sender.collect(1.0, out);
    AssertEq(out.size(), 4, "4 sent");
    sender.onAck(1, 0b111, 1.05);      // 1 is missing, 2, 3 and 4 are there (bit 0 is message 2)
    out.clear();
    sender.collect(1.13, out);         // 80 ms later: well before the wait is over
    Assert(out.size() == 1 && out[0].seq == 1 && out[0].again, "message 1 is sent again at once: the later ones passed it");
}

Test(reliable, too_far_ahead_is_ignored)
{
    Channel receiver;

    receiver.onData(100000, number(1));
    AssertEq(receiver.takeDelivered().size(), 0, "nothing delivered");
    AssertEq(receiver.ackBits(), 0, "and it is not kept");
    receiver.onData(0, number(0));
    AssertEq(receiver.takeDelivered().size(), 1, "the real one goes on");
}
