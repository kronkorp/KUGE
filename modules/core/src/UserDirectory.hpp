#pragma once

#include <filesystem>
#include <string_view>

namespace kuge
{

    enum class UserDir
    {
        Config,   //!< Settings, keybinds
        Data,     //!< Saves
    };

    //! Where a game keeps what belongs to the player, and creates it:
    //! $XDG_CONFIG_HOME or ~/.config, and $XDG_DATA_HOME or ~/.local/share,
    //! then the name of the game. Without a home directory: the current one.
    //! On Windows, both are %APPDATA% (the roaming application data), then the
    //! name of the game.
    //! @throw std::invalid_argument if the name of the game cannot be a folder name
    std::filesystem::path userDirectory(UserDir kind, std::string_view game);

}
