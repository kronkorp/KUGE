#pragma once
// What the scripted runs (--frames) share: a player that plays by itself, and the picture of a frame

#include "ClientModule.hpp"
#include <SDL_events.h>   // (not SDL.h: on Windows it renames main to SDL_main, which needs the SDL2main library)
#include <fstream>

namespace arena
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

    // A player for --frames: walks ahead into the coin that is in front of it, then turns on the spot, jumping now and then
    inline void scriptedPlayer(kuge::InputMap& input, int frame)
    {
        using kuge::Key;

        if (frame == 150) {
            input.handle(kuge::KeyEvent{Key::W, true});
        }
        if (frame == 190) {
            input.handle(kuge::KeyEvent{Key::W, false});
        }
        if (frame > 200) {
            // The view turns by the mouse, as it would for a person (SDL reports the move, the client reads it)
            SDL_Event event{};

            event.type = SDL_MOUSEMOTION;
            event.motion.xrel = 2;
            SDL_PushEvent(&event);
        }
        if (frame > 200 && frame % 100 == 0) {
            input.handle(kuge::KeyEvent{Key::Space, true});
        }
        if (frame > 200 && frame % 100 == 10) {
            input.handle(kuge::KeyEvent{Key::Space, false});
        }
    }

}
