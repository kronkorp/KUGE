#pragma once
// What the scripted runs (--frames) share: a player that plays by itself, and the picture of a frame

#include "ArenaClient.hpp"
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

    // One move of the mouse, as SDL reports it (the client reads it as a person's)
    inline void pushMouse(int xrel)
    {
        SDL_Event event{};

        event.type = SDL_MOUSEMOTION;
        event.motion.xrel = xrel;
        SDL_PushEvent(&event);
    }

    // A player for --frames: turns to face the open side of the arena (it starts in a corner), runs, jumps while
    // running, then holds the trigger while the view turns slowly
    inline void scriptedPlayer(kuge::InputMap& input, int frame)
    {
        using kuge::Key;

        if (frame >= 120 && frame < 150) {
            pushMouse(-10);
        }
        if (frame >= 270 && frame < 350) {
            pushMouse(3);
        }
        if (frame == 170) {
            input.handle(kuge::KeyEvent{Key::W, true});
        }
        if (frame == 220) {
            input.handle(kuge::KeyEvent{Key::Space, true});
        }
        if (frame == 225) {
            input.handle(kuge::KeyEvent{Key::Space, false});
        }
        if (frame == 235) {
            input.handle(kuge::KeyEvent{Key::W, false});
        }
        if (frame == 260) {
            input.handle(kuge::MouseButtonEvent{kuge::MouseButton::Left, true, {}});
        }
        if (frame == 360) {
            input.handle(kuge::MouseButtonEvent{kuge::MouseButton::Left, false, {}});
        }
    }

    //! What a scripted player must have seen: it stood, ran, jumped and shot (its character played each clip), and
    //! its bullets were drawn
    inline bool scriptPassed(const ClientReport& report)
    {
        return report.joined && report.mostPlayers >= 1 && report.mostBullets >= 1 && report.clips.count("idle") && report.clips.count("run")
            && report.clips.count("jump") && report.clips.count("shoot");
    }

}
