#include "render/ImageDecoder.hpp"
#include "Serializer.hpp"
#include <format>
#include <kronk3d/scene/Image.hpp>

kuge::Image kuge::decodeImage(std::span<const std::uint8_t> data)
{
    // kronk3d already carries stb_image (a second copy would clash at link time)
    const k3::Result<k3::Image> decoded = k3::Image::loadFromMemory(data.data(), data.size());

    if (!decoded) {
        throw ImageError(std::format("cannot read the picture: {}", decoded.error()));
    }
    Image image;

    image.width = static_cast<int>(decoded->width);
    image.height = static_cast<int>(decoded->height);
    image.rgba = decoded->pixels;
    return image;
}

kuge::Image kuge::decodeImageFile(const std::filesystem::path& path)
{
    try {
        return decodeImage(readFile(path));
    } catch (const SerializerError&) {
        throw ImageError(std::format("cannot open '{}'", path.string()));
    } catch (const ImageError& error) {
        throw ImageError(std::format("'{}': {}", path.string(), error.what()));
    }
}
