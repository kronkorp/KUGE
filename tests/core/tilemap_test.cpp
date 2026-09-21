extern "C" {
    #include "kronklab/kronklab.h"
}
#include "TileMap.hpp"
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    const char* SMALL = R"(# a level
kuge-tilemap 1
size 4 3
tilesize 16
solid 1 2

layer ground          # the walls and the floor
1 1 1 1
0 0 0 0
2, 2, 0, 0
layer sky hidden
0 0 0 0
0 5 0 0
0 0 0 65535
)";

    // The message of the TileMapError that parse throws, "" if it does not
    std::string errorOf(const std::string& text)
    {
        try {
            kuge::TileMap::parse(text);
        } catch (const kuge::TileMapError& error) {
            return error.what();
        }
        return {};
    }

    bool mentions(const std::string& message, const char* part)
    {
        return message.find(part) != std::string::npos;
    }

    std::filesystem::path tempPath(const char* name)
    {
        return std::filesystem::temp_directory_path() / ("kuge_test_" + std::to_string(::getpid()) + "_" + name);
    }
}

Test(tilemap, build_and_read)
{
    kuge::TileMap map(5, 3, 8.0f);
    auto& ground = map.addLayer("ground");
    auto& sky = map.addLayer("sky");

    AssertEq(map.width(), 5, "width");
    AssertEq(map.height(), 3, "height");
    AssertEq(map.tileSize(), 8.0f, "tile size");
    AssertEq(ground.tiles.size(), 15, "a cell per tile");
    map.setTile(ground, 2, 1, 7);
    map.setTile(sky, 4, 2, 9);
    AssertEq(map.tileAt(ground, 2, 1), 7, "what was put");
    AssertEq(map.tileAt(sky, 4, 2), 9, "in its own layer");
    AssertEq(map.tileAt(ground, 4, 2), kuge::EMPTY_TILE, "and not in the other");
    AssertEq(map.tileAt(ground, -1, 0), kuge::EMPTY_TILE, "outside is empty");
    AssertEq(map.tileAt(ground, 5, 0), kuge::EMPTY_TILE, "on every side");
    map.setTile(ground, 99, 99, 3);   // ignored
    Assert(map.inside(4, 2) && !map.inside(5, 2) && !map.inside(0, -1), "inside");
    AssertEq(map.layers().size(), 2, "two layers");
    Assert(map.findLayer("sky") == &map.layers()[1] && map.findLayer("nothing") == nullptr, "found by name");
    Assert(map.layers()[0].visible, "visible unless said otherwise");
}

Test(tilemap, layer_names_and_size)
{
    bool duplicate = false, empty = false, spaced = false, badSize = false, badTile = false;
    kuge::TileMap map(2, 2, 16.0f);

    map.addLayer("a");
    try { map.addLayer("a"); } catch (const kuge::TileMapError&) { duplicate = true; }
    try { map.addLayer(""); } catch (const kuge::TileMapError&) { empty = true; }
    try { map.addLayer("two words"); } catch (const kuge::TileMapError&) { spaced = true; }
    try { kuge::TileMap bad(0, 5, 16.0f); } catch (const kuge::TileMapError&) { badSize = true; }
    try { kuge::TileMap bad(5, 5, 0.0f); } catch (const kuge::TileMapError&) { badTile = true; }
    Assert(duplicate, "a name is used once");
    Assert(empty, "and is not empty");
    Assert(spaced, "and has no spaces: it is written in a file");
    Assert(badSize, "a map has a size");
    Assert(badTile, "and its tiles too");
    AssertEq(map.layers().size(), 1, "the refused ones were not added");
}

Test(tilemap, solid_tiles)
{
    kuge::TileMap map(2, 2, 16.0f);

    map.setSolid(3);
    map.setSolid(5);
    Assert(map.isSolid(3) && map.isSolid(5) && !map.isSolid(4), "solid tiles");
    Assert(!map.isSolid(kuge::EMPTY_TILE), "nothing is never solid");
    map.setSolid(kuge::EMPTY_TILE);
    Assert(!map.isSolid(kuge::EMPTY_TILE), "even if asked");
    map.setSolid(3, false);
    Assert(!map.isSolid(3), "and it can be undone");
    AssertEq(map.solidTiles().size(), 1, "one left");
}

Test(tilemap, read_a_file)
{
    const auto map = kuge::TileMap::parse(SMALL);

    AssertEq(map.width(), 4, "width");
    AssertEq(map.height(), 3, "height");
    AssertEq(map.tileSize(), 16.0f, "tile size");
    AssertEq(map.layers().size(), 2, "two layers");
    Assert(map.isSolid(1) && map.isSolid(2) && !map.isSolid(5), "solid tiles");
    const auto& ground = *map.findLayer("ground");
    const auto& sky = *map.findLayer("sky");

    Assert(ground.visible && !sky.visible, "the second is hidden");
    AssertEq(map.tileAt(ground, 0, 0), 1, "first row");
    AssertEq(map.tileAt(ground, 3, 0), 1, "first row, last");
    AssertEq(map.tileAt(ground, 0, 2), 2, "commas are fine too");
    AssertEq(map.tileAt(ground, 2, 2), 0, "empty");
    AssertEq(map.tileAt(sky, 1, 1), 5, "in the other layer");
    AssertEq(map.tileAt(sky, 3, 2), 65535, "the biggest tile");
}

