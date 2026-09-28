#include "backend/dummy/DummyBackend.hpp"
#include <stdexcept>

void kuge::DummyRenderer::begin(Color clear)
{
    m_clear = clear;
    m_current.clear();
}

void kuge::DummyRenderer::present(void)
{
    m_last = m_current;
    ++m_presented;
}

kuge::TextureId kuge::DummyRenderer::createTexture(int width, int height, std::span<const std::uint8_t> rgba)
{
    if (width <= 0 || height <= 0
        || rgba.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4) {
        throw std::runtime_error("DummyRenderer: the pixels do not match the size");
    }
    const TextureId id = m_next++;

    m_textures[id] = TextureInfo{width, height, {rgba.begin(), rgba.end()}};
    return id;
}

void kuge::DummyRenderer::destroyTexture(TextureId texture) noexcept
{
    m_textures.erase(texture);
}

void kuge::DummyRenderer::fillRect(const Rect& destination, Color color)
{
    m_current.push_back({Call::Kind::Fill, destination, color, {}});
}

void kuge::DummyRenderer::strokeRect(const Rect& destination, Color color)
{
    m_current.push_back({Call::Kind::Stroke, destination, color, {}});
}

void kuge::DummyRenderer::drawTexture(const TextureDraw& draw)
{
    m_current.push_back({Call::Kind::Texture, draw.destination, draw.tint, draw});
}

const kuge::DummyRenderer::TextureInfo* kuge::DummyRenderer::texture(TextureId id) const
{
    const auto found = m_textures.find(id);

    return found == m_textures.end() ? nullptr : &found->second;
}

kuge::DummyBackend kuge::makeDummyBackend(Vec2 size)
{
    auto window = std::make_unique<DummyWindow>(size);
    auto input = std::make_unique<DummyInput>();
    auto renderer = std::make_unique<DummyRenderer>(size);
    auto audio = std::make_unique<DummyAudio>();
    auto fonts = std::make_unique<DummyFontLoader>();
    DummyBackend result{{}, window.get(), input.get(), renderer.get(), audio.get(), fonts.get()};

    result.backend.window = std::move(window);
    result.backend.input = std::move(input);
    result.backend.renderer = std::move(renderer);
    result.backend.audio = std::move(audio);
    result.backend.fonts = std::move(fonts);
    return result;
}
