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
    IRenderer2D& renderer, int width, int height, std::span<const std::uint8_t> rgba)
{
    const TextureId id = renderer.createTexture(width, height, rgba);

    return std::make_shared<Texture>(renderer, id, width, height);
}

std::shared_ptr<kuge::Texture> kuge::Texture::fromFile(IRenderer2D& renderer, const std::filesystem::path& path)
{
    const Image image = decodeImageFile(path);

    return fromPixels(renderer, image.width, image.height, image.rgba);
}

void kuge::Texture::replace(int width, int height, std::span<const std::uint8_t> rgba)
{
    const TextureId id = m_renderer.createTexture(width, height, rgba);   // first: it may throw

    m_renderer.destroyTexture(m_id);
    m_id = id;
    m_width = width;
    m_height = height;
}

void kuge::Texture::reloadFromFile(const std::filesystem::path& path)
{
    const Image image = decodeImageFile(path);

    replace(image.width, image.height, image.rgba);
}
