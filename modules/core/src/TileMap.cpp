#include "TileMap.hpp"
#include "Serializer.hpp"
#include <algorithm>
#include <charconv>
#include <format>
#include <fstream>
#include <sstream>

namespace
{
    constexpr std::string_view SPACES = " \t\r";

    std::string_view trim(std::string_view text)
    {
        const auto begin = text.find_first_not_of(SPACES);

        if (begin == std::string_view::npos) {
            return {};
        }
        return text.substr(begin, text.find_last_not_of(SPACES) - begin + 1);
    }

    // The words of a line, cut at spaces and commas
    std::vector<std::string_view> words(std::string_view line)
    {
        std::vector<std::string_view> found;

        while (true) {
            const auto begin = line.find_first_not_of(" \t\r,");

            if (begin == std::string_view::npos) {
                return found;
            }
            line.remove_prefix(begin);
            const auto end = line.find_first_of(" \t\r,");

            found.push_back(line.substr(0, end));
            if (end == std::string_view::npos) {
                return found;
            }
            line.remove_prefix(end);
        }
    }

    template<typename T>
    bool toNumber(std::string_view word, T& value)
    {
        const auto result = std::from_chars(word.data(), word.data() + word.size(), value);

        return result.ec == std::errc() && result.ptr == word.data() + word.size();
    }
}

kuge::TileMap::TileMap(int width, int height, float tileSize)
    : m_width(width), m_height(height), m_tileSize(tileSize)
{
    if (width <= 0 || height <= 0 || !(tileSize > 0.0f)) {
        throw TileMapError(std::format("a tilemap needs a positive size ({} x {}, tiles of {})", width, height, tileSize));
    }
}

kuge::TileLayer& kuge::TileMap::addLayer(std::string name)
{
    if (name.empty() || name.find_first_of(" \t\r\n#") != std::string::npos) {
        throw TileMapError(std::format("invalid layer name '{}'", name));
    }
    if (findLayer(name)) {
        throw TileMapError(std::format("there is already a layer '{}'", name));
    }
    TileLayer layer;

    layer.name = std::move(name);
    layer.tiles.assign(static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height), EMPTY_TILE);
    m_layers.push_back(std::move(layer));
    return m_layers.back();
}

kuge::TileLayer* kuge::TileMap::findLayer(std::string_view name) noexcept
{
    const auto found = std::find_if(m_layers.begin(), m_layers.end(), [&](const TileLayer& l) { return l.name == name; });

    return found == m_layers.end() ? nullptr : &*found;
}

const kuge::TileLayer* kuge::TileMap::findLayer(std::string_view name) const noexcept
{
    const auto found = std::find_if(m_layers.begin(), m_layers.end(), [&](const TileLayer& l) { return l.name == name; });

    return found == m_layers.end() ? nullptr : &*found;
}

void kuge::TileMap::setSolid(TileId tile, bool solid)
{
    if (solid && tile != EMPTY_TILE) {
        m_solid.insert(tile);
    } else {
        m_solid.erase(tile);
    }
}

std::string kuge::TileMap::toString(void) const
{
    std::string text = std::format("kuge-tilemap 1\nsize {} {}\ntilesize {}\n", m_width, m_height, m_tileSize);

    if (!m_solid.empty()) {
        text += "solid";
        for (TileId tile : m_solid) {
            text += std::format(" {}", tile);
        }
        text += '\n';
    }
    for (const TileLayer& layer : m_layers) {
        text += std::format("layer {}{}\n", layer.name, layer.visible ? "" : " hidden");
        for (int y = 0; y < m_height; ++y) {
            for (int x = 0; x < m_width; ++x) {
                text += std::format("{}{}", x == 0 ? "" : " ", layer.tiles[index(x, y)]);
            }
            text += '\n';
        }
    }
    return text;
}

