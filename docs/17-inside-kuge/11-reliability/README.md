# Step 11: Reliability

UDP loses, repeats and reorders datagrams. An `Endpoint` builds three things on top of that: connections (a
handshake, keep-alives, timeouts), a **reliable channel** (each message arrives once and in order, however many
times it has to be sent), and handlers that are safe to run. This step reads `Endpoint::poll` and `ReliableChannel`.

## A connection's states

```
client                                                   server
Connecting ── Connect (every 0.25 s, for 5 s) ─────────▶ (transport "Connected": Handshaking, not reported)
                                                          right magic and protocol: Connected, onConnected
Connected ◀────────────────────────────────── Accept ──   (sent again if the same Connect comes again)
  (or on the first Data: the Accept was lost, but the server obviously sees us as connected)
```

```cpp
// modules/net/src/Endpoint.cpp, Endpoint::handlePacket, case Connect (server side)
if (version != m_config.protocol) {
    sendSimple(connection, Reject, 0, time);             // another version of the protocol: refused
    close(id, DisconnectReason::Refused, false, true);
    return;
}
if (connection.state == State::Handshaking) {
    connection.salt = salt;
    establish(connection);                               // Connected, and onConnected is queued
}
if (connection.state == State::Connected && connection.salt == salt) {
    sendSimple(connection, Accept, salt, time);          // (again, if the first one was lost)
}
```

A server keeps the connections of peers that never finished the handshake out of sight: they are not reported,
and are dropped after `connectTimeout`. Only a peer that answered becomes a connection the game sees.

## One `poll()`

```cpp
// modules/net/src/Endpoint.cpp, Endpoint::poll
m_inPoll = true;
m_transport->poll(events);                    // 1. what arrived
for (auto& event : events) {
    handleEvent(event, time);                 //    handshake, acks, reliable data put in order...
}
for (const ConnectionId id : ids) {
    service(connection, time);                // 2. timeouts, (re)sends, acks and keep-alives
}
m_inPoll = false;
runDeferred();                                // 3. and only now, the game's handlers
```

```cpp
// Endpoint::service, for a connected peer
if (time - connection.lastReceived >= m_config.timeout) {
    close(connection.id, DisconnectReason::Timeout, true, true);          // 10 s of silence: gone
    return;
}
flushReliable(connection, time);                                          // new reliable messages, and resends
if (connection.reliable.ackPending() || time - connection.lastSent >= m_config.keepAlive) {
    sendData(connection, Channel::Unreliable, true, 0, nullptr, time);    // a control packet: ack, keep-alive
}
```

## The reliable channel

`ReliableChannel` (`Reliable.cpp`) does only arithmetic: it sends nothing and reads no clock (the time is given),
which is why it has tests of its own. One per connection and direction.

**Sending.**

```cpp
// modules/net/src/Reliable.cpp, ReliableChannel::collect
while (!m_queued.empty() && m_inflight.size() < m_config.window) {   // at most 256 on their way
    ... give the next message a number (seq), put it in flight, send it
}
for (Sent& sent : m_inflight) {
    // Each miss doubles the wait (up to the limit)
    const double wait = std::min(rto() * std::pow(2.0, std::min<std::uint32_t>(sent.sends - 1, 8)), m_config.maxRto);
    // A message that later ones have overtaken is very likely lost: no need to wait for the whole wait
    const bool overtaken = m_anyAcked && sent.seq < m_highestAcked && sent.sends == 1
        && now - sent.lastSent >= std::max(m_config.minRto, m_hasRtt ? m_srtt * 1.5 : m_config.minRto);

    if (now - sent.lastSent >= wait || overtaken) {
        ... send it again
    }
}
```

**The round trip time** comes from the acks, with TCP's classic formulas:

```cpp
// ReliableChannel::acknowledge
if (found->sends == 1) {                 // only a message sent once: an ack could be for any of the copies
    const double sample = now - found->firstSent;
    m_rttvar = 0.75 * m_rttvar + 0.25 * std::fabs(m_srtt - sample);
    m_srtt = 0.875 * m_srtt + 0.125 * sample;
}
// rto() = clamp(srtt + 4 × rttvar, 0.03 s, 1 s); 0.2 s before the first sample
```

**Receiving.**

```cpp
// ReliableChannel::onData
m_ackPending = true;                               // whatever arrives is worth an ack, even a copy
if (seq < m_next) {
    ++m_duplicates;                                // already delivered: a copy
    return;
}
if (seq != m_next) {
    m_stash.emplace(seq, std::move(payload));      // early: kept until the ones before it arrive
    return;
}
m_delivered.push_back(std::move(payload));         // the one expected: delivered...
++m_next;
... and every stashed one that now follows, in order
```

What goes back to the sender is `ack = m_next`, meaning "I have every message below this one", and `ack bits`: bit
*i* is set if message `m_next + 1 + i` is in the stash. With the bits, the sender learns what arrived out of order,
and stops resending it. Over TCP, `resend` is off: the stream never loses.

