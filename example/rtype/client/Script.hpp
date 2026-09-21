#pragma once

// What the scripted runs (--frames) share: a pilot that plays by itself, and the picture of a frame

#include "ClientModule.hpp"
#include <fstream>

namespace rtype
{
    inline bool writePpm(const char* path, const kuge::Image& image)
    {
        std::ofstream out(path, std::ios::binary);

        out << "P6\n" << image.width << ' ' << image.height << "\n255\n";
        for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
            out.write(reinterpret_cast<const char*>(&image.rgba[i]), 3);
        }
        return static_cast<bool>(out);
    }

    // A pilot for --frames: goes right, weaves up and down, and fires all the time
    inline void scriptedPilot(kuge::InputMap& input, int frame)
    {
        using kuge::Key;

        if (frame == 1) {
            input.handle(kuge::KeyEvent{Key::Space, true});
        }
        if (frame % 90 == 5) {
            input.handle(kuge::KeyEvent{Key::Right, true});
            input.handle(kuge::KeyEvent{Key::Down, true});
        }
        if (frame % 90 == 20) {
            input.handle(kuge::KeyEvent{Key::Right, false});
            input.handle(kuge::KeyEvent{Key::Down, false});
        }
        if (frame % 90 == 50) {
            input.handle(kuge::KeyEvent{Key::Up, true});
        }
        if (frame % 90 == 80) {
            input.handle(kuge::KeyEvent{Key::Up, false});
            input.handle(kuge::KeyEvent{Key::Left, true});
        }
        if (frame % 90 == 88) {
            input.handle(kuge::KeyEvent{Key::Left, false});
        }
    }
}

