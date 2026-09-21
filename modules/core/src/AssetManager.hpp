#pragma once

#include <cstddef>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Loads files once and shares them
     *
     * Asking twice for the same file gives the same object, for as long as
     * someone still holds it. When the last holder lets go, the asset is
     * destroyed, and the next load() reads the file again. The loader (a
     * function that reads a file into an asset) is given by whoever knows how:
     * the client gives one for textures, for example.
     *
     * A loader that fails throws, and nothing is kept for that file.
     *
     * **Hot reload.** With a reloader (a function that reads a file *into an
     * asset that already exists*), reloadChanged() finds the files that changed
     * since they were loaded (by date and size) and reloads them in place: the
     * object stays the same, so everyone who holds it sees the new content
     * without doing anything. A reloader that throws leaves the asset as it was
     * (it must not change it before it is sure to succeed); the file is tried
     * again only when it changes once more. A file that is missing is left
     * alone (editors often delete then write).
     */
    ////////////////////////////////////////////////////////////////////////////
    template<typename T>
    class AssetManager
    {
        public:
            using Loader   = std::function<std::shared_ptr<T>(const std::filesystem::path&)>;
            using Reloader = std::function<void(T&, const std::filesystem::path&)>;

            //! What reloadChanged() did
            struct ReloadReport
            {
                std::vector<std::filesystem::path>                        reloaded;
                std::vector<std::pair<std::filesystem::path, std::string>> failed;   //!< and why
            };

            explicit AssetManager(Loader loader, Reloader reloader = {})
                : m_loader(std::move(loader)), m_reloader(std::move(reloader)) {}

            AssetManager(const AssetManager&)            = delete;
            AssetManager& operator=(const AssetManager&) = delete;

            std::shared_ptr<T> load(const std::filesystem::path& path)
            {
                std::lock_guard lock(m_mutex);
                const std::string key = path.lexically_normal().string();
                auto found = m_cache.find(key);

                if (found != m_cache.end()) {
                    if (auto alive = found->second.asset.lock()) {
                        return alive;
                    }
                }
                const Stamp stamp = stampOf(path);   // before reading: a write during the read shows up later
                std::shared_ptr<T> asset = m_loader(path);

                m_cache[key] = Entry{asset, path, stamp};
                return asset;
            }

            //! Reloads, in place, the assets whose file changed since they were loaded.
            //! Call it from the thread that uses the assets (the client does, at the
            //! start of each loop, if it is asked to watch them).
            ReloadReport reloadChanged(void)
            {
                std::lock_guard lock(m_mutex);
                ReloadReport report;

                if (!m_reloader) {
                    return report;
                }
                for (auto& [key, entry] : m_cache) {
                    const auto asset = entry.asset.lock();
                    const Stamp now = asset ? stampOf(entry.path) : Stamp{};

                    if (!asset || !now.exists || now == entry.stamp) {
                        continue;
                    }
                    entry.stamp = now;
                    try {
                        m_reloader(*asset, entry.path);
                        report.reloaded.push_back(entry.path);
                    } catch (const std::exception& error) {
                        report.failed.emplace_back(entry.path, error.what());
                    }
                }
                return report;
            }

            //! How many assets are alive
            std::size_t loaded(void) const
            {
                std::lock_guard lock(m_mutex);
                std::size_t count = 0;

                for (const auto& [key, entry] : m_cache) {
                    count += entry.asset.expired() ? 0 : 1;
                }
                return count;
            }

            //! Forgets the assets that are gone
            void purge(void)
            {
                std::lock_guard lock(m_mutex);

                for (auto it = m_cache.begin(); it != m_cache.end();) {
                    it = it->second.asset.expired() ? m_cache.erase(it) : std::next(it);
                }
            }

        private:
            //! What tells that a file changed
            struct Stamp
            {
                bool                            exists = false;
                std::filesystem::file_time_type time{};
                std::uintmax_t                  size = 0;

                bool operator==(const Stamp& other) const noexcept
                {
                    return exists == other.exists && time == other.time && size == other.size;
                }
            };

            struct Entry
            {
                std::weak_ptr<T>       asset;
                std::filesystem::path  path;
                Stamp                  stamp;
            };

            static Stamp stampOf(const std::filesystem::path& path)
            {
                std::error_code error;
                Stamp stamp;

                stamp.time = std::filesystem::last_write_time(path, error);
                stamp.exists = !error;
                if (stamp.exists) {
                    stamp.size = std::filesystem::file_size(path, error);
                    stamp.exists = !error;
                }
                return stamp;
            }

            Loader                                     m_loader;
            Reloader                                   m_reloader;
            mutable std::mutex                         m_mutex;
            std::unordered_map<std::string, Entry>     m_cache;
    };

}
