#include "Pong.hpp"
#include "backend/SdlBackend.hpp"
#include "Logger.hpp"
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace
{
    // A picture as a PPM file: the simplest format there is, and any viewer reads it
    bool writePpm(const char* path, const kuge::Image& image)
    {
        std::ofstream out(path, std::ios::binary);

        out << "P6\n" << image.width << ' ' << image.height << "\n255\n";
        for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
            out.write(reinterpret_cast<const char*>(&image.rgba[i]), 3);
        }
        return static_cast<bool>(out);
    }

    // What a player would do in the first seconds, for --frames
    void scriptedPlayer(kuge::InputMap& input, int frame)
    {
        if (frame == 5) {
            input.handle(kuge::KeyEvent{kuge::Key::Space, true});
            input.handle(kuge::KeyEvent{kuge::Key::Space, false});
        }
        if (frame == 10) {
            input.handle(kuge::KeyEvent{kuge::Key::S, true});
        }
        if (frame == 45) {
            input.handle(kuge::KeyEvent{kuge::Key::S, false});
            input.handle(kuge::KeyEvent{kuge::Key::Up, true});
        }
    }
}

// pong                       plays
// pong --frames N [--screenshot file.ppm]
//                            plays N frames of a scripted game, as fast as it can
//                            (no waiting), and keeps a picture of the last one
int main(int argc, char** argv)
{
    int frames = 0;
    const char* screenshot = nullptr;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot = argv[++i];
        } else {
            Logger::logger().error("usage: pong [--frames N] [--screenshot file.ppm]");
            return 2;
        }
    }
    try {
        kuge::WindowConfig window;

        window.title = "KUGE Pong";
        window.width = static_cast<int>(pong::FIELD_W);
        window.height = static_cast<int>(pong::FIELD_H);

        kuge::Engine engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed, .tickRate = 60});
        auto& client = engine.addModule<kuge::ClientModule>(kuge::makeSdlBackend(window),
            kuge::ClientConfig{kuge::Color{16, 16, 24, 255}});

        // The defaults, then what the player changed: keybinds.cfg is written on
        // the first launch, so that there is something to edit
        pong::bindDefaults(client.input());
        if (frames == 0 && !client.input().loadBindings("keybinds.cfg")) {
            client.input().saveBindings("keybinds.cfg");
        }
        engine.scenes().change<pong::PongScene>();

        if (frames == 0) {
            return engine.run();
        }
        for (int frame = 0; frame < frames; ++frame) {
            scriptedPlayer(client.input(), frame);
            if (frame == frames - 1) {
                client.requestScreenshot();
            }
            if (!engine.step(1.0 / 60.0)) {
                break;
            }
        }
        if (screenshot) {
            const auto image = client.takeScreenshot();

            if (!image || !writePpm(screenshot, *image)) {
                Logger::logger().error("cannot keep the screenshot");
                return 1;
            }
        }
        return 0;
    } catch (const std::exception& error) {
        Logger::logger().error("{}", error.what());
        return 1;
    }
}
