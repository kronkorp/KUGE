#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace kuge
{

    //! The plural form that a language uses for a number (CLDR): `plural()` reads the text `key.<form>`
    enum class PluralForm : std::uint8_t { Zero, One, Two, Few, Many, Other };

    //! The form of `count` in a language ("en", "fr", "pt-BR", "ru", ...). A language that is not known has One (1) and Other.
    PluralForm pluralForm(std::string_view language, std::int64_t count) noexcept;
    const char* pluralFormName(PluralForm form) noexcept;

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The texts of a game by language: a current language, `{0}` where a value goes, plural forms, the text that is wanted when one is missing
     *
     *     kuge::Localization texts;
     *     texts.load("data/lang");                       // en.json, fr.json: a flat object of texts, the file name is the language
     *     texts.setLanguage("fr");
     *     texts.text("menu.play");                       // "Jouer"
     *     texts.format("hello", {"Ana"});                // "Bonjour Ana"       ("hello": "Bonjour {0}")
     *     texts.plural("coins", 3);                      // "3 pièces"          ("coins.one", "coins.other": "{0} pièces")
     *     for (const auto& problem : texts.problems()) { log(problem); }
     *
     * A text that the language lacks is the one of the default language ("en"), and if that lacks it too, the key (it is said once in the
     * log). The name of a language in its own words is its text `language.name` (the code if it has none). One thread (the game's).
     */
    ////////////////////////////////////////////////////////////////////////////
    class Localization
    {
        public:
            //! Reads every `<code>.json` of a folder. @throw JsonError for a file that is not JSON or has anything but texts (the message names it)
            void load(const std::filesystem::path& folder);
            //! Reads one language from the text of a file. @throw JsonError (names `source`)
            void add(const std::string& language, std::string_view json, const std::string& source = "");

            void setDefaultLanguage(const std::string& language) { m_default = language; }
            //! False, and nothing changes, if there is no such language
            bool setLanguage(const std::string& language);
            const std::string& language(void) const noexcept { return m_current; }
            //! The codes that were loaded, in order
            std::vector<std::string> languages(void) const;
            //! The name of a language in its own words, for a menu
            std::string languageName(const std::string& language) const;

            //! The text of a key
            const std::string& text(const std::string& key) const;
            //! The text with {0}, {1}... replaced by `args` (a {n} with no argument stays as it is)
            std::string format(const std::string& key, const std::vector<std::string>& args) const;
            //! The text `key.<form>` for the count (else `key.other`), with {0} = the count and {1}... = `args`
            std::string plural(const std::string& key, std::int64_t count, const std::vector<std::string>& args = {}) const;

            //! What differs between languages: a key one lacks, a `{n}` that one has and another has not, a plural with no `.other`
            std::vector<std::string> problems(void) const;

        private:
            using Table = std::map<std::string, std::string>;

            const std::string* find(const std::string& language, const std::string& key) const;
            const std::string& missing(const std::string& key) const;

            std::map<std::string, Table>  m_tables;
            std::vector<std::string>      m_order;
            std::string                   m_default = "en";
            std::string                   m_current = "en";
            mutable std::set<std::string> m_said;
            mutable std::string           m_key;   // what text() answers when nothing has the key
    };

}
