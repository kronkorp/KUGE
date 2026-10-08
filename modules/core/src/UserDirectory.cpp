#include "UserDirectory.hpp"
#include <cstdlib>
#include <format>
#include <stdexcept>
#include <string>

namespace
{
    // An environment variable that is set and not empty
    std::string variable(const char* name)
    {
        const char* value = std::getenv(name);

        return value ? std::string(value) : std::string();
    }
}

std::filesystem::path kuge::userDirectory(UserDir kind, std::string_view game)
{
    if (game.empty() || game == "." || game == ".." || game.find_first_of("/\\") != std::string_view::npos) {
        throw std::invalid_argument(std::format("'{}' cannot be the name of a folder", game));
    }
    const bool config = kind == UserDir::Config;
#ifdef _WIN32
    // Both in the roaming application data: what belongs to the player follows them
    (void)config;
    std::filesystem::path base(variable("APPDATA"));

    if (base.empty()) {
        base = ".";
    }
#else
    std::filesystem::path base(variable(config ? "XDG_CONFIG_HOME" : "XDG_DATA_HOME"));

    if (base.empty()) {
        const std::string home = variable("HOME");

        base = home.empty() ? std::filesystem::path(".") : std::filesystem::path(home) / (config ? ".config" : ".local/share");
    }
#endif
    const std::filesystem::path directory = base / std::string(game);
    std::error_code error;

    std::filesystem::create_directories(directory, error);
    return directory;
}
