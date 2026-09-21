#pragma once

#include "ThreadPool.hpp"
#include <any>
#include <atomic>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
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
     *
     * **Loading in the background.** Reading and decoding a file is slow, and
     * often does not need the thread that owns the asset (a texture must be made
     * by the thread of the renderer, but its picture can be decoded anywhere).
     * With enableAsync(), loadAsync() asks the worker threads for the first half
     * (prepare: file to any data), and the owner's pump() does the second half
     * (finish: data to asset), so that the asset is always made by that thread.
     * loadAsync() gives a ticket at once; it turns Ready after a pump().
     */
    ////////////////////////////////////////////////////////////////////////////
    template<typename T>
    class AssetManager
    {
        public:
            using Loader   = std::function<std::shared_ptr<T>(const std::filesystem::path&)>;
            using Reloader = std::function<void(T&, const std::filesystem::path&)>;

            //! The answer to loadAsync(): the asset when it is ready
            class Ticket
            {
                public:
                    enum class State { Loading, Ready, Failed };

                    State state(void) const noexcept { return m_state.load(std::memory_order_acquire); }
                    bool  done(void) const noexcept { return state() != State::Loading; }

                    //! The asset once Ready, nullptr before. (The ticket keeps it alive: let go of it.)
                    std::shared_ptr<T> asset(void) const noexcept { return state() == State::Ready ? m_asset : nullptr; }

                    //! Why it failed (once Failed)
                    const std::string& error(void) const noexcept { return m_error; }

                private:
                    friend class AssetManager;

                    std::atomic<State>  m_state{State::Loading};
                    std::shared_ptr<T>  m_asset;
                    std::string         m_error;
                };

            //! First half of a background load, on a worker: reads the file into any data
            using Prepare = std::function<std::any(const std::filesystem::path&)>;
            //! Second half, on the thread that calls pump(): makes the asset from the data
            using Finish = std::function<std::shared_ptr<T>(std::any&&, const std::filesystem::path&)>;

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

            //! Allows loadAsync(). @param pool  Gives the worker threads (called when one is needed)
            void enableAsync(std::function<ThreadPool&(void)> pool, Prepare prepare, Finish finish)
            {
                std::lock_guard lock(m_mutex);

                m_async = Async{std::move(pool), std::move(prepare), std::move(finish)};
            }

            //! Like load(), without waiting: the file is read on a worker, and the asset is made
            //! by the next pump(). Asking again for a file that is on its way shares its load.
            //! @throw std::logic_error without enableAsync()
            std::shared_ptr<Ticket> loadAsync(const std::filesystem::path& path)
            {
                std::lock_guard lock(m_mutex);
                auto ticket = std::make_shared<Ticket>();

                if (!m_async) {
                    throw std::logic_error("AssetManager::loadAsync() needs enableAsync()");
                }
                const std::string key = path.lexically_normal().string();
                auto found = m_cache.find(key);

                if (found != m_cache.end()) {
                    if (auto alive = found->second.asset.lock()) {
                        settle(*ticket, std::move(alive), {});
                        return ticket;
                    }
                }
                auto& waiting = m_waiting[key];

                waiting.push_back(ticket);
                if (waiting.size() == 1) {
                    try {
                        m_async->pool().post([shared = m_shared, prepare = m_async->prepare, key, path, stamp = stampOf(path)] {
                            Done done{key, path, stamp, {}, {}, false};

                            try {
                                done.prepared = prepare(path);
                            } catch (const std::exception& error) {
                                done.failed = true;
                                done.error = error.what();
                            }
                            std::lock_guard lock(shared->mutex);
                            shared->done.push_back(std::move(done));
                        });
                    } catch (const std::exception& error) {
                        for (auto& t : m_waiting.extract(key).mapped()) {
                            settle(*t, nullptr, error.what());
                        }
                    }
                }
                return ticket;
            }

            //! Finishes the loads that the workers have read: makes the assets, and turns their
            //! tickets Ready (or Failed). Call it from the thread that owns the assets.
            //! @return  How many loads were finished
            std::size_t pump(void)
            {
                std::vector<Done> finished;

                {
                    std::lock_guard lock(m_shared->mutex);
                    finished.swap(m_shared->done);
                }
                for (Done& done : finished) {
                    std::shared_ptr<T> asset;
                    std::string error = done.error;
                    Finish finish;

                    {
                        std::lock_guard lock(m_mutex);
                        finish = m_async ? m_async->finish : Finish{};
                    }
                    if (!done.failed && finish) {
                        try {
                            asset = finish(std::move(done.prepared), done.path);   // not under the lock: it is the game's code
                        } catch (const std::exception& e) {
                            error = e.what();
                        }
                    }
                    std::lock_guard lock(m_mutex);
                    auto tickets = m_waiting.extract(done.key);

                    if (asset) {
                        // A load() may have made it in the meantime: there is only one
                        auto known = m_cache.find(done.key);

                        if (known != m_cache.end() && known->second.asset.lock()) {
                            asset = known->second.asset.lock();
                        } else {
                            m_cache[done.key] = Entry{asset, done.path, done.stamp};
                        }
                    }
                    if (!tickets.empty()) {
                        for (auto& ticket : tickets.mapped()) {
                            settle(*ticket, asset, error);
                        }
                    }
                }
                return finished.size();
            }

            //! Loads that were asked for and are not finished yet
            std::size_t loading(void) const
            {
                std::lock_guard lock(m_mutex);

                return m_waiting.size();
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

            struct Async
            {
                std::function<ThreadPool&(void)> pool;
                Prepare                          prepare;
                Finish                           finish;
            };

            //! What a worker read, waiting for pump()
            struct Done
            {
                std::string            key;
                std::filesystem::path  path;
                Stamp                  stamp;
                std::any               prepared;
                std::string            error;
                bool                   failed;
            };

            struct Shared
            {
                std::mutex        mutex;
                std::vector<Done> done;
            };

            static void settle(Ticket& ticket, std::shared_ptr<T> asset, const std::string& error)
            {
                ticket.m_asset = std::move(asset);
                ticket.m_error = ticket.m_asset ? std::string() : (error.empty() ? std::string("could not be loaded") : error);
                ticket.m_state.store(ticket.m_asset ? Ticket::State::Ready : Ticket::State::Failed, std::memory_order_release);
            }

            Loader                                     m_loader;
            Reloader                                   m_reloader;
            mutable std::mutex                         m_mutex;
            std::unordered_map<std::string, Entry>     m_cache;
            std::optional<Async>                       m_async;
            std::shared_ptr<Shared>                    m_shared = std::make_shared<Shared>();   // (what workers use: it outlives the manager)
            std::unordered_map<std::string, std::vector<std::shared_ptr<Ticket>>> m_waiting;
    };

}
