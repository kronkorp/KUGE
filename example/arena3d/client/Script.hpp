#pragma once
// What the scripted runs (--frames) share: a player that plays by itself, and the picture of a frame

#include "ArenaClient.hpp"
#include "ClientModule.hpp"
#include <SDL_events.h>   // (not SDL.h: on Windows it renames main to SDL_main, which needs the SDL2main library)
#include "Engine.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <thread>

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
        if (frame == 235) {
            input.handle(kuge::KeyEvent{Key::Space, false});      // (a quarter of a second: a tap a slow machine can see)
            input.handle(kuge::KeyEvent{Key::W, false});
        }
        if (frame == 260) {
            input.handle(kuge::MouseButtonEvent{kuge::MouseButton::Left, true, {}});
        }
        if (frame == 360) {
            input.handle(kuge::MouseButtonEvent{kuge::MouseButton::Left, false, {}});
        }
    }

    //! Plays the script for `frames` frames of a game at 60 a second, in real time: what passes in the game is what
    //! passes on the clock (a slow machine draws fewer frames, and the player still runs, jumps and shoots for as long,
    //! at the same moments, as the server which lives in real time expects). Each frame number of the script is
    //! played, none is skipped. The last frame is kept as a picture.
    inline void playScript(kuge::Engine& engine, kuge::ClientModule& client, int frames)
    {
        using Clock = std::chrono::steady_clock;
        const auto start = Clock::now();
        auto last = start;
        int played = -1;                // the last frame of the script that was played

        for (bool finished = false; !finished;) {
            const auto now = Clock::now();
            const int reached = std::min(frames - 1, static_cast<int>(std::chrono::duration<double>(now - start).count() * 60.0));
            const double seconds = std::min(0.1, std::chrono::duration<double>(now - last).count());

            for (; played < reached; ) {
                scriptedPlayer(client.input(), ++played);
            }
            finished = reached >= frames - 1;
            if (finished) {
                client.requestScreenshot();
            }
            last = now;
            if (!engine.step(seconds)) {
                break;
            }
            if (seconds < 0.004) {
                std::this_thread::sleep_for(std::chrono::milliseconds(4));      // (a fast machine does not need to go faster)
            }
        }
    }

    //! What a scripted player must have seen: it stood, ran, jumped and shot (its character played each clip), and
    //! its bullets were drawn
    inline bool scriptPassed(const ClientReport& report)
    {
        return report.joined && report.mostPlayers >= 1 && report.mostBullets >= 1 && report.clips.count("idle") && report.clips.count("run")
            && report.clips.count("jump") && (report.clips.count("shooting") || report.clips.count("shooting_standing"));
    }

}
