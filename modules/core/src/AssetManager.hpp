#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

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
     */
    ////////////////////////////////////////////////////////////////////////////
    template<typename T>
    class AssetManager
    {
        public:
            using Loader = std::function<std::shared_ptr<T>(const std::filesystem::path&)>;

            explicit AssetManager(Loader loader) : m_loader(std::move(loader)) {}

            AssetManager(const AssetManager&)            = delete;
            AssetManager& operator=(const AssetManager&) = delete;

            std::shared_ptr<T> load(const std::filesystem::path& path)
            {
                std::lock_guard lock(m_mutex);
                const std::string key = path.lexically_normal().string();
                auto found = m_cache.find(key);

                if (found != m_cache.end()) {
                    if (auto alive = found->second.lock()) {
                        return alive;
                    }
                }
                std::shared_ptr<T> asset = m_loader(path);

                m_cache[key] = asset;
                return asset;
            }

            //! How many assets are alive
            std::size_t loaded(void) const
            {
                std::lock_guard lock(m_mutex);
                std::size_t count = 0;

                for (const auto& [key, weak] : m_cache) {
                    count += weak.expired() ? 0 : 1;
                }
                return count;
            }

            //! Forgets the assets that are gone
            void purge(void)
            {
                std::lock_guard lock(m_mutex);

                for (auto it = m_cache.begin(); it != m_cache.end();) {
                    it = it->second.expired() ? m_cache.erase(it) : std::next(it);
                }
            }

        private:
            Loader                                          m_loader;
            mutable std::mutex                              m_mutex;
            std::unordered_map<std::string, std::weak_ptr<T>> m_cache;
    };

}
