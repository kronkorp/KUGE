#include "Mods.hpp"
#include "Json.hpp"
#include <algorithm>
#include <fstream>
#include <format>
#include <map>
#include <set>
#include <sstream>

namespace
{
    constexpr std::uint64_t FNV_BASIS = 1469598103934665603ull;
    constexpr std::uint64_t FNV_PRIME = 1099511628211ull;

    void mix(std::uint64_t& hash, const void* data, std::size_t size)
    {
        const auto* bytes = static_cast<const unsigned char*>(data);

        for (std::size_t i = 0; i < size; ++i) {
            hash = (hash ^ bytes[i]) * FNV_PRIME;
        }
    }

    void mixText(std::uint64_t& hash, const std::string& text)
    {
        const std::uint64_t size = text.size();   // (so that "ab","c" is not "a","bc")

        mix(hash, &size, sizeof(size));
        mix(hash, text.data(), text.size());
    }

    // The files of a mod, relative, with "/", sorted; mod.json is not data of the game
    std::vector<std::pair<std::string, std::filesystem::path>> filesOf(const std::filesystem::path& folder, bool withManifest)
    {
        std::vector<std::pair<std::string, std::filesystem::path>> files;
        std::error_code ignored;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(folder, ignored)) {
            if (!entry.is_regular_file(ignored)) {
                continue;
            }
            const std::string relative = std::filesystem::relative(entry.path(), folder).generic_string();

            if (withManifest || relative != "mod.json") {
                files.emplace_back(relative, entry.path());
            }
        }
        std::sort(files.begin(), files.end());
        return files;
    }
}

kuge::Mods kuge::Mods::scan(const std::filesystem::path& folder, const std::string& kugeVersion)
{
    Mods mods;
    std::map<std::string, ModInfo> byName;
    std::error_code ignored;

    if (!std::filesystem::is_directory(folder, ignored)) {
        return mods;
    }
    for (const auto& entry : std::filesystem::directory_iterator(folder, ignored)) {
        const std::filesystem::path manifest = entry.path() / "mod.json";

        if (!entry.is_directory(ignored) || !std::filesystem::is_regular_file(manifest, ignored)) {
            continue;
        }
        ModInfo info;

        info.folder = entry.path();
        try {
            const Json json = Json::load(manifest);

            info.name = json["name"].string();
            info.version = json.find("version") ? json["version"].string() : "";
            info.kuge = json.find("kuge") ? json["kuge"].string() : "";
            if (const Json* depends = json.find("depends")) {
                for (const Json& one : depends->items()) {
                    info.depends.push_back(one.string());
                }
            }
        } catch (const JsonError& error) {
            throw ModError(std::format("the mod in '{}': {}", entry.path().filename().string(), error.what()));
        }
        if (info.name.empty()) {
            throw ModError(std::format("the mod in '{}' has no name", entry.path().filename().string()));
        }
        if (byName.count(info.name)) {
            throw ModError(std::format("there are two mods named '{}'", info.name));
        }
        if (!kugeVersion.empty() && !info.kuge.empty() && info.kuge != kugeVersion) {
            throw ModError(std::format("the mod '{}' was made for KUGE {}, this is {}", info.name, info.kuge, kugeVersion));
        }
        byName.emplace(info.name, std::move(info));
    }
    for (const auto& [name, info] : byName) {
        for (const std::string& dependency : info.depends) {
            if (!byName.count(dependency)) {
                throw ModError(std::format("the mod '{}' needs '{}', which is not there", name, dependency));
            }
        }
    }
    // Dependencies first, then by name: each time, the first (by name) that has nothing left to wait for
    std::set<std::string> placed;

    while (placed.size() < byName.size()) {
        bool progress = false;

        for (const auto& [name, info] : byName) {
            if (placed.count(name)) {
                continue;
            }
            if (std::all_of(info.depends.begin(), info.depends.end(), [&](const std::string& d) { return placed.count(d) != 0; })) {
                mods.m_mods.push_back(info);
                placed.insert(name);
                progress = true;
                break;
            }
        }
        if (!progress) {
            std::string cycle;

            for (const auto& [name, info] : byName) {
                if (!placed.count(name)) {
                    cycle += (cycle.empty() ? "" : ", ") + name;
                }
            }
            throw ModError(std::format("these mods wait for each other, so none can be first: {}", cycle));
        }
    }
    return mods;
}

void kuge::Mods::apply(AssetRoots& roots) const
{
    int priority = 1;

    for (const ModInfo& mod : m_mods) {
        roots.add(mod.folder, priority++, mod.name);
    }
}

std::vector<kuge::Mods::Override> kuge::Mods::overrides(const std::filesystem::path& gameRoot) const
{
    std::vector<Override> found;
    std::map<std::string, std::string> owner;   // file -> who has it now ("game" or a mod)
    std::error_code ignored;

    for (const ModInfo& mod : m_mods) {
        for (const auto& [relative, path] : filesOf(mod.folder, false)) {
            const auto had = owner.find(relative);

            if (had != owner.end()) {
                found.push_back({relative, mod.name, had->second});
            } else if (std::filesystem::is_regular_file(gameRoot / relative, ignored)) {
                found.push_back({relative, mod.name, "game"});
            }
            owner[relative] = mod.name;
        }
    }
    return found;
}

std::uint64_t kuge::Mods::fingerprint(void) const
{
    if (m_mods.empty()) {
        return 0;
    }
    std::uint64_t hash = FNV_BASIS;

    for (const ModInfo& mod : m_mods) {
        mixText(hash, mod.name);
        mixText(hash, mod.version);
        for (const auto& [relative, path] : filesOf(mod.folder, true)) {
            std::ifstream in(path, std::ios::binary);
            std::ostringstream bytes;

            bytes << in.rdbuf();
            mixText(hash, relative);
            mixText(hash, bytes.str());
        }
    }
    return hash == 0 ? 1 : hash;
}