## Handlers run last, and may destroy the endpoint

A handler may send, disconnect, open another endpoint, or destroy this one: `MatchmakingClient` removes a room's
endpoint from its `Net` when the room is lost. If handlers ran in the middle of `handlePacket`, they could erase the
connection being read. So every handler is **queued** (`m_deferred`) and runs at the end of `poll`, when the endpoint
is in order:

```cpp
// modules/net/src/Endpoint.cpp, Endpoint::runDeferred
const std::shared_ptr<bool> alive = m_alive;       // the destructor sets it to false

while (!m_deferred.empty()) {
    std::vector<std::function<void(void)>> run;

    run.swap(m_deferred);
    for (auto& job : run) {
        job();
        if (!*alive) {
            return;                                // a handler destroyed this endpoint: touch nothing of it
        }
    }
}
```

The handler that runs is also a **copy** of the one registered. A handler that destroys its endpoint, or replaces
itself with `on<T>()`, is therefore not freed while it is still running. (Both protections were added after a use
after free was found in exactly that case. `tests/net/endpoint_test.cpp` has the three scenarios.)

## Lab

`example/lab/lab11_reliable.cpp`:

```cpp
// Lab 11: twenty reliable messages over a network that loses, repeats, delays and reorders packets.
// The network and the endpoints share a made-up clock: ten seconds pass in no time, and every run is the same.
#include "Endpoint.hpp"
#include "Logger.hpp"
#include "Loopback.hpp"
#include <cstdio>
#include <vector>

namespace
{
    struct Number
    {
        KUGE_MESSAGE(Number, value)
        std::uint32_t value = 0;
    };
}

int main()
{
    using namespace kuge::net;
    Logger::logger().enable(false);
    double now = 0.0;
    LoopbackNetwork network(LoopbackNetwork::Conditions{.loss = 0.3, .duplicate = 0.1, .latency = 0.03, .jitter = 0.03, .seed = 7},
        [&now] { return now; });
    EndpointConfig config;

    config.clock = [&now] { return now; };
    Endpoint server(network.listen("room"), Role::Server, config);
    Endpoint client(network.connect("room"), Role::Client, config);
    std::vector<std::uint32_t> received;

    server.on<Number>([&](ConnectionId, const Number& number) { received.push_back(number.value); });
    while (!client.connected() && now < 5.0) {
        now += 0.01;
        server.poll();
        client.poll();
    }
    std::printf("connected after %.2f s (one round trip, at 30 to 60 ms each way)\n", now);
    const double start = now;
    std::uint32_t sent = 0;

    while (received.size() < 20 && now < start + 10.0) {
        now += 0.01;
        if (sent < 20) {
            client.send(CLIENT_CONNECTION, Number{++sent});    // one per 10 ms
        }
        server.poll();
        client.poll();
    }
    std::printf("all 20 received after %.2f s, in this order:", now - start);
    for (const std::uint32_t value : received) {
        std::printf(" %u", value);
    }
    std::printf("\n");
    std::printf("network: %llu packets sent, %llu lost\n",
        static_cast<unsigned long long>(network.packetsSent()), static_cast<unsigned long long>(network.packetsLost()));
    std::printf("client: %llu messages sent again; server: %llu copies thrown away\n",
        static_cast<unsigned long long>(client.stats().resends), static_cast<unsigned long long>(server.stats().duplicates));
    std::printf("the client measured a round trip of %.0f ms\n", client.rtt(CLIENT_CONNECTION).value_or(0.0) * 1000.0);
}
```

What it prints (`./build/example/lab11_reliable`):

```text
connected after 0.09 s (one round trip, at 30 to 60 ms each way)
all 20 received after 1.60 s, in this order: 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20
network: 55 packets sent, 22 lost
client: 15 messages sent again; server: 3 copies thrown away
the client measured a round trip of 102 ms
```

## Reading the output

- The network loses 30 % of the packets, repeats 10 %, and delays each by 30 to 60 ms, so packets pass each other.
  **All twenty messages arrived, once each, and in order.** Nothing in the game had to know.
- **22 of 55 packets were lost** (more than 30 %: that is chance, and the seed makes it the same chance every run).
  The client sent 15 messages again. The server threw away 3 copies: resends of messages whose ack was lost, and
  packets the network repeated.
- The measured **round trip of 102 ms** is about twice the one-way delay (30 ms, plus 15 of jitter on average),
  plus the 10 ms between two polls.
- **1.6 s** for 20 messages sent over 0.2 s: the last losses waited for their resend timer, which doubles at each
  miss. That is the cost of reliability over a bad network. It is also why positions and inputs are sent
  **unreliable** (a newer one replaces a lost one), and only what must arrive goes reliable.

Next: [Step 12: Lobby and rooms](../12-lobby-and-rooms/README.md).
