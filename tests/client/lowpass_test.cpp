extern "C" {
    #include "kronklab/kronklab.h"
}
#include "audio/LowPass.hpp"
#include <cmath>
#include <vector>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    constexpr int RATE = 44100;

    // How loud a sine of this frequency comes out, as a fraction of how loud it went in
    float passed(float frequency, float lowpass)
    {
        std::vector<std::int16_t> samples(2 * 4410);
        kuge::LowPassFilter filter;

        for (std::size_t i = 0; i < samples.size() / 2; ++i) {
            samples[2 * i] = samples[2 * i + 1] = static_cast<std::int16_t>(10000.0 * std::sin(6.283185307 * frequency * static_cast<double>(i) / RATE));
        }
        filter.process(samples.data(), samples.size(), kuge::lowpassAlpha(lowpass, RATE));
        float peak = 0.0f;

        for (std::size_t i = samples.size() / 2; i < samples.size(); ++i) {   // (after the filter settled)
            peak = std::max(peak, std::fabs(static_cast<float>(samples[i])));
        }
        return peak / 10000.0f;
    }
}

Test(lowpass, open_changes_nothing)
{
    Assert(passed(8000.0f, 1.0f) > 0.99f, "a high note goes through untouched");
    Assert(kuge::lowpassAlpha(1.0f, RATE) == 1.0f, "no filtering at all");
}

Test(lowpass, muffled_cuts_the_highs)
{
    Assert(passed(200.0f, 0.2f) > 0.5f, "a low note stays: %f", passed(200.0f, 0.2f));
    Assert(passed(8000.0f, 0.2f) < 0.1f, "a high one goes: %f", passed(8000.0f, 0.2f));
    Assert(passed(3000.0f, 0.2f) < passed(3000.0f, 0.6f), "less muffled, more highs");
    Assert(kuge::lowpassCutoff(0.0f) < kuge::lowpassCutoff(0.5f) && kuge::lowpassCutoff(0.5f) < kuge::lowpassCutoff(1.0f), "the cut-off grows");
}
