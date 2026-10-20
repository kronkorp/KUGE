#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace kuge
{

    //! A file that none of the roots has: the message names every root searched
    class AssetNotFound : public std::runtime_error
    {
        public:
            using std::runtime_error::runtime_error;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The folders that files are looked for in, in order: the game's own, and the mods on top of it
     *
     *     kuge::AssetRoots roots(".");                      // the game's folder, the last one looked in
     *     mods.apply(roots);                                // each mod on top of it, in its load order
     *     roots.resolve("data/weapons/wand.json");          // the file of the highest root that has it
     *     client.textures().setRoots(roots);                // textures("hero.png") finds the mod's hero.png first
     *
     * A path that is absolute is not searched: it is used as it is. The roots are added at the start and read from then on (any thread, as
     * long as nobody adds).
     */
    ////////////////////////////////////////////////////////////////////////////
    class AssetRoots
    {
        public:
            struct Root
            {
                std::filesystem::path path;
                int                   priority = 0;   //!< The higher is looked in first
                std::string           name;           //!< For messages: "game", the name of a mod
            };

            AssetRoots(void) = default;

            //! The game's own folder, with the lowest priority
            explicit AssetRoots(std::filesystem::path game) { add(std::move(game), 0, "game"); }

            //! Adds a folder. The highest priority is looked in first; at the same priority, the last one added.
            void add(std::filesystem::path path, int priority, std::string name = {});

            //! The file of the highest root that has it; nothing if none does. `relative` is a path with "/" such as "data/weapons/wand.json".
            std::optional<std::filesystem::path> find(const std::filesystem::path& relative) const;

            //! @throw AssetNotFound naming each root searched
            std::filesystem::path resolve(const std::filesystem::path& relative) const;

            //! The root that gave this file (its name), for a message or a log; "" if no root has it
            std::string whoHas(const std::filesystem::path& relative) const;

            //! Every file of a folder, in all the roots, once by name (the highest root's file), by name
            std::vector<std::filesystem::path> list(const std::filesystem::path& folder) const;

            //! Highest first
            const std::vector<Root>& roots(void) const noexcept { return m_roots; }

        private:
            std::vector<Root> m_roots;
    };

}
