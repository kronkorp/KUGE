#pragma once

#include "AssetRoots.hpp"
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace kuge
{

    //! A mod that cannot be used; the message names it
    class ModError : public std::runtime_error
    {
        public:
            using std::runtime_error::runtime_error;
    };

    struct ModInfo
    {
        std::string               name;
        std::string               version;
        std::string               kuge;        //!< The version of KUGE the mod was made for (the game checks it, see Mods::scan())
        std::vector<std::string>  depends;     //!< Names of the mods that must be loaded before this one
        std::filesystem::path     folder;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Mods: folders of data and resources laid over the game's, in a load order, with nothing to run
     *
     *     mods/dark_wand/mod.json        { "name": "dark_wand", "version": "1.0", "kuge": "0.1", "depends": ["base_pack"] }
     *     mods/dark_wand/data/weapons/wand.json         (replaces the game's)
     *     mods/dark_wand/resources/sprites/wand.png
     *
     *     auto mods = kuge::Mods::scan("mods");              // @throw ModError: a cycle, a missing dependency, a bad mod.json
     *     kuge::AssetRoots roots(".");                       // the game
     *     mods.apply(roots);                                 // the mods on top, the last one loaded wins
     *     for (const auto& o : mods.overrides(".")) log.info("{} replaces {} of {}", o.mod, o.file, o.replaces);
     *
     * The order is by `depends` (a mod comes after what it depends on) then by name, so it is the same on every machine. **A mod is data,
     * never code**: what it may change (a JSON of balance, a sprite, a sound) is the game's decision (it only loads what it reads through
     * the roots). In multiplayer the server decides: `fingerprint()` is a hash of the names, versions, files and their bytes, and a client
     * whose fingerprint is not the server's is refused (see MatchmakingClient::setMods()).
     */
    ////////////////////////////////////////////////////////////////////////////
    class Mods
    {
        public:
            struct Override
            {
                std::string file;       //!< Relative to the roots: "data/weapons/wand.json"
                std::string mod;        //!< The mod that replaces it
                std::string replaces;   //!< "game", or the mod that had it before
            };

            //! Reads the mods in the folders of `folder` (a folder without a mod.json is not a mod, and is ignored; none, or no `folder`: no mod).
            //! @param kugeVersion  If not empty, a mod that names another `kuge` is refused
            //! @throw ModError
            static Mods scan(const std::filesystem::path& folder, const std::string& kugeVersion = {});

            //! In load order: dependencies first, then by name
            const std::vector<ModInfo>& ordered(void) const noexcept { return m_mods; }
            bool empty(void) const noexcept { return m_mods.empty(); }

            //! Adds the mods to the roots, each on top of the one before
            void apply(AssetRoots& roots) const;

            //! The files that mods replace, in load order. `gameRoot` is the folder of the game.
            std::vector<Override> overrides(const std::filesystem::path& gameRoot) const;

            //! 64 bits that are the same for the same mods in the same order with the same files: 0 when there is no mod
            std::uint64_t fingerprint(void) const;

        private:
            std::vector<ModInfo> m_mods;
    };

}
