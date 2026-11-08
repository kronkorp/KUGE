#include "Catch.hpp"
#include "backend/SdlBackend.hpp"
#include <cstdlib>
#include <cstring>
#include <fstream>

#ifndef CATCH_FOLDER
    #define CATCH_FOLDER "."   // the folder of the game: data/ and mods/ are in it
#endif

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
}

// kuge_modding                      plays, with the mods of <folder>/mods
// kuge_modding --no-mods            plays the game alone
// kuge_modding --mods <folder>      looks for the mods in another folder
// kuge_modding --frames N [--screenshot file.ppm]
//                                   plays N frames with nobody at the keyboard, as fast as it can, and keeps a picture of the last one
int main(int argc, char** argv)
{
    int frames = 0;
    bool withMods = true;
    const char* screenshot = nullptr;
    std::filesystem::path modsFolder = std::filesystem::path(CATCH_FOLDER) / "mods";

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (std::strcmp(argv[i], "--mods") == 0 && i + 1 < argc) {
            modsFolder = argv[++i];
        } else if (std::strcmp(argv[i], "--no-mods") == 0) {
            withMods = false;
        } else {
            Logger::logger().error("usage: kuge_modding [--no-mods] [--mods folder] [--frames N] [--screenshot file.ppm]");
            return 2;
        }
    }
    try {
        // The mods first: a mod that does not load (a bad mod.json, a script with a mistake) is said with its file and line, before any window
        const kuge::Mods mods = withMods ? kuge::Mods::scan(modsFolder) : kuge::Mods{};
        catchgame::Setup setup = catchgame::makeSetup(CATCH_FOLDER, mods);

        kuge::WindowConfig window;

        window.title = "KUGE Catch";
        window.width = static_cast<int>(catchgame::FIELD_W);
        window.height = static_cast<int>(catchgame::FIELD_H);

        kuge::Engine engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed, .tickRate = 60});
        auto& client = engine.addModule<kuge::ClientModule>(kuge::makeSdlBackend(window), kuge::ClientConfig{kuge::Color{16, 16, 24, 255}});

        catchgame::bindDefaults(client.input());
        engine.scenes().change<catchgame::CatchScene>(std::move(setup));

        if (frames == 0) {
            return engine.run();
        }
        for (int frame = 0; frame < frames; ++frame) {
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
