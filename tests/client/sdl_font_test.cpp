extern "C" {
    #include "kronklab/kronklab.h"
}
#include "backend/SdlBackend.hpp"
#include "ui/TextRenderer.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    // Any of these will do; the tests say so out loud when none is here
    std::filesystem::path systemFont(void)
    {
        for (const char* candidate : {
                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                "/usr/share/fonts/dejavu/DejaVuSans.ttf",
                "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
                "/usr/share/fonts/TTF/DejaVuSans.ttf"}) {
            if (std::filesystem::exists(candidate)) {
                return candidate;
            }
        }
        return {};
    }

    // No font on this machine: not a failure of the code, but it must not pass unseen
    bool skipped(const std::filesystem::path& font)
    {
        if (font.empty()) {
            klInfo("SKIPPED: no TrueType font found on this machine, the SDL_ttf test did not run\n");
        }
        return font.empty();
    }
}

Test(sdlfont, measures_text)
{
    const auto file = systemFont();

    if (skipped(file)) { return; }
    auto loader = kuge::makeSdlFontLoader();
    auto font = loader->load(file, 24);
    const auto hello = font->measure("Hello");

    Assert(hello.x > 20.0f && hello.y > 10.0f, "a word has a size: %f x %f", hello.x, hello.y);
    Assert(font->measure("Hello, world").x > hello.x, "more letters, wider");
    Assert(font->measure("").x == 0.0f && font->measure("").y > 0.0f, "no text: no width, still a line high");
    Assert(font->lineHeight() >= 24, "a line is at least as high as the letters: %d", font->lineHeight());
    Assert(font->measure("caf\xC3\xA9 \xE2\x82\xAC").x > 0.0f, "UTF-8: an accent and a euro sign");
    const auto twoLines = font->measure("Hello\nHello");
    Assert(twoLines.y > hello.y * 1.7f && twoLines.x <= hello.x + 1.0f, "a line break starts another line: %f", twoLines.y);
    const auto small = loader->load(file, 12);
    Assert(small->measure("Hello").x < hello.x, "a smaller size is narrower");
}

Test(sdlfont, wrapping)
{
    const auto file = systemFont();

    if (skipped(file)) { return; }
    auto font = kuge::makeSdlFontLoader()->load(file, 20);
    const auto free = font->measure("one two three four five six seven");
    const auto wrapped = font->measure("one two three four five six seven", 120);

    Assert(wrapped.x <= 121.0f, "kept within the width: %f", wrapped.x);
    Assert(wrapped.y > free.y * 1.8f, "so it is taller: %f against %f", wrapped.y, free.y);
}

Test(sdlfont, a_picture_of_the_text)
{
    const auto file = systemFont();

    if (skipped(file)) { return; }
    auto font = kuge::makeSdlFontLoader()->load(file, 24);
    const auto image = font->rasterize("Hi");
    const auto size = font->measure("Hi");

    AssertEq(image.width, static_cast<int>(size.x), "as wide as measured");
    AssertEq(image.height, static_cast<int>(size.y), "as high");
    AssertEq(image.rgba.size(), static_cast<std::size_t>(image.width) * image.height * 4, "4 bytes a pixel");
    int solid = 0, clear = 0;
    bool white = true;

    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
        solid += image.rgba[i + 3] > 200 ? 1 : 0;
        clear += image.rgba[i + 3] == 0 ? 1 : 0;
        if (image.rgba[i + 3] > 200 && (image.rgba[i] != 255 || image.rgba[i + 1] != 255 || image.rgba[i + 2] != 255)) {
            white = false;
        }
    }
    Assert(solid > 20, "there is ink: %d solid pixels", solid);
    Assert(clear > solid, "and a clear background around it");
    Assert(white, "the ink is white, so that it can be tinted");
    Assert(font->rasterize("").empty(), "an empty text has no picture");
}

Test(sdlfont, fonts_come_and_go)
{
    const auto file = systemFont();

    if (skipped(file)) { return; }
    auto loader = kuge::makeSdlFontLoader();
    auto first = loader->load(file, 16);
    auto second = loader->load(file, 20);

    first.reset();                                   // the library stays for the other one
    Assert(second->measure("still here").x > 0.0f, "one font goes, the other still works");
    second.reset();
    auto third = loader->load(file, 16);             // the library was closed: it starts again
    Assert(third->measure("again").x > 0.0f, "and can be started again after all were gone");
}

Test(sdlfont, bad_files)
{
    auto loader = kuge::makeSdlFontLoader();
    const auto garbage = std::filesystem::temp_directory_path() / ("kuge_test_" + std::to_string(::getpid()) + "_garbage.ttf");
    bool missing = false, notFont = false;

    {
        std::ofstream file(garbage);
        file << "this is not a font";
    }
    try { loader->load("/nonexistent/font.ttf", 16); } catch (const kuge::FontError&) { missing = true; }
    try { loader->load(garbage, 16); } catch (const kuge::FontError&) { notFont = true; }
    std::filesystem::remove(garbage);
    Assert(missing, "a file that is not there");
    Assert(notFont, "a file that is not a font");
}

Test(sdlfont, text_on_the_screen)
{
    const auto file = systemFont();

    if (skipped(file)) { return; }
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_RENDER_DRIVER", "software", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    kuge::WindowConfig window;

    window.width = 200;
    window.height = 60;
    window.vsync = false;
    auto backend = kuge::makeSdlBackend(window);
    auto font = backend.fonts->load(file, 28);
    kuge::TextRenderer text(*backend.renderer);

    backend.renderer->begin({0, 0, 0, 255});
    text.draw(*font, "Hello", {10.0f, 10.0f}, {255, 0, 0, 255});
    const auto shot = backend.renderer->readPixels();
    int red = 0;
    int stray = 0;

    for (int y = 0; y < shot.height; ++y) {
        for (int x = 0; x < shot.width; ++x) {
            const auto* pixel = &shot.rgba[(static_cast<std::size_t>(y) * shot.width + x) * 4];

            if (pixel[0] > 200 && pixel[1] < 40 && pixel[2] < 40) {
                ++red;
            } else if (pixel[1] > 40 || pixel[2] > 40) {
                ++stray;     // the text was tinted red: nothing else lit
            }
        }
    }
    Assert(red > 40, "the text shows on the screen, in red: %d pixels", red);
    AssertEq(stray, 0, "and only in red");
}
