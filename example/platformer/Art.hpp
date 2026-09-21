#pragma once

// The pictures and the sounds of the demo, made by code: nothing to ship, and
// nothing to load. A game would load its own files (client.textures().load(...)).

#include "ClientModule.hpp"
#include "Serializer.hpp"
#include "animation/Animation.hpp"
#include "render/Spritesheet.hpp"
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace platformer
{

    // A picture being drawn, pixel by pixel
    class Pixels
    {
        public:
            Pixels(int width, int height) : m_width(width), m_height(height), m_rgba(static_cast<std::size_t>(width) * height * 4, 0) {}

            void rect(int x, int y, int w, int h, kuge::Color color)
            {
                for (int j = y; j < y + h; ++j) {
                    for (int i = x; i < x + w; ++i) {
                        set(i, j, color);
                    }
                }
            }

            void disc(float cx, float cy, float rx, float ry, kuge::Color color)
            {
                for (int j = 0; j < m_height; ++j) {
                    for (int i = 0; i < m_width; ++i) {
                        const float dx = (static_cast<float>(i) + 0.5f - cx) / rx;
                        const float dy = (static_cast<float>(j) + 0.5f - cy) / ry;

                        if (dx * dx + dy * dy <= 1.0f) {
                            set(i, j, color);
                        }
                    }
                }
            }

            void set(int x, int y, kuge::Color color)
            {
                if (x >= 0 && y >= 0 && x < m_width && y < m_height) {
                    std::uint8_t* at = &m_rgba[(static_cast<std::size_t>(y) * m_width + x) * 4];

                    at[0] = color.r; at[1] = color.g; at[2] = color.b; at[3] = color.a;
                }
            }

            int width(void) const { return m_width; }
            int height(void) const { return m_height; }
            const std::vector<std::uint8_t>& rgba(void) const { return m_rgba; }

        private:
            int                        m_width;
            int                        m_height;
            std::vector<std::uint8_t>  m_rgba;
    };

    inline std::shared_ptr<kuge::Spritesheet> sheetOf(kuge::IRenderer2D& renderer, const Pixels& pixels, int frame)
    {
        auto sheet = std::make_shared<kuge::Spritesheet>();

        sheet->texture = kuge::Texture::fromPixels(renderer, pixels.width(), pixels.height(), pixels.rgba());
        sheet->frameWidth = frame;
        sheet->frameHeight = pixels.height();
        return sheet;
    }

    // Tiles, from 1: the ground, bricks, a tuft of grass, a cloud
    inline std::shared_ptr<kuge::Spritesheet> makeTiles(kuge::IRenderer2D& renderer)
    {
        Pixels art(64, 16);
        const kuge::Color soil{120, 82, 50, 255}, dark{92, 62, 38, 255}, grass{86, 170, 70, 255}, brick{170, 70, 60, 255}, mortar{225, 200, 180, 255};

        art.rect(0, 0, 16, 16, soil);                                   // tile 1: the ground
        art.rect(0, 0, 16, 4, grass);
        for (int i = 0; i < 6; ++i) { art.rect(2 + i * 3, 7 + (i * 5) % 6, 2, 2, dark); }
        art.rect(16, 0, 16, 16, brick);                                 // tile 2: bricks
        for (int row = 0; row < 4; ++row) {
            art.rect(16, row * 4 + 3, 16, 1, mortar);
            art.rect(16 + ((row % 2) ? 4 : 10), row * 4, 1, 4, mortar);
        }
        for (int i = 0; i < 5; ++i) { art.rect(32 + 2 + i * 3, 10 - (i % 2) * 3, 1, 6 + (i % 2) * 3, grass); }   // tile 3: grass
        art.disc(56.0f, 8.0f, 6.0f, 3.5f, {235, 240, 250, 255});        // tile 4: a cloud
        art.disc(52.0f, 9.0f, 4.0f, 2.5f, {235, 240, 250, 255});
        return sheetOf(renderer, art, 16);
    }

    // Eight frames: 0-1 idle, 2-5 run, 6 jump
    inline std::shared_ptr<kuge::Spritesheet> makeHero(kuge::IRenderer2D& renderer)
    {
        Pixels art(128, 16);
        const kuge::Color shirt{60, 110, 230, 255}, skin{240, 200, 160, 255}, legs{50, 50, 90, 255};

        for (int frame = 0; frame < 8; ++frame) {
            const int x = frame * 16;
            const int bob = frame < 2 ? frame : 0;                                   // idle: breathes
            const int step = (frame >= 2 && frame <= 5) ? (frame % 2 ? 1 : -1) : 0;  // run: legs swing
            const bool jump = frame == 6;

            art.rect(x + 5, 3 + bob, 6, 5, skin);                                    // head
            art.rect(x + 6, 5 + bob, 1, 1, {20, 20, 20, 255});                       // eye
            art.rect(x + 4, 8 + bob, 8, 5, shirt);                                   // body
            art.rect(x + (jump ? 3 : 5), (jump ? 6 : 9) + bob, 2, 3, skin);          // arm
            art.rect(x + 5 + step, 13, 3, 3, legs);                                  // legs
            art.rect(x + 8 - step, 13, 3, 3, legs);
        }
        return sheetOf(renderer, art, 16);
    }

    // A coin that turns: four frames, from face on to edge on
    inline std::shared_ptr<kuge::Spritesheet> makeCoin(kuge::IRenderer2D& renderer)
    {
        Pixels art(64, 16);
        const float widths[4] = {5.0f, 3.5f, 1.5f, 3.5f};

        for (int frame = 0; frame < 4; ++frame) {
            art.disc(static_cast<float>(frame * 16) + 8.0f, 8.0f, widths[frame], 5.0f, {245, 200, 50, 255});
            art.disc(static_cast<float>(frame * 16) + 8.0f, 8.0f, widths[frame] * 0.5f, 3.0f, {255, 235, 130, 255});
        }
        return sheetOf(renderer, art, 16);
    }

    // A walking blob, two frames
    inline std::shared_ptr<kuge::Spritesheet> makeBlob(kuge::IRenderer2D& renderer)
    {
        Pixels art(32, 16);

        for (int frame = 0; frame < 2; ++frame) {
            art.disc(static_cast<float>(frame * 16) + 8.0f, 11.0f - static_cast<float>(frame), 6.0f, 4.0f + static_cast<float>(frame), {200, 60, 120, 255});
            art.rect(frame * 16 + 5, 8, 2, 2, {255, 255, 255, 255});
            art.rect(frame * 16 + 9, 8, 2, 2, {255, 255, 255, 255});
        }
        return sheetOf(renderer, art, 16);
    }

    inline std::shared_ptr<const kuge::AnimationSet> makeClips(void)
    {
        return std::make_shared<kuge::AnimationSet>(kuge::AnimationSet::parse(
            "clip idle fps=3  frames=0-1\n"
            "clip run  fps=12 frames=2-5\n"
            "clip jump fps=1  frames=6\n"
            "clip spin fps=8  frames=0-3\n"
            "clip walk fps=6  frames=0-1\n"));
    }

    // A beep: a sine that fades out, as a WAV file
    inline std::vector<std::uint8_t> beep(double frequency, double seconds, double slide = 0.0)
    {
        const int rate = 22050;
        const std::uint32_t samples = static_cast<std::uint32_t>(seconds * rate);
        kuge::ByteWriter out;

        for (char c : {'R', 'I', 'F', 'F'}) { out.write<char>(c); }
        out.write<std::uint32_t>(36 + samples * 2);
        for (char c : {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '}) { out.write<char>(c); }
        out.write<std::uint32_t>(16);
        out.write<std::uint16_t>(1);
        out.write<std::uint16_t>(1);
        out.write<std::uint32_t>(rate);
        out.write<std::uint32_t>(rate * 2);
        out.write<std::uint16_t>(2);
        out.write<std::uint16_t>(16);
        for (char c : {'d', 'a', 't', 'a'}) { out.write<char>(c); }
        out.write<std::uint32_t>(samples * 2);
        double phase = 0.0;

        for (std::uint32_t i = 0; i < samples; ++i) {
            const double t = static_cast<double>(i) / samples;

            phase += 6.283185307 * (frequency + slide * t) / rate;
            out.write<std::int16_t>(static_cast<std::int16_t>(9000.0 * (1.0 - t) * std::sin(phase)));
        }
        return out.bytes();
    }

    // Writes the sounds of the game, once, in a folder
    inline void writeSounds(const std::filesystem::path& folder)
    {
        std::error_code error;

        std::filesystem::create_directories(folder, error);
        kuge::writeFile(folder / "jump.wav", beep(330.0, 0.12, 300.0));
        kuge::writeFile(folder / "coin.wav", beep(880.0, 0.16, 500.0));
        kuge::writeFile(folder / "hurt.wav", beep(220.0, 0.30, -150.0));
        kuge::writeFile(folder / "menu.wav", beep(520.0, 0.05));
    }

}
