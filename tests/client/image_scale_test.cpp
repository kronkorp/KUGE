extern "C" {
    #include "kronklab/kronklab.h"
}
#include "backend/dummy/DummyBackend.hpp"
#include "render/ImageScale.hpp"
#include "render/Texture.hpp"
#include <vector>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    // A picture of grays, each given as one number (alpha 255)
    kuge::Image grays(int width, int height, const std::vector<int>& values)
    {
        kuge::Image image{width, height, {}};

        for (int value : values) {
            image.rgba.insert(image.rgba.end(), {static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value), 255});
        }
        return image;
    }

    bool is(const kuge::Image& image, int x, int y, int r, int g, int b, int a, int tolerance = 1)
    {
        const std::size_t at = (static_cast<std::size_t>(y) * image.width + x) * 4;

        return std::abs(image.rgba[at] - r) <= tolerance && std::abs(image.rgba[at + 1] - g) <= tolerance
            && std::abs(image.rgba[at + 2] - b) <= tolerance && std::abs(image.rgba[at + 3] - a) <= tolerance;
    }
}

Test(shrink, each_block_is_averaged)
{
    // Each 2 x 2 block alternates two grays: 0 and 100 (mean 50), 200 and 100 (150), 40 and 40 (40), 255 and 255 (255)
    const auto out = kuge::shrink(grays(4, 4, {0, 100, 200, 100,
                                               100, 0, 100, 200,
                                               40, 40, 255, 255,
                                               40, 40, 255, 255}), {}, 2, 2);

    AssertEq(out.width, 2, "width");
    AssertEq(out.height, 2, "height");
    Assert(is(out, 0, 0, 50, 50, 50, 255) && is(out, 1, 0, 150, 150, 150, 255), "the top blocks");
    Assert(is(out, 0, 1, 40, 40, 40, 255) && is(out, 1, 1, 255, 255, 255, 255), "the bottom ones");
}

Test(shrink, a_clear_pixel_does_not_darken)
{
    kuge::Image image{2, 1, {255, 255, 255, 255,   0, 0, 0, 0}};   // white, then clear (and black)
    const auto out = kuge::shrink(image, {}, 1, 1);

    Assert(is(out, 0, 0, 255, 255, 255, 128), "white, half clear: there is no dark fringe");
}

Test(shrink, half_covered_counts_half)
{
    // 3 pixels into 2: the first new one covers 1 and a half of the old (0, and half of 60: mean 20), the second the rest (60 and 120: 100)
    const auto out = kuge::shrink(grays(3, 1, {0, 60, 120}), {}, 2, 1);

    Assert(is(out, 0, 0, 20, 20, 20, 255) && is(out, 1, 0, 100, 100, 100, 255), "means weighed by what they cover");
}

Test(shrink, only_the_part_asked_for)
{
    const auto out = kuge::shrink(grays(4, 2, {0, 0, 200, 200,
                                               0, 0, 200, 200}), {2.0f, 0.0f, 2.0f, 2.0f}, 1, 1);

    Assert(is(out, 0, 0, 200, 200, 200, 255), "the right half");
}

Test(shrink, nonsense_is_empty)
{
    Assert(kuge::shrink({}, {}, 2, 2).empty(), "no picture");
    Assert(kuge::shrink(grays(2, 2, {0, 0, 0, 0}), {}, 0, 2).empty(), "no width");
    Assert(kuge::shrink(grays(2, 2, {0, 0, 0, 0}), {10.0f, 10.0f, 5.0f, 5.0f}, 1, 1).empty(), "a part that is outside");
    Assert(kuge::shrink(kuge::Image{2, 2, {1, 2, 3}}, {}, 1, 1).empty(), "pixels that do not match the size");
}

Test(texture, pixels_only_if_asked)
{
    auto dummy = kuge::makeDummyBackend({64.0f, 64.0f});
    const std::vector<std::uint8_t> pixels(2 * 2 * 4, 9);
    const auto plain = kuge::Texture::fromPixels(*dummy.renderer, 2, 2, pixels);
    const auto kept = kuge::Texture::fromPixels(*dummy.renderer, 2, 2, pixels, true);

    Assert(plain->pixels() == nullptr, "no copy by default");
    Assert(kept->pixels() != nullptr && kept->pixels()->rgba == pixels, "a copy when asked");
    AssertEq(kept->revision(), 0, "not changed yet");
    const std::vector<std::uint8_t> other(2 * 2 * 4, 77);

    kept->update(other);
    Assert(kept->pixels()->rgba == other && kept->revision() == 1, "an update changes the copy and the revision");
    const std::vector<std::uint8_t> bigger(3 * 1 * 4, 5);

    kept->replace(3, 1, bigger);
    Assert(kept->pixels()->width == 3 && kept->pixels()->rgba == bigger && kept->revision() == 2, "so does a replace");
}
