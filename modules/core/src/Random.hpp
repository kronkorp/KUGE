#pragma once

#include <cstdint>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A random number generator that gives the same numbers on every machine and every compiler, for the same seed
     *
     * `std::mt19937` is the same everywhere, but the distributions of the standard library (`uniform_real_distribution`...) are not:
     * the same seed gives other numbers with other implementations. Anything that two programs must agree on (a client that predicts
     * and a room that decides, a replay) uses this, with a seed they both know:
     *
     *     kuge::Random random(kuge::seedFor(player, tick));          // the same on the client and in the room
     *     const float side = random.range(-1.0f, 1.0f);
     *
     * It is PCG32 (a small, fast generator with good statistics). Not for secrets.
     */
    ////////////////////////////////////////////////////////////////////////////
    class Random
    {
        public:
            explicit Random(std::uint64_t seed = 1) noexcept
            {
                m_state = 0;
                next();
                m_state += seed;
                next();
            }

            //! 32 random bits
            std::uint32_t next(void) noexcept
            {
                const std::uint64_t old = m_state;

                m_state = old * 6364136223846793005ull + 1442695040888963407ull;
                const auto xorShifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
                const auto rotation = static_cast<std::uint32_t>(old >> 59u);

                return (xorShifted >> rotation) | (xorShifted << ((~rotation + 1u) & 31u));
            }

            //! In [0, 1): 24 bits, so every value is exact in a float
            float uniform(void) noexcept { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }

            //! In [low, high)
            float range(float low, float high) noexcept { return low + (high - low) * uniform(); }

            //! In [0, count) (0 if count is 0); without the bias of a modulo
            std::uint32_t below(std::uint32_t count) noexcept
            {
                if (count == 0) {
                    return 0;
                }
                const std::uint32_t limit = (0u - count) % count;   // values under it would make some results more likely
                std::uint32_t value = next();

                while (value < limit) {
                    value = next();
                }
                return value % count;
            }

            //! True with this chance (0 to 1)
            bool chance(float probability) noexcept { return uniform() < probability; }

        private:
            std::uint64_t m_state = 0;
    };

    //! A seed that two sides compute from what they both know (a player, a tick, anything else in `salt`): the same bits in, the same
    //! seed out, and nearby inputs give unrelated seeds
    constexpr std::uint64_t seedFor(std::uint32_t player, std::uint32_t tick, std::uint32_t salt = 0) noexcept
    {
        std::uint64_t x = (static_cast<std::uint64_t>(player) << 32) ^ tick;

        x ^= static_cast<std::uint64_t>(salt) * 0x9E3779B97F4A7C15ull;
        x += 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    }

}
