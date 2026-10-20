// Ticket 023: the prediction needs the same floats on every platform. A scripted 2D simulation is
// run for 10 000 ticks and its final state is hashed; EXPECTED_HASH is a constant, and the CI of
// Linux AND of Windows must reach it. (A plain program, not a kronklab test: Windows runs it too.)
//
// If you change the physics on purpose, run this program, check that the new hash is the same on
// both platforms' CI, and commit it. Never "fix" a red Windows job by editing the constant alone.
#include "Physics.hpp"
#include "kronkworld/Kronkworld.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
    constexpr std::uint64_t EXPECTED_HASH = 0x2e908ec14bd6351dull;
    constexpr int           TICKS = 10000;
    constexpr float         DT = 1.0f / 60.0f;

    // FNV-1a over the bits of the floats: a difference of one ulp changes it
    struct Hash
    {
        std::uint64_t value = 14695981039346656037ull;

        void byte(std::uint8_t b) { value = (value ^ b) * 1099511628211ull; }
        void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) byte(static_cast<std::uint8_t>(v >> (8 * i))); }
        void f32(float f) { std::uint32_t bits; std::memcpy(&bits, &f, sizeof bits); u32(bits); }
    };

    // An integer generator: the same on every platform (<random>'s distributions are not)
    struct Lcg
    {
        std::uint32_t state = 12345u;

        std::uint32_t next(void) { state = state * 1664525u + 1013904223u; return state >> 8; }
    };

    // a * b + c with one rounding if the compiler fused it, with two otherwise. The values make the
    // two differ: (1 + 2^-13)^2 = 1 + 2^-12 + 2^-26, and a float drops the 2^-26.
    bool floatsAreNotFused(void)
    {
        volatile float a = 1.0f + 1.0f / 8192.0f;
        volatile float c = -(1.0f + 1.0f / 4096.0f);
        const float r = a * a + c;

        return r == 0.0f;
    }

    std::uint64_t simulate(std::uint64_t& events)
    {
        using namespace kuge;
        kw::World world;
        Lcg random;
        std::vector<kw::Entity> things;

        world.addResource<Physics2D>(PhysicsConfig{});
        auto& physics = world.getResource<Physics2D>();

        physics.tiles.reset(40, 20, 16.0f);
        for (int x = 0; x < 40; ++x) {
            physics.tiles.set(x, 19, true);
        }
        for (int y = 0; y < 20; ++y) {
            physics.tiles.set(0, y, true);
            physics.tiles.set(39, y, true);
        }
        physics.tiles.set(10, 16, true);
        physics.tiles.set(11, 16, true);
        physics.tiles.set(25, 14, true);

        auto spawn = [&](Vec2 position, Collider collider, Body body, bool moves) {
            const kw::Entity entity = world.create();

            world.add<Transform2D>(entity, Transform2D{position});
            world.add<Collider>(entity, collider);
            if (moves) {
                world.add<Body>(entity, body);
            }
            things.push_back(entity);
            return entity;
        };

        spawn({320.0f, 200.0f}, Collider::box(97.3f, 11.1f), Body{}, false);
        spawn({150.7f, 120.3f}, Collider::circle(13.7f), Body{}, false);
        spawn({480.0f, 60.0f}, Collider::box(40.0f, 40.0f).asTrigger(), Body{}, false);
        for (int i = 0; i < 12; ++i) {
            Body body;

            body.type = Body::Type::Dynamic;
            body.velocity = {static_cast<float>(i) * 13.37f - 70.1f, -static_cast<float>(i) * 7.77f};
            body.gravityScale = 0.5f + static_cast<float>(i) * 0.0937f;
            spawn({40.0f + static_cast<float>(i) * 21.7f, 20.0f + static_cast<float>(i) * 3.3f},
                  i % 2 ? Collider::circle(4.1f + static_cast<float>(i) * 0.31f) : Collider::box(7.3f, 9.1f + static_cast<float>(i)),
                  body, true);
        }
        for (int i = 0; i < 4; ++i) {
            Body body;

            body.type = Body::Type::Kinematic;
            body.velocity = {37.7f + static_cast<float>(i) * 11.1f, 23.3f - static_cast<float>(i) * 9.9f};
            spawn({100.0f + static_cast<float>(i) * 83.3f, 90.0f}, Collider::box(12.9f, 5.7f), body, true);
        }

        for (int tick = 0; tick < TICKS; ++tick) {
            // The "player": every few ticks a push in a direction that does not divide well
            if (tick % 7 == 0) {
                const kw::Entity entity = things[3 + random.next() % 16];

                if (world.has<Body>(entity)) {
                    auto& body = world.get<Body>(entity);

                    // (named: the order of evaluation of two calls in one expression is not specified)
                    const float push = static_cast<float>(random.next() % 200) * 0.731f - 73.1f;
                    const float lift = static_cast<float>(random.next() % 50) * 3.17f;

                    body.velocity += Vec2{push, -lift};
                }
            }
            physics.step(world, DT);
            for (const auto& event : physics.events()) {
                events += event.entered ? 1u : 0u;
            }
        }

        Hash hash;

        for (const kw::Entity entity : things) {
            const auto& position = world.get<Transform2D>(entity).position;

            hash.f32(position.x);
            hash.f32(position.y);
            if (world.has<Body>(entity)) {
                const auto& body = world.get<Body>(entity);

                hash.f32(body.velocity.x);
                hash.f32(body.velocity.y);
                hash.byte(static_cast<std::uint8_t>(body.contacts.left | body.contacts.right << 1 | body.contacts.up << 2 | body.contacts.down << 3));
            }
        }
        hash.u32(static_cast<std::uint32_t>(events));
        return hash.value;
    }
}

int main()
{
    int failures = 0;
    std::uint64_t events = 0;
    std::uint64_t again = 0;
    const std::uint64_t hash = simulate(events);

    std::printf("float determinism: hash 0x%016llx, %llu trigger entries\n",
        static_cast<unsigned long long>(hash), static_cast<unsigned long long>(events));
    if (!floatsAreNotFused()) {
        std::printf("FAIL: a * b + c was fused (FMA contraction): build with kuge-determinism\n");
        ++failures;
    }
    if (simulate(again) != hash) {
        std::printf("FAIL: the same scenario run twice gives two hashes\n");
        ++failures;
    }
    if (hash != EXPECTED_HASH) {
        std::printf("FAIL: expected 0x%016llx. Another platform, or another set of compile options, or the physics changed.\n",
            static_cast<unsigned long long>(EXPECTED_HASH));
        ++failures;
    }
    return failures == 0 ? 0 : 1;
}
