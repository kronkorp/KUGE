extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Random.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// NOTE: kronklab test names are limited to 31 characters.

Test(random, same_seed_same_numbers)
{
    kuge::Random a(42), b(42), c(43);
    std::vector<std::uint32_t> first, second, other;

    for (int i = 0; i < 100; ++i) {
        first.push_back(a.next());
        second.push_back(b.next());
        other.push_back(c.next());
    }
    Assert(first == second, "the same seed, the same numbers");
    Assert(first != other, "another seed, other ones");
}

Test(random, numbers_that_never_change)
{
    // Pinned: a change here breaks every replay and every client that predicts a spread against a room of another build
    kuge::Random random(42);

    AssertEq(random.next(), 0xC2F57BD6u, "first");
    AssertEq(random.next(), 0x6B07C4A9u, "second");
    AssertEq(random.next(), 0x72B7B29Bu, "third");
    AssertEq(random.next(), 0x44215383u, "fourth");
    kuge::Random again(42);

    Assert(std::fabs(again.uniform() - 0.76155823f) < 1e-7f, "uniform: a float built from the 24 high bits");
    AssertEq(kuge::seedFor(3, 1000), 15895449093087905130ull, "and the seed for (player 3, tick 1000)");
}

Test(random, in_range_and_fair)
{
    kuge::Random random(7);
    std::array<int, 10> buckets{};
    float low = 1.0f, high = 0.0f;

    for (int i = 0; i < 100000; ++i) {
        const float u = random.uniform();

        low = std::min(low, u);
        high = std::max(high, u);
        ++buckets[random.below(10)];
    }
    Assert(low >= 0.0f && high < 1.0f, "uniform is in [0, 1): %f to %f", low, high);
    for (int count : buckets) {
        Assert(count > 9500 && count < 10500, "each of 10 buckets gets about a tenth: %d", count);
    }
    for (int i = 0; i < 1000; ++i) {
        const float r = random.range(-2.0f, 3.0f);

        Assert(r >= -2.0f && r < 3.0f, "range: %f", r);
    }
    AssertEq(random.below(0), 0u, "below(0) is 0, not a crash");
    int heads = 0;

    for (int i = 0; i < 10000; ++i) {
        heads += random.chance(0.25f) ? 1 : 0;
    }
    Assert(heads > 2300 && heads < 2700, "a chance of a quarter: %d", heads);
}

Test(random, a_spread_both_sides_agree_on)
{
    // The client predicts the recoil of a shot, the room decides it: same player, same tick, same pattern
    const auto pattern = [](std::uint32_t player, std::uint32_t tick) {
        kuge::Random random(kuge::seedFor(player, tick));

        return std::array<float, 3>{random.range(-1.0f, 1.0f), random.range(-1.0f, 1.0f), random.range(0.0f, 0.5f)};
    };

    Assert(pattern(3, 1000) == pattern(3, 1000), "the client and the room get the same");
    Assert(pattern(3, 1000) != pattern(3, 1001) && pattern(3, 1000) != pattern(4, 1000), "another tick or player, another pattern");
    Assert(kuge::seedFor(1, 2) != kuge::seedFor(2, 1), "the order of the arguments matters");
    Assert(kuge::seedFor(1, 2, 0) != kuge::seedFor(1, 2, 1), "so does the salt (a second thing in the same tick)");
}
