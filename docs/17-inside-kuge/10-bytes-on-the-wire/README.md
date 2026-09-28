# Step 10: Bytes on the wire

From here on, the tutorial is about the network. This step is about bytes. It shows how a value becomes bytes
(the serializer), how a struct becomes a message (`KUGE_MESSAGE`), what an endpoint puts around a message (the
packets), and what carries the packets (the transports).

## The serializer

```cpp
// modules/core/src/Serializer.hpp, ByteWriter::write
template<Scalar T>
void write(T value)
{
    if constexpr (std::is_enum_v<T>) {
        write(static_cast<std::underlying_type_t<T>>(value));
    } else if constexpr (std::is_same_v<T, bool>) {
        put(value ? 1u : 0u, 1);
    } else if constexpr (std::is_floating_point_v<T>) {
        put(std::bit_cast<std::uint32_t>(value), 4);      // (or 8 for a double): the IEEE 754 bits
    } else {
        put(static_cast<std::uint64_t>(static_cast<std::make_unsigned_t<T>>(value)), sizeof(T));
    }
}

// modules/core/src/Serializer.cpp
void ByteWriter::put(std::uint64_t value, std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i) {
        m_bytes.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xff));   // little-endian, always
    }
}
```

Every number has its exact size, and is written byte by byte, low byte first, whatever the machine is. A string
or a vector is its count (4 bytes), then its elements.

`ByteReader` is the mirror image, and never trusts its input. Reading more than is left throws `SerializerError`.
A count is checked against what is left **before** anything is allocated (`readCount`), so a packet claiming a
four-billion-element vector costs nothing. A bool that is neither 0 nor 1 is an error too. A `ByteReader` is a view
on bytes it does not own; building one on a temporary vector is a compile error (a deleted constructor, checked
by a boundary test).

The same writer and reader serve the saves, the snapshots and the network.

## `KUGE_MESSAGE`: a struct that knows its fields

```cpp
// modules/net/src/Wire.hpp
#define KUGE_MESSAGE(Type, ...) \
    static constexpr std::uint32_t kugeMessageId = ::kuge::net::hashName(#Type); \
    static constexpr const char* kugeMessageName = #Type; \
    template<typename Visitor> void kugeVisit(Visitor&& visitor) { visitor(field1); visitor(field2); ... } \
    template<typename Visitor> void kugeVisit(Visitor&& visitor) const { ... the same ... }
```

The macro adds three things to the struct:

- **An id**, `hashName("PlayerMoved")`. That is FNV-1a on the name, computed at compile time, so it is the same on
  every machine and in every build.
- **Its name**, so that two types whose names hash to the same id are caught when their handlers are registered
  (`Endpoint::registerHandler` throws) rather than mixed up.
- **`kugeVisit`**, which calls a visitor on each listed field, in order. The field list comes from a classic
  "for each argument" preprocessor trick, up to 24 fields.

`encode(out, value)` and `decode(in, value)` (same file) pick the right writing for each type with `if constexpr`:
numbers, strings, vectors, arrays, optionals, `Vec2`, `Rect`, and a message inside a message, through
`kugeVisit`. A message on the wire is its **id (4 bytes), then its fields**, and nothing else: no field names, no
sizes, no version. Both sides must agree on the struct.

## What an endpoint puts around a message

The first byte of every packet says what it is:

| Type | Byte 0 | Then | Size |
|---|---|---|---|
| `Connect` | 1 | magic `"KUGE"` (u32), protocol (u16), salt (u32) | 11 |
| `Accept` | 2 | the same salt (u32) | 5 |
| `Reject` | 3 | 0 (u32) | 5 |
| `Data` | 4 | channel (u8: 0 unreliable, 1 reliable, 2 control), ack (u32), ack bits (u32), **reliable only**: seq (u32), then the message | 10, or 14 + message |
| `Goodbye` | 5 | 0 (u32) | 5 |

Every `Data` packet carries `ack` and `ack bits`: what this side has received of the other's reliable messages
(step 11). So acknowledgements ride on the traffic, and a `control` packet (a `Data` with no message) is only sent
when there is nothing else to carry them, or to keep a silent connection alive.

## The transports

An endpoint does not know what carries its packets. It talks to an `ITransport`:

```cpp
// modules/net/src/Transport.hpp
virtual void poll(std::vector<TransportEvent>& out) = 0;   // Connected, Disconnected, Packet (a whole one)
virtual bool send(ConnectionId connection, std::span<const std::uint8_t> packet) = 0;
virtual void close(ConnectionId connection) = 0;
virtual bool reliable(void) const noexcept = 0;            // true: nothing is ever lost or repeated (TCP)
```

