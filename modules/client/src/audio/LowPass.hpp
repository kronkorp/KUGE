#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace kuge
{

    //! The cut-off of a low-pass for a `lowpass` between 0 (muffled) and 1 (open), in Hz: an exponential scale, so
    //! that each step sounds as big as the last
    inline float lowpassCutoff(float lowpass) noexcept
    {
        constexpr float MUFFLED = 300.0f;
        constexpr float OPEN = 20000.0f;

        return MUFFLED * std::pow(OPEN / MUFFLED, std::clamp(lowpass, 0.0f, 1.0f));
    }

    //! The weight of the new sample in a one-pole low-pass: 1 when nothing is cut
    inline float lowpassAlpha(float lowpass, int sampleRate) noexcept
    {
        if (lowpass >= 0.999f) {
            return 1.0f;
        }
        return 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * lowpassCutoff(lowpass) / static_cast<float>(sampleRate));
    }

    //! A one-pole low-pass on stereo samples (left, right, left, right...). What a voice that is far, behind a wall or
    //! behind the listener goes through (see IAudio::setVoice()).
    struct LowPassFilter
    {
        float left  = 0.0f;
        float right = 0.0f;

        //! @param count  How many samples (twice the frames)
        void process(std::int16_t* samples, std::size_t count, float alpha) noexcept
        {
            if (alpha >= 1.0f) {
                left = right = 0.0f;   // (it starts from rest when it closes again)
                return;
            }
            for (std::size_t i = 0; i + 1 < count; i += 2) {
                left += alpha * (static_cast<float>(samples[i]) - left);
                right += alpha * (static_cast<float>(samples[i + 1]) - right);
                samples[i] = static_cast<std::int16_t>(left);
                samples[i + 1] = static_cast<std::int16_t>(right);
            }
        }
    };

}
