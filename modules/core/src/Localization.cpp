#include "Localization.hpp"
#include "Json.hpp"
#include "Logger.hpp"
#include <algorithm>
#include <format>

namespace kuge
{
    namespace
    {
        constexpr const char* FORMS[] = {"zero", "one", "two", "few", "many", "other"};

        bool between(std::int64_t value, std::int64_t low, std::int64_t high) { return value >= low && value <= high; }

        // "{0}" and "{12}" in a text, as the numbers they hold
        std::set<std::size_t> placeholders(const std::string& text)
        {
            std::set<std::size_t> found;

            for (std::size_t at = text.find('{'); at != std::string::npos; at = text.find('{', at + 1)) {
                const std::size_t end = text.find('}', at);

                if (end != std::string::npos && end > at + 1 && std::all_of(text.begin() + at + 1, text.begin() + end, [](char c) { return c >= '0' && c <= '9'; })) {
                    found.insert(std::stoul(text.substr(at + 1, end - at - 1)));
                }
            }
            return found;
        }

        // A text with {0}, {1}... replaced by the arguments (a {n} with no argument stays as it is)
        std::string substitute(const std::string& source, const std::vector<std::string>& args)
        {
            std::string out;

            for (std::size_t at = 0; at < source.size(); ++at) {
                const std::size_t end = source[at] == '{' ? source.find('}', at) : std::string::npos;

                if (end != std::string::npos && end > at + 1 && std::all_of(source.begin() + at + 1, source.begin() + end, [](char c) { return c >= '0' && c <= '9'; })) {
                    const std::size_t index = std::stoul(source.substr(at + 1, end - at - 1));

                    if (index < args.size()) {
                        out += args[index];
                        at = end;
                        continue;
                    }
                }
                out += source[at];
            }
            return out;
        }

        // "coins.one" -> "coins" (a key that is a plural form), else the key
        std::string baseOf(const std::string& key)
        {
            const std::size_t dot = key.rfind('.');

            if (dot != std::string::npos) {
                const std::string last = key.substr(dot + 1);

                if (std::any_of(std::begin(FORMS), std::end(FORMS), [&](const char* form) { return last == form; })) {
                    return key.substr(0, dot);
                }
            }
            return key;
        }
    }

    PluralForm pluralForm(std::string_view language, std::int64_t count) noexcept
    {
        const std::string_view code = language.substr(0, language.find_first_of("-_"));
        const std::int64_t n = count < 0 ? -count : count;
        const std::int64_t last = n % 10, lastTwo = n % 100;

        if (code == "ja" || code == "zh" || code == "ko" || code == "vi" || code == "th" || code == "id") {
            return PluralForm::Other;
        }
        if (code == "fr" || code == "pt") {
            return n <= 1 ? PluralForm::One : PluralForm::Other;   // (French counts 0 as one)
        }
        if (code == "ru" || code == "uk") {
            if (last == 1 && lastTwo != 11) {
                return PluralForm::One;
            }
            return between(last, 2, 4) && !between(lastTwo, 12, 14) ? PluralForm::Few : PluralForm::Many;
        }
        if (code == "pl") {
            if (n == 1) {
                return PluralForm::One;
            }
            return between(last, 2, 4) && !between(lastTwo, 12, 14) ? PluralForm::Few : PluralForm::Many;
        }
        if (code == "cs" || code == "sk") {
            return n == 1 ? PluralForm::One : between(n, 2, 4) ? PluralForm::Few : PluralForm::Other;
        }
        if (code == "ar") {
            return n == 0 ? PluralForm::Zero : n == 1 ? PluralForm::One : n == 2 ? PluralForm::Two : between(lastTwo, 3, 10) ? PluralForm::Few
                 : between(lastTwo, 11, 99) ? PluralForm::Many : PluralForm::Other;
        }
        return n == 1 ? PluralForm::One : PluralForm::Other;
    }

    const char* pluralFormName(PluralForm form) noexcept { return FORMS[static_cast<std::size_t>(form)]; }