- **Loopback** (`Loopback.cpp`): in memory, by name. A connection is a `Link` with two queues, under one mutex for
  the whole network, so the two sides can be on two threads. Each packet gets an arrival time
  (`clock() + latency + jitter × random`), and `take()` gives what has arrived, sorted by arrival. The
  `Conditions` lose, repeat, delay and reorder packets with a seeded generator, and the clock can be replaced.
- **UDP** (`SocketTransport.cpp`, through kronknet): one packet is one datagram. On a server, kronknet makes a
  "connection" per address that sends it something. A poll reads until the socket is empty (at most 512 datagrams).
  A datagram is sent whole or dropped when the socket is full; it is never kept to be glued to the next ones.
- **TCP**: a stream has no packets, so `StreamFramer` adds them: each packet goes as a 4-byte length, then the
  packet. The reader puts packets back together whatever way TCP cut the bytes. A length of 0 or over 8192 means
  the stream cannot be trusted any more, and the peer is dropped.
- kronknet keeps a process-wide counter that is not thread-safe, so every call into it goes through one mutex
  (`g_kronknet`).

## Lab

`example/lab/lab10_wire.cpp`:

```cpp
// Lab 10: the bytes. A message as it is encoded, then every packet two endpoints exchange.
#include "Endpoint.hpp"
#include "Logger.hpp"
#include "Loopback.hpp"
#include <cstdio>
#include <memory>
#include <string>

namespace
{
    struct PlayerMoved
    {
        KUGE_MESSAGE(PlayerMoved, id, position, heading)
        std::uint32_t id = 0;
        kuge::Vec2    position;
        float         heading = 0.0f;
    };

    struct Chat
    {
        KUGE_MESSAGE(Chat, text)
        std::string text;
    };

    void hex(std::span<const std::uint8_t> bytes, std::size_t from, std::size_t count)
    {
        for (std::size_t i = from; i < from + count && i < bytes.size(); ++i) {
            std::printf(" %02x", bytes[i]);
        }
    }

    std::uint32_t u32(std::span<const std::uint8_t> p, std::size_t at)
    {
        return p[at] | p[at + 1] << 8 | p[at + 2] << 16 | static_cast<std::uint32_t>(p[at + 3]) << 24;
    }

    // A transport that forwards everything, and prints each packet it sends
    class Spy final : public kuge::net::ITransport
    {
        public:
            Spy(std::unique_ptr<ITransport> inner, const char* who, const double& now) : m_inner(std::move(inner)), m_who(who), m_now(now) {}

            void poll(std::vector<kuge::net::TransportEvent>& out) override { m_inner->poll(out); }
            void close(kuge::net::ConnectionId connection) override { m_inner->close(connection); }
            bool reliable(void) const noexcept override { return m_inner->reliable(); }
            std::string describe(void) const override { return m_inner->describe(); }

            bool send(kuge::net::ConnectionId connection, std::span<const std::uint8_t> p) override
            {
                std::printf("%5.2f s  %s ->", m_now, m_who);
                switch (p[0]) {
                    case 1: std::printf(" Connect  magic %08x, protocol %u, salt (random)", u32(p, 1), p[5] | p[6] << 8); break;
                    case 2: std::printf(" Accept   salt (the same)"); break;
                    case 3: std::printf(" Reject"); break;
                    case 5: std::printf(" Goodbye"); break;
                    case 4: {
                        static const char* channels[] = {"unreliable", "reliable", "control"};
                        std::printf(" Data     %-10s ack %u bits %u", channels[p[1]], u32(p, 2), u32(p, 6));
                        std::size_t body = 10;
                        if (p[1] == 1) {
                            std::printf(" seq %u", u32(p, 10));
                            body = 14;
                        }
                        if (p.size() > body) {
                            std::printf(" | message id %08x +%zu bytes:", u32(p, body), p.size() - body - 4);
                            hex(p, body + 4, p.size() - body - 4);
                        }
                        break;
                    }
                }
                std::printf("   (%zu bytes)\n", p.size());
                return m_inner->send(connection, p);
            }

        private:
            std::unique_ptr<ITransport> m_inner;
            const char*                 m_who;
            const double&               m_now;
    };
}

int main()
{
    using namespace kuge::net;
    Logger::logger().enable(false);

    // -- 1. A message, as bytes ---------------------------------------------------------------
    kuge::ByteWriter out;

    out.write<std::uint32_t>(PlayerMoved::kugeMessageId);
    encode(out, PlayerMoved{7, {1.5f, -2.0f}, 90.0f});
    std::printf("PlayerMoved{7, {1.5, -2}, 90}, id = hashName(\"PlayerMoved\") = %08x\n", PlayerMoved::kugeMessageId);
    std::printf("  id      "); hex(out.bytes(), 0, 4);  std::printf("\n");
    std::printf("  id      "); hex(out.bytes(), 4, 4);  std::printf("   (uint32 7, little-endian)\n");
    std::printf("  position"); hex(out.bytes(), 8, 8);  std::printf("   (two floats, IEEE 754)\n");
    std::printf("  heading "); hex(out.bytes(), 16, 4); std::printf("\n");

    // -- 2. Two endpoints, and everything they send -----------------------------------------------
    std::printf("\nChat id = %08x\n", Chat::kugeMessageId);
    double now = 0.0;
    LoopbackNetwork network({}, [&now] { return now; });
    EndpointConfig config;

    config.clock = [&now] { return now; };
    auto server = std::make_unique<Endpoint>(std::make_unique<Spy>(network.listen("room"), "server", now), Role::Server, config);
    auto client = std::make_unique<Endpoint>(std::make_unique<Spy>(network.connect("room"), "client", now), Role::Client, config);
    const auto run = [&](double until) {
        while (now < until - 1e-9) {
            now += 0.01;
            server->poll();
            client->poll();
        }
    };

    run(0.03);
    std::printf("-- connected: the client sends a reliable Chat and an unreliable one\n");
    client->send(CLIENT_CONNECTION, Chat{"hi"});
    client->send(CLIENT_CONNECTION, Chat{"yo"}, Channel::Unreliable);
    run(0.05);
    std::printf("-- nothing to say for a while\n");
    run(1.1);
    std::printf("-- the client goes away\n");
    client.reset();
}
```

