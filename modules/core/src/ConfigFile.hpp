#pragma once

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace kuge
{

    //! A config file that cannot be parsed, or a key or value that cannot be
    //! written in one
    class ConfigError : public std::runtime_error
    {
        public:
            using std::runtime_error::runtime_error;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Settings as text: what a player or a designer edits by hand
     *
     *     # a comment (a line starting with # or ;)
     *     fullscreen = true
     *     [audio]
     *     volume = 0.8        -> the key is "audio.volume"
     *
     * A line is "key = value" (split at the first "="), or "[section]", which
     * prefixes the keys that follow. Spaces around keys and values are ignored;
     * a value is the rest of the line, so there are no comments after a value.
     *
     * Reading never fails: a missing or unreadable value gives the default you
     * pass. Keys are kept in alphabetical order, so a saved file is always the
     * same for the same content.
     */
    ////////////////////////////////////////////////////////////////////////////
    class ConfigFile
    {
        public:
            using Entries = std::map<std::string, std::string, std::less<>>;

            //! Adds the settings of text (a key that is already there is replaced)
            //! @throw ConfigError on a line that is not a comment, a section or
            //!        "key = value"; nothing is added then
            void parse(std::string_view text);

            //! @return  false if the file does not exist or cannot be opened
            //!          (normal on the first launch); the settings are left as they were
            //! @throw   ConfigError if the file is not valid (nothing is added then)
            bool load(const std::filesystem::path& path);

            //! Replaces the file whole or not at all
            //! @throw ConfigError
            void save(const std::filesystem::path& path) const;

            //! The file as text: one "key = value" per line
            std::string toString(void) const;

            bool                       has(std::string_view key) const;
            std::optional<std::string> get(std::string_view key) const;

            std::string getString(std::string_view key, std::string_view fallback = {}) const;
            long long   getInt(std::string_view key, long long fallback) const;
            double      getDouble(std::string_view key, double fallback) const;

            //! true / false, yes / no, on / off, 1 / 0, in any case
            bool        getBool(std::string_view key, bool fallback) const;

            //! @throw ConfigError if the key is empty or has spaces, "=" or
            //!        starts with "#", ";" or "[", or if the value has a line break
            void set(std::string_view key, std::string_view value);

            template<typename T>
                requires std::is_arithmetic_v<T>
            void set(std::string_view key, T value)
            {
                if constexpr (std::is_same_v<T, bool>) {
                    set(key, std::string_view(value ? "true" : "false"));
                } else {
                    char buffer[64];
                    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);

                    set(key, std::string_view(buffer, static_cast<std::size_t>(result.ptr - buffer)));
                }
            }

            //! @return  true if the key was there
            bool remove(std::string_view key);

            const Entries& entries(void) const noexcept { return m_entries; }

        private:
            Entries m_entries;
    };

}
