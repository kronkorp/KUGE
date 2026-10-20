#include "Cvars.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

namespace
{
    using Entry = kuge::detail::CvarEntry;

    bool validName(std::string_view name)
    {
        return !name.empty() && std::all_of(name.begin(), name.end(), [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.';
        });
    }

    std::string lower(std::string_view text)
    {
        std::string result(text);

        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return result;
    }
}

kuge::detail::CvarEntry& kuge::Cvars::make(std::string_view name, CvarFlag flags, std::string_view help)
{
    if (!validName(name)) {
        throw std::invalid_argument(std::format("'{}' is not a name for a variable (letters, digits, _ and .)", name));
    }
    if (m_entries.find(name) != m_entries.end()) {
        throw std::invalid_argument(std::format("the variable '{}' is already there", name));
    }
    auto entry = std::make_unique<Entry>();

    entry->name = std::string(name);
    entry->help = std::string(help);
    entry->flags = flags;
    return *m_entries.emplace(std::string(name), std::move(entry)).first->second;
}

const kuge::detail::CvarEntry* kuge::Cvars::find(std::string_view name) const
{
    const auto found = m_entries.find(name);

    return found == m_entries.end() ? nullptr : found->second.get();
}

bool kuge::Cvars::has(std::string_view name) const
{
    return find(name) != nullptr;
}

std::string kuge::Cvars::show(const Entry& entry)
{
    switch (entry.kind) {
        case Entry::Kind::Bool:   return entry.b.load() ? "true" : "false";
        case Entry::Kind::Int:    return std::to_string(entry.i.load());
        case Entry::Kind::Float:  return std::format("{}", entry.f.load());
        case Entry::Kind::String: {
            std::lock_guard lock(entry.lock);

            return entry.s;
        }
    }
    return {};
}

std::string kuge::Cvars::get(std::string_view name) const
{
    const Entry* entry = find(name);

    return entry ? show(*entry) : std::string();
}

kuge::Cvars::Result kuge::Cvars::set(std::string_view name, std::string_view text)
{
    const auto found = m_entries.find(name);

    if (found == m_entries.end()) {
        return {false, std::format("unknown variable '{}'", name)};
    }
    Entry& entry = *found->second;

    if (hasFlag(entry.flags, CvarFlag::Cheat) && !m_cheats) {
        return {false, std::format("'{}' is a cheat: turn cheats on first", name)};
    }
    if ((hasFlag(entry.flags, CvarFlag::ServerOnly) && m_side == Side::Client) || (hasFlag(entry.flags, CvarFlag::ClientOnly) && m_side == Side::Server)) {
        return {false, std::format("'{}' can only be changed on the {}", name, hasFlag(entry.flags, CvarFlag::ServerOnly) ? "server" : "client")};
    }
    switch (entry.kind) {
        case Entry::Kind::Bool: {
            const std::string word = lower(text);

            if (word == "1" || word == "true" || word == "on" || word == "yes") {
                entry.b = true;
            } else if (word == "0" || word == "false" || word == "off" || word == "no") {
                entry.b = false;
            } else {
                return {false, std::format("'{}' is not true or false for {}", text, name)};
            }
            break;
        }
        case Entry::Kind::Int: {
            int value = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);

            if (text.empty() || error != std::errc() || end != text.data() + text.size()) {
                return {false, std::format("'{}' is not a whole number for {}", text, name)};
            }
            entry.i = value;
            break;
        }
        case Entry::Kind::Float: {
            float value = 0.0f;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);

            if (text.empty() || error != std::errc() || end != text.data() + text.size() || !std::isfinite(value)) {
                return {false, std::format("'{}' is not a number for {}", text, name)};
            }
            entry.f = value;
            break;
        }
        case Entry::Kind::String: {
            std::lock_guard lock(entry.lock);

            entry.s = std::string(text);
            break;
        }
    }
    return {true, std::format("{} = {}", name, show(entry))};
}

std::vector<std::string> kuge::Cvars::list(std::string_view filter) const
{
    std::vector<std::string> lines;

    for (const auto& [name, entry] : m_entries) {
        if (filter.empty() || name.find(filter) != std::string::npos) {
            lines.push_back(std::format("{} = {}{}", name, show(*entry), entry->help.empty() ? std::string() : "  (" + entry->help + ")"));
        }
    }
    return lines;
}

std::vector<std::string> kuge::Cvars::names(std::string_view prefix) const
{
    std::vector<std::string> result;

    for (const auto& [name, entry] : m_entries) {
        if (name.compare(0, prefix.size(), prefix) == 0) {
            result.push_back(name);
        }
    }
    return result;
}

void kuge::Cvars::save(ConfigFile& config) const
{
    for (const auto& [name, entry] : m_entries) {
        if (hasFlag(entry->flags, CvarFlag::Persistent)) {
            config.set("cvar." + name, show(*entry));
        }
    }
}

std::vector<std::string> kuge::Cvars::load(const ConfigFile& config)
{
    std::vector<std::string> problems;
    const bool cheats = m_cheats;

    m_cheats = true;   // (what the player saved was allowed when it was saved)
    for (const auto& [name, entry] : m_entries) {
        if (hasFlag(entry->flags, CvarFlag::Persistent) && config.has("cvar." + name)) {
            const Side side = m_side;

            m_side = Side::Both;
            const Result result = set(name, config.getString("cvar." + name));
            m_side = side;
            if (!result.ok) {
                problems.push_back(result.message);
            }
        }
    }
    m_cheats = cheats;
    return problems;
}
