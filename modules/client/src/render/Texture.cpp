#include "render/Texture.hpp"
#include "render/ImageDecoder.hpp"

kuge::Texture::Texture(IRenderer2D& renderer, TextureId id, int width, int height) noexcept
    : m_renderer(renderer), m_id(id), m_width(width), m_height(height)
{
}

kuge::Texture::~Texture(void)
{
    m_renderer.destroyTexture(m_id);
}

std::shared_ptr<kuge::Texture> kuge::Texture::fromPixels(
    IRenderer2D& renderer, int width, int height, std::span<const std::uint8_t> rgba, bool keepPixels)
{
    const TextureId id = renderer.createTexture(width, height, rgba);
    auto texture = std::make_shared<Texture>(renderer, id, width, height);

    if (keepPixels) {
        texture->m_pixels = std::make_shared<const Image>(Image{width, height, {rgba.begin(), rgba.end()}});
    }
    return texture;
}

std::shared_ptr<kuge::Texture> kuge::Texture::fromFile(IRenderer2D& renderer, const std::filesystem::path& path, bool keepPixels)
{
    const Image image = decodeImageFile(path);

    return fromPixels(renderer, image.width, image.height, image.rgba, keepPixels);
}

void kuge::Texture::update(std::span<const std::uint8_t> rgba)
{
    m_renderer.updateTexture(m_id, rgba);   // (it throws if the pixels are not the right size: nothing below is changed then)
    if (m_pixels) {
        m_pixels = std::make_shared<const Image>(Image{m_width, m_height, {rgba.begin(), rgba.end()}});
    }
    ++m_revision;
}

void kuge::Texture::replace(int width, int height, std::span<const std::uint8_t> rgba)
{
    const TextureId id = m_renderer.createTexture(width, height, rgba);   // first: it may throw

    m_renderer.destroyTexture(m_id);
    m_id = id;
    m_width = width;
    m_height = height;
    if (m_pixels) {
        m_pixels = std::make_shared<const Image>(Image{width, height, {rgba.begin(), rgba.end()}});
    }
    ++m_revision;
}

void kuge::Texture::reloadFromFile(const std::filesystem::path& path)
{
    const Image image = decodeImageFile(path);

    replace(image.width, image.height, image.rgba);
}
