#include "Platformer.hpp"
#include "backend/SdlBackend.hpp"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <vector>

namespace
{
    std::filesystem::path findFont(void)
    {
        std::vector<std::filesystem::path> candidates = {
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf"};

        if (const char* windows = std::getenv("WINDIR")) {
            for (const char* font : {"segoeui.ttf", "arial.ttf", "tahoma.ttf"}) {
                candidates.push_back(std::filesystem::path(windows) / "Fonts" / font);
            }
        }
        for (const auto& candidate : candidates) {
            if (std::filesystem::exists(candidate)) {
                return candidate;
            }
        }
        return {};
    }

    bool writePpm(const char* path, const kuge::Image& image)
    {
        std::ofstream out(path, std::ios::binary);

        out << "P6\n" << image.width << ' ' << image.height << "\n255\n";
        for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
            out.write(reinterpret_cast<const char*>(&image.rgba[i]), 3);
        }
        return static_cast<bool>(out);
    }

    // What a player would do, for --frames: run right, and jump now and then
    void scriptedPlayer(kuge::InputMap& input, int frame)
    {
        if (frame == 3) {
            input.handle(kuge::KeyEvent{kuge::Key::D, true});
        }
        if (frame > 30 && frame % 33 == 0) {
            input.handle(kuge::KeyEvent{kuge::Key::Space, true});
            input.handle(kuge::KeyEvent{kuge::Key::Space, false});
        }
    }
}

// platformer                     plays (the menu first)
// platformer --font file.ttf     with this font instead of the one found on the system
// platformer --frames N [--screenshot file.ppm]
//                                plays N frames of a scripted game, as fast as it can (no
//                                waiting, in a temporary folder), and keeps a picture of the last
int main(int argc, char** argv)
{
    std::filesystem::path font = findFont();
    int frames = 0;
    const char* screenshot = nullptr;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--font") == 0 && i + 1 < argc) {
            font = argv[++i];
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot = argv[++i];
        } else {
            Logger::logger().error("usage: platformer [--font file.ttf] [--frames N] [--screenshot file.ppm]");
            return 2;
        }
    }
    if (font.empty()) {
        Logger::logger().warn("no font found on this machine (try --font file.ttf): the text will not show");
    }
    try {
        // Where things go: the folders of the player, or a temporary one for a scripted run
        const std::filesystem::path scratch = std::filesystem::temp_directory_path() / std::format("kuge-platformer-{}", std::random_device{}());
        const bool scripted = frames > 0;
        const auto data = scripted ? scratch : kuge::userDirectory(kuge::UserDir::Data, "kuge-platformer");
        const auto config = scripted ? scratch : kuge::userDirectory(kuge::UserDir::Config, "kuge-platformer");

        platformer::writeSounds(data / "sfx");
        auto shared = std::make_shared<platformer::Shared>(font, data / "sfx", data / "saves");
        kuge::WindowConfig window;

        window.title = "KUGE Platformer";
        window.width = 640;
        window.height = 360;
        kuge::Engine engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed, .tickRate = 60});
        auto& client = engine.addModule<kuge::ClientModule>(kuge::makeSdlBackend(window), kuge::ClientConfig{kuge::Color{110, 170, 230, 255}});

        platformer::bindDefaults(client.input());
        if (!scripted && !client.input().loadBindings(config / "keybinds.cfg")) {
            client.input().saveBindings(config / "keybinds.cfg");
        }
        if (scripted) {
            engine.scenes().change<platformer::GameScene>(shared, false);
        } else {
            engine.scenes().change<platformer::MenuScene>(shared);
        }
        if (!scripted) {
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
        const auto image = client.takeScreenshot();

        if (screenshot && (!image || !writePpm(screenshot, *image))) {
            Logger::logger().error("cannot keep the screenshot");
            return 1;
        }
        std::error_code error;

        std::filesystem::remove_all(scratch, error);
        return 0;
    } catch (const std::exception& error) {
        Logger::logger().error("{}", error.what());
        return 1;
    }
}
