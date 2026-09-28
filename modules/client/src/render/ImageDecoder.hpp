#pragma once

#include "backend/IRenderer2D.hpp"
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>

namespace kuge
{

    class ImageError : public std::runtime_error
    {
        public:
            using std::runtime_error::runtime_error;
    };

    //! PNG, JPEG, BMP, TGA, GIF (first frame)... as RGBA pixels
    //! @throw ImageError if it is not a picture we can read
    Image decodeImage(std::span<const std::uint8_t> data);

    //! @throw ImageError
    Image decodeImageFile(const std::filesystem::path& path);

}
