#include "backend/dummy/DummyFont.hpp"
#include <algorithm>
#include <format>
#include <sstream>
#include <vector>

namespace
{
    // The lines of a text: cut at line breaks, and at spaces to stay within
    // the wrap width (a word that is longer than the width is left whole)
    std::vector<std::size_t> lineLengths(std::string_view text, int wrapWidth)
    {
        std::vector<std::size_t> lengths;
        const std::size_t limit = wrapWidth > 0 ? static_cast<std::size_t>(std::max(1, wrapWidth / kuge::DummyFont::LETTER)) : 0;
        std::size_t start = 0;

        while (start <= text.size()) {
            const auto end = text.find('\n', start);
            std::string_view line = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);

            while (limit > 0 && line.size() > limit) {
                auto cut = line.rfind(' ', limit);

                if (cut == std::string_view::npos || cut == 0) {
                    cut = line.find(' ');
                    if (cut == std::string_view::npos) {
                        break;
                    }
                }
                lengths.push_back(cut);
                line.remove_prefix(cut + 1);
            }
            lengths.push_back(line.size());
            if (end == std::string_view::npos) {
                break;
            }
            start = end + 1;
        }
        return lengths;
    }
}

kuge::Vec2 kuge::DummyFont::measure(std::string_view text, int wrapWidth) const
{
    if (text.empty()) {
        return {0.0f, static_cast<float>(LINE)};
    }
    const auto lengths = lineLengths(text, wrapWidth);
    const auto longest = *std::max_element(lengths.begin(), lengths.end());

    return {static_cast<float>(longest * LETTER), static_cast<float>(lengths.size() * LINE)};
}

kuge::Image kuge::DummyFont::rasterize(std::string_view text, int wrapWidth) const
{
    Image image;

    if (text.empty()) {
        return image;
    }
    const Vec2 size = measure(text, wrapWidth);

    image.width = static_cast<int>(size.x);
    image.height = static_cast<int>(size.y);
    if (image.width == 0) {
        image.width = 1;
    }
    image.rgba.assign(static_cast<std::size_t>(image.width) * image.height * 4, 255);
    return image;
}

std::shared_ptr<kuge::IFont> kuge::DummyFontLoader::load(const std::filesystem::path& file, int)
{
    if (!std::filesystem::exists(file)) {
        throw FontError(std::format("cannot open the font '{}'", file.string()));
    }
    ++loads;
    return std::make_shared<DummyFont>();
}
