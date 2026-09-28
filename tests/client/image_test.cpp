extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Serializer.hpp"
#include "backend/dummy/DummyBackend.hpp"
#include "render/ImageDecoder.hpp"
#include "render/Texture.hpp"
#include <cstring>
#include <filesystem>
#include <unistd.h>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    // A 2x2 BMP: red and green on top, blue and white below
    std::vector<std::uint8_t> bmp2x2(void)
    {
        kuge::ByteWriter out;

        out.write<char>('B');
        out.write<char>('M');
        out.write<std::uint32_t>(54 + 16);   // file size
        out.write<std::uint32_t>(0);
        out.write<std::uint32_t>(54);        // where the pixels start
        out.write<std::uint32_t>(40);        // size of this header
        out.write<std::int32_t>(2);          // width
        out.write<std::int32_t>(2);          // height (positive: rows go from the bottom)
        out.write<std::uint16_t>(1);         // planes
        out.write<std::uint16_t>(24);        // bits per pixel
        out.write<std::uint32_t>(0);         // no compression
        out.write<std::uint32_t>(16);        // size of the pixels
        out.write<std::int32_t>(0);
        out.write<std::int32_t>(0);
        out.write<std::uint32_t>(0);
        out.write<std::uint32_t>(0);
        // Bottom row first, blue-green-red, each row padded to 4 bytes
        for (std::uint8_t byte : {255, 0, 0,    255, 255, 255,   0, 0}) {   // blue, white
            out.write<std::uint8_t>(byte);
        }
        for (std::uint8_t byte : {0, 0, 255,    0, 255, 0,       0, 0}) {   // red, green
            out.write<std::uint8_t>(byte);
        }
        return out.bytes();
    }

    const std::uint8_t EXPECTED[] = {
        255, 0, 0, 255,       0, 255, 0, 255,        // red, green
        0, 0, 255, 255,       255, 255, 255, 255,    // blue, white
    };

    std::filesystem::path tempPath(const char* name)
    {
        return std::filesystem::temp_directory_path()
            / ("kuge_test_" + std::to_string(::getpid()) + "_" + name);
    }
}

Test(image, decode_from_memory)
{
    const auto image = kuge::decodeImage(bmp2x2());

    AssertEq(image.width, 2, "width");
    AssertEq(image.height, 2, "height");
    AssertEq(image.rgba.size(), sizeof(EXPECTED), "4 bytes a pixel");
    AssertEq(std::memcmp(image.rgba.data(), EXPECTED, sizeof(EXPECTED)), 0, "top row first, as RGBA");
}

Test(image, decode_from_file)
{
    const auto path = tempPath("picture.bmp");
    const auto bytes = bmp2x2();

    kuge::writeFile(path, bytes);
    const auto image = kuge::decodeImageFile(path);
    std::filesystem::remove(path);
    AssertEq(image.width, 2, "read from a file");
    AssertEq(std::memcmp(image.rgba.data(), EXPECTED, sizeof(EXPECTED)), 0, "same pixels");
}

Test(image, bad_data_is_an_error)
{
    const std::uint8_t garbage[] = {1, 2, 3, 4, 5, 6, 7, 8};
    bool garbageRefused = false;
    bool emptyRefused = false;
    bool missingRefused = false;
    std::string message;

    try { kuge::decodeImage(garbage); } catch (const kuge::ImageError&) { garbageRefused = true; }
    try { kuge::decodeImage({}); } catch (const kuge::ImageError&) { emptyRefused = true; }
    try {
        kuge::decodeImageFile(tempPath("nothing.png"));
    } catch (const kuge::ImageError& error) {
        missingRefused = true;
        message = error.what();
    }
    Assert(garbageRefused, "not a picture");
    Assert(emptyRefused, "nothing at all");
    Assert(missingRefused, "no such file");
    Assert(message.find("nothing.png") != std::string::npos, "the message names the file: '%s'", message.c_str());
}

Test(image, texture_from_a_file)
{
    const auto path = tempPath("texture.bmp");
    auto dummy = kuge::makeDummyBackend();

    kuge::writeFile(path, bmp2x2());
    {
        auto texture = kuge::Texture::fromFile(*dummy.renderer, path);

        AssertEq(texture->width(), 2, "width");
        AssertEq(texture->height(), 2, "height");
        Assert(texture->size() == kuge::Vec2(2.0f, 2.0f), "size");
        AssertEq(dummy.renderer->textureCount(), 1, "the renderer keeps it");
        const auto* info = dummy.renderer->texture(texture->id());
        Assert(info && info->width == 2 && info->rgba.size() == sizeof(EXPECTED), "with its pixels");
    }
    AssertEq(dummy.renderer->textureCount(), 0, "and lets it go when nobody holds it");
    std::filesystem::remove(path);
}

Test(image, texture_needs_the_right_pixels)
{
    auto dummy = kuge::makeDummyBackend();
    const std::uint8_t three[3] = {1, 2, 3};
    bool refused = false;

    try { kuge::Texture::fromPixels(*dummy.renderer, 1, 1, three); } catch (const std::runtime_error&) { refused = true; }
    Assert(refused, "3 bytes are not a pixel");
    AssertEq(dummy.renderer->textureCount(), 0, "nothing was kept");
}
