#pragma once

#include "Serializer.hpp"
#include <cmath>
#include <filesystem>
#include <unistd.h>

// A WAV file made by hand: a sine wave, 16 bits, mono. Nothing to ship with the tests.

inline std::vector<std::uint8_t> makeWav(double seconds, int sampleRate = 22050, double frequency = 440.0)
{
    const std::uint32_t samples = static_cast<std::uint32_t>(seconds * sampleRate);
    const std::uint32_t dataSize = samples * 2;
    kuge::ByteWriter out;

    for (char c : {'R', 'I', 'F', 'F'}) { out.write<char>(c); }
    out.write<std::uint32_t>(36 + dataSize);
    for (char c : {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '}) { out.write<char>(c); }
    out.write<std::uint32_t>(16);                       // size of the format
    out.write<std::uint16_t>(1);                        // PCM
    out.write<std::uint16_t>(1);                        // mono
    out.write<std::uint32_t>(static_cast<std::uint32_t>(sampleRate));
    out.write<std::uint32_t>(static_cast<std::uint32_t>(sampleRate) * 2);
    out.write<std::uint16_t>(2);
    out.write<std::uint16_t>(16);
    for (char c : {'d', 'a', 't', 'a'}) { out.write<char>(c); }
    out.write<std::uint32_t>(dataSize);
    for (std::uint32_t i = 0; i < samples; ++i) {
        out.write<std::int16_t>(static_cast<std::int16_t>(8000.0 * std::sin(6.283185307 * frequency * i / sampleRate)));
    }
    return out.bytes();
}

// A file that goes when the test does
struct SoundFile
{
    std::filesystem::path path;

    explicit SoundFile(const char* name, double seconds = 0.2)
        : path(std::filesystem::temp_directory_path() / ("kuge_test_" + std::to_string(::getpid()) + "_" + name + ".wav"))
    {
        kuge::writeFile(path, makeWav(seconds));
    }
    ~SoundFile() { std::filesystem::remove(path); }
};