kuge::TileMap kuge::TileMap::parse(std::string_view text)
{
    TileMap map;
    std::size_t lineNumber = 0;
    bool sawHeader = false;
    TileLayer* layer = nullptr;   // the layer being filled
    int row = 0;                  // rows of it read so far

    auto fail = [&lineNumber](const std::string& what) {
        throw TileMapError(std::format("line {}: {}", lineNumber, what));
    };

    while (!text.empty()) {
        const auto newline = text.find('\n');
        std::string_view line = text.substr(0, newline);

        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
        ++lineNumber;
        if (const auto comment = line.find('#'); comment != std::string_view::npos) {
            line = line.substr(0, comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        const auto parts = words(line);

        // Rows of tiles come after "layer"
        if (layer && row < map.m_height) {
            if (parts[0] == "layer") {
                fail(std::format("the layer '{}' has only {} rows out of {}", layer->name, row, map.m_height));
            }
            if (static_cast<int>(parts.size()) != map.m_width) {
                fail(std::format("a row has {} tiles, the map is {} wide", parts.size(), map.m_width));
            }
            for (int x = 0; x < map.m_width; ++x) {
                unsigned long value = 0;

                if (!toNumber(parts[x], value) || value > 65535) {
                    fail(std::format("'{}' is not a tile (0 to 65535)", parts[x]));
                }
                layer->tiles[map.index(x, row)] = static_cast<TileId>(value);
            }
            ++row;
            continue;
        }
        const std::string_view keyword = parts[0];

        if (!sawHeader) {
            if (keyword != "kuge-tilemap" || parts.size() != 2 || parts[1] != "1") {
                fail("a tilemap starts with 'kuge-tilemap 1'");
            }
            sawHeader = true;
        } else if (keyword == "size") {
            int width = 0;
            int height = 0;

            if (map.m_width != 0) {
                fail("the size is given twice");
            }
            if (parts.size() != 3 || !toNumber(parts[1], width) || !toNumber(parts[2], height) || width <= 0 || height <= 0) {
                fail("'size' wants a width and a height in tiles, both above 0");
            }
            map.m_width = width;
            map.m_height = height;
        } else if (keyword == "tilesize") {
            float size = 0.0f;

            if (parts.size() != 2 || !toNumber(parts[1], size) || !(size > 0.0f)) {
                fail("'tilesize' wants a size in pixels, above 0");
            }
            map.m_tileSize = size;
        } else if (keyword == "solid") {
            for (std::size_t i = 1; i < parts.size(); ++i) {
                unsigned long value = 0;

                if (!toNumber(parts[i], value) || value == 0 || value > 65535) {
                    fail(std::format("'{}' is not a tile that can be solid (1 to 65535)", parts[i]));
                }
                map.m_solid.insert(static_cast<TileId>(value));
            }
        } else if (keyword == "layer") {
            if (map.m_width == 0) {
                fail("'size' must come before the first layer");
            }
            if (parts.size() < 2 || parts.size() > 3 || (parts.size() == 3 && parts[2] != "hidden")) {
                fail("'layer' wants a name, and maybe 'hidden'");
            }
            try {
                layer = &map.addLayer(std::string(parts[1]));
            } catch (const TileMapError& error) {
                fail(error.what());
            }
            layer->visible = parts.size() == 2;
            row = 0;
        } else {
            fail(std::format("unknown '{}'", keyword));
        }
    }
    if (!sawHeader) {
        throw TileMapError("empty: a tilemap starts with 'kuge-tilemap 1'");
    }
    if (map.m_width == 0) {
        throw TileMapError("the tilemap has no 'size'");
    }
    if (layer && row < map.m_height) {
        throw TileMapError(std::format("the layer '{}' stops after {} rows out of {}", layer->name, row, map.m_height));
    }
    return map;
}

kuge::TileMap kuge::TileMap::load(const std::filesystem::path& path)
{
    std::ifstream in(path);
    std::stringstream content;

    if (!in) {
        throw TileMapError(std::format("cannot open '{}'", path.string()));
    }
    content << in.rdbuf();
    try {
        return parse(content.str());
    } catch (const TileMapError& error) {
        throw TileMapError(std::format("'{}': {}", path.string(), error.what()));
    }
}

void kuge::TileMap::save(const std::filesystem::path& path) const
{
    const std::string text = toString();

    try {
        writeFile(path, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
    } catch (const SerializerError& error) {
        throw TileMapError(error.what());
    }
}
