#include "ui/TextRenderer.hpp"
#include <algorithm>
#include <vector>

void kuge::TextRenderer::draw(const IFont& font, std::string_view text, Vec2 position, Color color, int wrapWidth)
{
    Key key{font.id(), std::string(text), wrapWidth};
    auto found = m_cache.find(key);

    if (found == m_cache.end()) {
        Entry entry{nullptr, m_frame};
        const Image image = font.rasterize(text, wrapWidth);

        if (!image.empty()) {
            entry.texture = Texture::fromPixels(m_renderer, image.width, image.height, image.rgba);
        }
        found = m_cache.emplace(std::move(key), std::move(entry)).first;
    }
    found->second.lastUsed = m_frame;
    if (!found->second.texture) {
        return;
    }
    TextureDraw draw;

    draw.texture = found->second.texture->id();
    draw.destination = {position.x, position.y, static_cast<float>(found->second.texture->width()),
        static_cast<float>(found->second.texture->height())};
    draw.tint = color;
    m_renderer.drawTexture(draw);
}

void kuge::TextRenderer::beginFrame(void)
{
    ++m_frame;
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        it = it->second.lastUsed + KEEP_FRAMES < m_frame ? m_cache.erase(it) : std::next(it);
    }
    // Too many texts at once (a counter that changes every frame, say): the oldest go
    while (m_cache.size() > MAX_TEXTS) {
        auto oldest = std::min_element(m_cache.begin(), m_cache.end(),
            [](const auto& a, const auto& b) { return a.second.lastUsed < b.second.lastUsed; });

        m_cache.erase(oldest);
    }
}
