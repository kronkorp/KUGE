#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#include "stb_image.h"

#include "render/ImageDecoder.hpp"
#include "Serializer.hpp"
#include <format>
#include <memory>

kuge::Image kuge::decodeImage(std::span<const std::uint8_t> data)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &width, &height, &channels, 4),
        &stbi_image_free);

    if (!pixels) {
        throw ImageError(std::format("cannot read the picture: {}", stbi_failure_reason()));
    }
    Image image;

    image.width = width;
    image.height = height;
    image.rgba.assign(pixels.get(), pixels.get() + static_cast<std::size_t>(width) * height * 4);
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