    void Localization::add(const std::string& language, std::string_view json, const std::string& source)
    {
        const std::string where = source.empty() ? language : source;
        Json parsed;

        try {
            parsed = Json::parse(json);
        } catch (const JsonError& error) {
            throw JsonError(where + ": " + error.what());
        }
        if (!parsed.isObject()) {
            throw JsonError(where + ": a language is an object of texts");
        }
        Table table;

        for (const auto& [key, value] : parsed.members()) {
            if (!value.isString()) {
                throw JsonError(std::format("{}: '{}' is {}, not a text", where, key, Json::name(value.type())));
            }
            table[key] = value.string();
        }
        if (!m_tables.contains(language)) {
            m_order.push_back(language);
        }
        m_tables[language] = std::move(table);
    }

    void Localization::load(const std::filesystem::path& folder)
    {
        std::vector<std::filesystem::path> files;

        for (const auto& entry : std::filesystem::directory_iterator(folder)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());   // (the same order on every machine)
        for (const auto& file : files) {
            add(file.stem().string(), Json::load(file).dump(), file.string());
        }
    }

    bool Localization::setLanguage(const std::string& language)
    {
        if (!m_tables.contains(language)) {
            return false;
        }
        m_current = language;
        return true;
    }

    std::vector<std::string> Localization::languages(void) const { return m_order; }

    std::string Localization::languageName(const std::string& language) const
    {
        const std::string* name = find(language, "language.name");

        return name ? *name : language;
    }

    const std::string* Localization::find(const std::string& language, const std::string& key) const
    {
        const auto table = m_tables.find(language);

        if (table == m_tables.end()) {
            return nullptr;
        }
        const auto text = table->second.find(key);

        return text == table->second.end() ? nullptr : &text->second;
    }

    const std::string& Localization::missing(const std::string& key) const
    {
        if (m_said.insert(key).second) {
            Logger::logger().category("localization").warn("no text '{}' in '{}' nor in '{}'", key, m_current, m_default);
        }
        m_key = key;
        return m_key;
    }

    const std::string& Localization::text(const std::string& key) const
    {
        for (const std::string* language : {&m_current, &m_default}) {
            if (const std::string* found = find(*language, key)) {
                return *found;
            }
        }
        return missing(key);
    }

    std::string Localization::format(const std::string& key, const std::vector<std::string>& args) const { return substitute(text(key), args); }

    std::string Localization::plural(const std::string& key, std::int64_t count, const std::vector<std::string>& args) const
    {
        std::vector<std::string> all{std::to_string(count)};

        all.insert(all.end(), args.begin(), args.end());
        for (const std::string* language : {&m_current, &m_default}) {
            for (const std::string& form : {std::string(pluralFormName(pluralForm(*language, count))), std::string("other")}) {
                if (const std::string* found = find(*language, key + "." + form)) {
                    return substitute(*found, all);
                }
            }
        }
        return missing(key);
    }

    std::vector<std::string> Localization::problems(void) const
    {
        std::vector<std::string> found;
        std::set<std::string> keys;

        for (const auto& [language, table] : m_tables) {
            for (const auto& entry : table) {
                keys.insert(baseOf(entry.first));
            }
        }
        for (const auto& [language, table] : m_tables) {
            for (const std::string& key : keys) {
                const bool plain = table.contains(key);
                const bool forms = table.contains(key + ".other");

                if (!plain && !forms) {
                    found.push_back(std::format("{}: lacks '{}'", language, key));
                }
            }
            for (const auto& [key, value] : table) {
                if (baseOf(key) != key && !table.contains(baseOf(key) + ".other")) {
                    found.push_back(std::format("{}: '{}' has no form 'other'", language, baseOf(key)));
                    break;
                }
            }
        }
        const auto reference = m_tables.find(m_default);

        for (const auto& [language, table] : m_tables) {
            for (const auto& [key, value] : table) {
                const std::string* other = reference == m_tables.end() || language == m_default ? nullptr : find(m_default, key);

                if (other && placeholders(*other) != placeholders(value)) {
                    found.push_back(std::format("{}: '{}' does not have the same {{n}} as in {}", language, key, m_default));
                }
            }
        }
        return found;
    }

}
