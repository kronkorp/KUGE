#pragma once

#include <bit>
#include <cstdint>
#include <span>

namespace kuge::net
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  SipHash-2-4 (Aumasson and Bernstein, 2012): a keyed hash of 64 bits, made to authenticate short messages
     *         fast. The endpoint uses it for the cookie of the handshake and for the tag of each packet.
     *
     * It is a MAC, not an encryption: it tells that whoever made the packet knew the key. With a key that an
     * attacker has not seen, a packet cannot be forged by guessing (2^-32 for a 32-bit tag), but the packets are
     * readable, and whoever reads the handshake knows the key. It keeps off-path attackers out, not eavesdroppers.
     * (Checked against the reference vectors of the paper in tests/net/auth_test.cpp.)
     */
    ////////////////////////////////////////////////////////////////////////////
    constexpr std::uint64_t sipHash24(std::uint64_t k0, std::uint64_t k1, std::span<const std::uint8_t> data) noexcept
    {
        std::uint64_t v0 = k0 ^ 0x736f6d6570736575ull;
        std::uint64_t v1 = k1 ^ 0x646f72616e646f6dull;
        std::uint64_t v2 = k0 ^ 0x6c7967656e657261ull;
        std::uint64_t v3 = k1 ^ 0x7465646279746573ull;

        const auto round = [&] {
            v0 += v1; v1 = std::rotl(v1, 13); v1 ^= v0; v0 = std::rotl(v0, 32);
            v2 += v3; v3 = std::rotl(v3, 16); v3 ^= v2;
            v0 += v3; v3 = std::rotl(v3, 21); v3 ^= v0;
            v2 += v1; v1 = std::rotl(v1, 17); v1 ^= v2; v2 = std::rotl(v2, 32);
        };
        const auto word = [&](std::size_t from, std::size_t count) {
            std::uint64_t value = 0;

            for (std::size_t i = 0; i < count; ++i) {
                value |= static_cast<std::uint64_t>(data[from + i]) << (8 * i);   // little-endian, whatever the machine
            }
            return value;
        };
        std::size_t i = 0;

        for (; i + 8 <= data.size(); i += 8) {
            const std::uint64_t m = word(i, 8);

            v3 ^= m;
            round(); round();
            v0 ^= m;
        }
        const std::uint64_t last = (static_cast<std::uint64_t>(data.size()) << 56) | word(i, data.size() - i);

        v3 ^= last;
        round(); round();
        v0 ^= last;
        v2 ^= 0xff;
        round(); round(); round(); round();
        return v0 ^ v1 ^ v2 ^ v3;
    }

}