What it prints (`./build/example/lab10_wire`):

```text
PlayerMoved{7, {1.5, -2}, 90}, id = hashName("PlayerMoved") = 859dd5cb
  id       cb d5 9d 85
  id       07 00 00 00   (uint32 7, little-endian)
  position 00 00 c0 3f 00 00 00 c0   (two floats, IEEE 754)
  heading  00 00 b4 42

Chat id = 2279d8cb
 0.01 s  client -> Connect  magic 4547554b, protocol 1, salt (random)   (11 bytes)
 0.02 s  server -> Accept   salt (the same)   (5 bytes)
-- connected: the client sends a reliable Chat and an unreliable one
 0.03 s  client -> Data     reliable   ack 0 bits 0 seq 0 | message id 2279d8cb +6 bytes: 02 00 00 00 68 69   (24 bytes)
 0.03 s  client -> Data     unreliable ack 0 bits 0 | message id 2279d8cb +6 bytes: 02 00 00 00 79 6f   (20 bytes)
 0.04 s  server -> Data     control    ack 1 bits 0   (10 bytes)
-- nothing to say for a while
 1.03 s  client -> Data     control    ack 0 bits 0   (10 bytes)
 1.04 s  server -> Data     control    ack 1 bits 0   (10 bytes)
-- the client goes away
 1.10 s  client -> Goodbye   (5 bytes)
 1.10 s  server -> Goodbye   (5 bytes)
```

## Reading the output

- **The message**: 4 bytes of id (`cb d5 9d 85` is `859dd5cb`, low byte first), the id field (7), two floats
  (`00 00 c0 3f` is 1.5 in IEEE 754, `00 00 00 c0` is -2), then the heading (`00 00 b4 42` is 90). 20 bytes, no
  padding, no names.
- **The handshake**: `Connect` is 11 bytes. `4547554b` is "KUGE" read as a little-endian number. The server answers
  `Accept` with the same salt. The salt tells this client's attempt apart from an older one that came from the same
  address.
- **The reliable Chat** is 24 bytes: 14 of header (type, channel, ack, bits, seq 0), the id `2279d8cb`, then the
  string "hi" as its length (`02 00 00 00`) and its 2 bytes. The **unreliable** one has no seq: 20 bytes.
- **`ack 0`** on the client's packets: it has received none of the server's reliable messages. The server answers
  with a control packet, **`ack 1`**: "I have every message below 1". That is all it takes to acknowledge seq 0.
- **Keep-alive**: after a second of silence each side sends a control packet (the client at 1.03, one second after
  its last packet at 0.03; the server at 1.04).
- **Goodbye**: the client's destructor tells the server. Then `main` returns and the server's destructor sends its
  own `Goodbye`: it has not polled since, so for it the client is still there. Neither side waits for a timeout.

Next: [Step 11: Reliability](../11-reliability/README.md).
