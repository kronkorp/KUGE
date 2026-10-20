extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Auth.hpp"
#include <array>
#include <vector>

// NOTE: kronklab test names are limited to 31 characters.

Test(auth, siphash_reference_vectors)
{
    // The key 00 01 ... 0f and the input 00 01 ... (length - 1): the vectors of the paper (vectors_sip64)
    const std::uint64_t k0 = 0x0706050403020100ull, k1 = 0x0f0e0d0c0b0a0908ull;
    std::vector<std::uint8_t> input(16);

    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<std::uint8_t>(i);
    }
    const auto hash = [&](std::size_t length) { return kuge::net::sipHash24(k0, k1, std::span<const std::uint8_t>(input.data(), length)); };

    AssertEq(hash(0), 0x726fdb47dd0e0e31ull, "no input");
    AssertEq(hash(1), 0x74f839c593dc67fdull, "one byte");
    AssertEq(hash(8), 0x93f5f5799a932462ull, "one whole block");
    AssertEq(hash(15), 0xa129ca6149be45e5ull, "a block and a tail of 7");
}

Test(auth, a_key_and_an_input_matter)
{
    const std::array<std::uint8_t, 4> a{1, 2, 3, 4}, b{1, 2, 3, 5};

    Assert(kuge::net::sipHash24(1, 2, a) != kuge::net::sipHash24(1, 2, b), "another input, another hash");
    Assert(kuge::net::sipHash24(1, 2, a) != kuge::net::sipHash24(1, 3, a), "another key, another hash");
    Assert(kuge::net::sipHash24(1, 2, a) != kuge::net::sipHash24(2, 2, a), "the other half of the key too");
    AssertEq(kuge::net::sipHash24(1, 2, a), kuge::net::sipHash24(1, 2, a), "and the same ones, the same");
}

// The tags in the examples of the RFC (docs/19, annexe A): the key is the salt and the cookie, and a packet from the
// server is not tagged like the same bytes from a client
Test(auth, the_rfc_example_tags)
{
    const std::uint64_t salt = 0x1122334455667788ull, cookie = 0xA1A2A3A4A5A6A7A8ull, fromServer = 0x5352565253525652ull;
    const auto tag = [&](std::vector<std::uint8_t> body, bool server = false) {
        return static_cast<std::uint32_t>(kuge::net::sipHash24(salt ^ (server ? fromServer : 0), cookie, body));
    };

    AssertEq(tag({0x04, 0x02, 5, 0, 0, 0, 0, 0, 0, 0}), 0xE45C3302u, "the keep-alive: 02 33 5C E4");
    AssertEq(tag({0x04, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x4B, 0x31, 0x5C, 0xF5, 8, 7, 6, 5, 4, 3, 2, 1}), 0x65CC42D7u, "the Hello: D7 42 CC 65");
    AssertEq(tag({0x04, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0x4A, 0x45, 0x41, 0x3C, 0x2C, 1, 0, 0}), 0x8D14635Du, "the SnapshotAck: 5D 63 14 8D");
    Assert(tag({0x05}) != tag({0x05}, true), "and a packet from the server is not tagged like one from a client");
}
