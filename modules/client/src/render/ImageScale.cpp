#include "render/ImageScale.hpp"
#include <algorithm>
#include <cmath>

kuge::Image kuge::shrink(const Image& source, Rect part, int width, int height)
{
    if (source.empty() || width <= 0 || height <= 0
        || source.rgba.size() != static_cast<std::size_t>(source.width) * static_cast<std::size_t>(source.height) * 4) {
        return {};
    }
    if (part.w <= 0.0f || part.h <= 0.0f) {
        part = {0.0f, 0.0f, static_cast<float>(source.width), static_cast<float>(source.height)};
    }
    const float left = std::clamp(part.x, 0.0f, static_cast<float>(source.width));
    const float top = std::clamp(part.y, 0.0f, static_cast<float>(source.height));
    const float right = std::clamp(part.right(), left, static_cast<float>(source.width));
    const float bottom = std::clamp(part.bottom(), top, static_cast<float>(source.height));

    if (right <= left || bottom <= top) {
        return {};
    }
    Image out;

    out.width = width;
    out.height = height;
    out.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
    const float stepX = (right - left) / static_cast<float>(width);
    const float stepY = (bottom - top) / static_cast<float>(height);

    for (int y = 0; y < height; ++y) {
        const float y0 = top + stepY * static_cast<float>(y);
        const float y1 = y0 + stepY;

        for (int x = 0; x < width; ++x) {
            const float x0 = left + stepX * static_cast<float>(x);
            const float x1 = x0 + stepX;
            float weight = 0.0f;       // the area covered
            float opaque = 0.0f;       // the area covered, times the alpha
            float sum[3] = {0.0f, 0.0f, 0.0f};

            for (int sy = static_cast<int>(std::floor(y0)); sy < static_cast<int>(std::ceil(y1)) && sy < source.height; ++sy) {
                const float h = std::min(y1, static_cast<float>(sy + 1)) - std::max(y0, static_cast<float>(sy));

                for (int sx = static_cast<int>(std::floor(x0)); sx < static_cast<int>(std::ceil(x1)) && sx < source.width; ++sx) {
                    const float area = h * (std::min(x1, static_cast<float>(sx + 1)) - std::max(x0, static_cast<float>(sx)));
                    const std::uint8_t* pixel = &source.rgba[(static_cast<std::size_t>(sy) * static_cast<std::size_t>(source.width) + static_cast<std::size_t>(sx)) * 4];
                    const float alpha = area * static_cast<float>(pixel[3]) / 255.0f;

                    weight += area;
                    opaque += alpha;
                    for (int c = 0; c < 3; ++c) {
                        sum[c] += alpha * static_cast<float>(pixel[c]);
                    }
                }
            }
            std::uint8_t* target = &out.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4];

            if (weight > 0.0f && opaque > 0.0f) {
                for (int c = 0; c < 3; ++c) {
                    target[c] = static_cast<std::uint8_t>(std::lround(sum[c] / opaque));
                }
                target[3] = static_cast<std::uint8_t>(std::lround(255.0f * opaque / weight));
            }
        }
    }
    return out;
}
