#include "AssetRoots.hpp"
#include <algorithm>
#include <format>
#include <map>

void kuge::AssetRoots::add(std::filesystem::path path, int priority, std::string name)
{
    if (name.empty()) {
        name = path.string();
    }
    Root root{std::move(path), priority, std::move(name)};
    // Before the first root whose priority is not higher: the last added wins a tie
    const auto at = std::find_if(m_roots.begin(), m_roots.end(), [priority](const Root& other) { return other.priority <= priority; });

    m_roots.insert(at, std::move(root));
}

std::optional<std::filesystem::path> kuge::AssetRoots::find(const std::filesystem::path& relative) const
{
    std::error_code ignored;

    if (relative.is_absolute()) {
        return std::filesystem::exists(relative, ignored) ? std::optional(relative) : std::nullopt;
    }
    for (const Root& root : m_roots) {
        const std::filesystem::path candidate = root.path / relative;

        if (std::filesystem::is_regular_file(candidate, ignored)) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::filesystem::path kuge::AssetRoots::resolve(const std::filesystem::path& relative) const
{
    if (const auto found = find(relative)) {
        return *found;
    }
    std::string searched;

    for (const Root& root : m_roots) {
        searched += (searched.empty() ? "" : ", ") + std::format("{} ({})", root.path.string(), root.name);
    }
    throw AssetNotFound(std::format("'{}' is in none of the folders searched: {}", relative.string(), searched.empty() ? "(no folder)" : searched));
}

std::string kuge::AssetRoots::whoHas(const std::filesystem::path& relative) const
{
    std::error_code ignored;

    for (const Root& root : m_roots) {
        if (std::filesystem::is_regular_file(root.path / relative, ignored)) {
            return root.name;
        }
    }
    return {};
}

std::vector<std::filesystem::path> kuge::AssetRoots::list(const std::filesystem::path& folder) const
{
    std::map<std::string, std::filesystem::path> files;   // by name: the first root to give a name keeps it
    std::error_code ignored;

    for (const Root& root : m_roots) {
        const std::filesystem::path where = root.path / folder;

        if (!std::filesystem::is_directory(where, ignored)) {
            continue;
        }
        for (const auto& entry : std::filesystem::recursive_directory_iterator(where, ignored)) {
            if (entry.is_regular_file(ignored)) {
                files.emplace((folder / std::filesystem::relative(entry.path(), where)).generic_string(), entry.path());
            }
        }
    }
    std::vector<std::filesystem::path> result;

    for (auto& [name, path] : files) {
        result.push_back(std::move(path));
    }
    return result;
}
