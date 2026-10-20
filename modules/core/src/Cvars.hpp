#pragma once

#include "ConfigFile.hpp"
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace kuge
{

    //! What a variable is for (combine them with |)
    enum class CvarFlag : std::uint8_t {
        None       = 0,
        Persistent = 1,   //!< Saved by Cvars::save() and read back by load()
        ServerOnly = 2,   //!< Only the server (or a host) may change it
        ClientOnly = 4,   //!< Only a client may change it
        Cheat      = 8,   //!< Refused unless cheats are on (Cvars::setCheats())
    };

    constexpr CvarFlag operator|(CvarFlag a, CvarFlag b) noexcept
    {
        return static_cast<CvarFlag>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
    }

    constexpr bool hasFlag(CvarFlag flags, CvarFlag flag) noexcept
    {
        return (static_cast<std::uint8_t>(flags) & static_cast<std::uint8_t>(flag)) != 0;
    }

    namespace detail
    {
        //! What a variable keeps. Numbers are atomic, so a system reads them with no lock.
        struct CvarEntry
        {
            enum class Kind : std::uint8_t { Bool, Int, Float, String };

            std::string        name;
            std::string        help;
            CvarFlag           flags = CvarFlag::None;
            Kind               kind = Kind::Int;
            std::atomic<bool>  b{false};
            std::atomic<int>   i{0};
            std::atomic<float> f{0.0f};
            mutable std::mutex lock;   //!< For `s` only
            std::string        s;
        };
    }

    //! A handle on a variable: cheap to copy and to read (no lookup by name). Valid as long as its Cvars.
    template<typename T>
    class Cvar
    {
        public:
            Cvar(void) = default;

            T get(void) const
            {
                if constexpr (std::is_same_v<T, bool>) {
                    return m_entry->b.load();
                } else if constexpr (std::is_same_v<T, int>) {
                    return m_entry->i.load();
                } else if constexpr (std::is_same_v<T, float>) {
                    return m_entry->f.load();
                } else {
                    std::lock_guard lock(m_entry->lock);

                    return m_entry->s;
                }
            }

            operator T(void) const { return get(); }   // NOLINT: reads like the value

        private:
            friend class Cvars;

            explicit Cvar(detail::CvarEntry* entry) noexcept : m_entry(entry) {}

            detail::CvarEntry* m_entry = nullptr;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Named variables that can be read and changed while the game runs (the "cvars" of a console)
     *
     *     kuge::Cvars cvars;
     *     auto perKill = cvars.add("brainrot_per_kill", 2, kuge::CvarFlag::Cheat, "Rot a kill gives");
     *     ...
     *     const int rot = perKill.get();                                   // in a system: no lookup, no lock
     *     auto answer = cvars.set("brainrot_per_kill", "5");               // from the console: parsed and checked
     *
     * `set` never throws: it says what happened ("unknown variable", "'abc' is not a number for tick_rate", the cheat
     * refusal). Names are letters, digits, `_` and `.`. Variables are added at the start and live as long as the Cvars:
     * add them from one thread, read them from any.
     */
    ////////////////////////////////////////////////////////////////////////////
    class Cvars
    {
        public:
            //! Which side runs this program: ServerOnly variables are refused on a client, ClientOnly ones on a server
            enum class Side : std::uint8_t { Both, Server, Client };

            struct Result
            {
                bool        ok;
                std::string message;   //!< For the player: what is now, or why not
            };

            //! @throw std::invalid_argument if the name is not valid or is taken
            template<typename T>
            Cvar<T> add(std::string_view name, T value, CvarFlag flags = CvarFlag::None, std::string_view help = {})
            {
                static_assert(std::is_same_v<T, bool> || std::is_same_v<T, int> || std::is_same_v<T, float> || std::is_same_v<T, std::string>,
                    "a cvar is a bool, an int, a float or a string (add a std::string, not a const char*)");
                detail::CvarEntry& entry = make(name, flags, help);

                if constexpr (std::is_same_v<T, bool>) {
                    entry.kind = detail::CvarEntry::Kind::Bool;
                    entry.b = value;
                } else if constexpr (std::is_same_v<T, int>) {
                    entry.kind = detail::CvarEntry::Kind::Int;
                    entry.i = value;
                } else if constexpr (std::is_same_v<T, float>) {
                    entry.kind = detail::CvarEntry::Kind::Float;
                    entry.f = value;
                } else {
                    entry.kind = detail::CvarEntry::Kind::String;
                    entry.s = std::move(value);
                }
                return Cvar<T>(&entry);
            }

            bool has(std::string_view name) const;

            //! Parses `text` as the type of the variable and sets it
            Result set(std::string_view name, std::string_view text);

            //! The value as text ("" and false for a variable that is not there)
            std::string get(std::string_view name) const;

            //! "name = value  (help)" for each variable whose name contains `filter`, by name
            std::vector<std::string> list(std::string_view filter = {}) const;

            //! The names that start with `prefix`, by name (for completion)
            std::vector<std::string> names(std::string_view prefix = {}) const;

            void setCheats(bool on) noexcept { m_cheats = on; }
            bool cheats(void) const noexcept { return m_cheats; }
            void setSide(Side side) noexcept { m_side = side; }

            //! Writes the Persistent variables as "cvar.<name>"
            void save(ConfigFile& config) const;

            //! Reads them back; a value that cannot be used is left out, and said in the result
            std::vector<std::string> load(const ConfigFile& config);

        private:
            detail::CvarEntry& make(std::string_view name, CvarFlag flags, std::string_view help);
            const detail::CvarEntry* find(std::string_view name) const;
            static std::string show(const detail::CvarEntry& entry);

            std::map<std::string, std::unique_ptr<detail::CvarEntry>, std::less<>> m_entries;   //!< Stable: a Cvar points into it
            bool m_cheats = false;
            Side m_side = Side::Both;
    };

}