Test(tilemap, write_and_read_back)
{
    const auto map = kuge::TileMap::parse(SMALL);
    const auto again = kuge::TileMap::parse(map.toString());

    AssertStrEq(again.toString().c_str(), map.toString().c_str(), "the same text after a round trip");
    AssertEq(again.layers().size(), 2, "layers");
    Assert(again.layers()[0].tiles == map.layers()[0].tiles && again.layers()[1].tiles == map.layers()[1].tiles, "tiles");
    Assert(!again.layers()[1].visible, "hidden stays hidden");
    Assert(again.solidTiles() == map.solidTiles(), "solid tiles");
    AssertEq(again.tileSize(), 16.0f, "tile size");

    // A big one, made by code
    kuge::TileMap big(300, 200, 12.5f);
    auto& layer = big.addLayer("all");
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 300; ++x) {
            big.setTile(layer, x, y, static_cast<kuge::TileId>((x * 7 + y * 13) % 1000));
        }
    }
    const auto backBig = kuge::TileMap::parse(big.toString());
    Assert(backBig.layers()[0].tiles == layer.tiles && backBig.tileSize() == 12.5f, "and so is a big one");
}

Test(tilemap, mistakes_name_the_line)
{
    Assert(mentions(errorOf(""), "kuge-tilemap"), "empty");
    Assert(mentions(errorOf("hello\n"), "line 1") && mentions(errorOf("hello\n"), "kuge-tilemap 1"), "no header");
    Assert(mentions(errorOf("kuge-tilemap 2\n"), "line 1"), "another version");
    Assert(mentions(errorOf("kuge-tilemap 1\nsize 2 2\nfoo bar\n"), "line 3"), "an unknown line");
    Assert(mentions(errorOf("kuge-tilemap 1\nsize 2 2\nsize 3 3\n"), "line 3"), "the size twice");
    Assert(mentions(errorOf("kuge-tilemap 1\nsize 0 2\n"), "line 2"), "a zero size");
    Assert(mentions(errorOf("kuge-tilemap 1\nsize 2\n"), "line 2"), "a missing height");
    Assert(mentions(errorOf("kuge-tilemap 1\nlayer a\n"), "'size' must come before"), "a layer before the size");
    Assert(mentions(errorOf("kuge-tilemap 1\nsize 2 2\ntilesize 0\n"), "line 3"), "a zero tile size");
    Assert(mentions(errorOf("kuge-tilemap 1\nsize 2 2\nsolid 0\n"), "line 3"), "0 cannot be solid");
    Assert(mentions(errorOf("kuge-tilemap 1\nsize 2 2\nlayer a shown\n"), "line 3"), "an unknown flag");
    Assert(mentions(errorOf("kuge-tilemap 1\nsize 2 2\nlayer\n"), "line 3"), "a layer with no name");
}

Test(tilemap, bad_rows)
{
    const std::string head = "kuge-tilemap 1\nsize 3 2\nlayer a\n";

    Assert(mentions(errorOf(head + "1 2\n1 2 3\n"), "line 4"), "a row too short");
    Assert(mentions(errorOf(head + "1 2 3\n1 2 3 4\n"), "line 5"), "a row too long");
    Assert(mentions(errorOf(head + "1 x 3\n1 2 3\n"), "'x'"), "a word that is not a number");
    Assert(mentions(errorOf(head + "1 -1 3\n1 2 3\n"), "'-1'"), "negative");
    Assert(mentions(errorOf(head + "1 65536 3\n1 2 3\n"), "'65536'"), "too big");
    Assert(mentions(errorOf(head + "1 2 3\n"), "1 rows out of 2") || mentions(errorOf(head + "1 2 3\n"), "after 1 rows"), "a row missing at the end");
    Assert(mentions(errorOf(head + "1 2 3\nlayer b\n1 2 3\n1 2 3\n"), "layer 'a' has only 1 rows"), "an unfinished layer, then another");
    Assert(mentions(errorOf(head + "1 2 3\n1 2 3\nlayer a\n1 2 3\n1 2 3\n"), "already a layer 'a'"), "a name used twice");
    AssertEq(errorOf(head + "1 2 3\n1 2 3\n").empty(), true, "and the right one is accepted");
}

Test(tilemap, files)
{
    const auto path = tempPath("level.tilemap");
    kuge::TileMap map(3, 2, 32.0f);
    bool missing = false;
    std::string message;

    map.addLayer("a");
    map.setSolid(4);
    map.save(path);
    AssertEq(std::filesystem::exists(path.string() + ".tmp"), false, "no temporary file left");
    const auto back = kuge::TileMap::load(path);
    AssertEq(back.tileSize(), 32.0f, "loaded");
    Assert(back.isSolid(4), "with its solid tiles");
    {
        std::ofstream broken(path);
        broken << "kuge-tilemap 1\nnonsense\n";
    }
    try { kuge::TileMap::load(path); } catch (const kuge::TileMapError& error) { message = error.what(); }
    Assert(mentions(message, "level.tilemap") && mentions(message, "line 2"), "an error names the file and the line: '%s'", message.c_str());
    std::filesystem::remove(path);
    try { kuge::TileMap::load(path); } catch (const kuge::TileMapError&) { missing = true; }
    Assert(missing, "a missing file");
}
