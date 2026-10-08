#pragma once

#include <filesystem>
#include <string>

namespace kuge
{

    //! A path as SDL takes it: in UTF-8, on every platform. (path::c_str() is wide on Windows,
    //! and path::string() is in the code page of the machine there.)
    inline std::string sdlPath(const std::filesystem::path& path)
    {
        const std::u8string utf8 = path.u8string();

        return std::string(utf8.begin(), utf8.end());
    }

}
