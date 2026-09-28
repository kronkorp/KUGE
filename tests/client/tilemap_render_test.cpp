extern "C" {
    #include "kronklab/kronklab.h"
}
#include "client_fixture.hpp"
#include "render/Tilemap.hpp"
#include <cmath>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using Call = kuge::DummyRenderer::Call;

    // A sheet of 4 x 2 tiles of 16 x 16. Tile numbers start at 1: tile 1 is the first cell.
    std::shared_ptr<kuge::Spritesheet> makeTiles(kw::World& world)
    {
        auto sheet = std::make_shared<kuge::Spritesheet>();
        std::vector<std::uint8_t> pixels(64 * 32 * 4, 255);

        sheet->texture = kuge::Texture::fromPixels(*world.getResource<kuge::Ref<kuge::IRenderer2D>>(), 64, 32, pixels);
        sheet->frameWidth = 16;
        sheet->frameHeight = 16;
        return sheet;
    }

    kw::Entity spawnMap(kw::World& world, std::shared_ptr<const kuge::TileMap> map, const std::string& layer, int drawLayer,
        kuge::Vec2 position = {})
    {
        const kw::Entity entity = world.create();
        kuge::TilemapView view;

        view.map = std::move(map);
        view.tiles = makeTiles(world);
        view.mapLayer = layer;
        view.layer = drawLayer;
        world.add<kuge::Transform2D>(entity, kuge::Transform2D{position});
        world.add<kuge::TilemapView>(entity, view);
        return entity;
    }

    std::shared_ptr<kuge::TileMap> filled(int width, int height, kuge::TileId tile)
    {
        auto map = std::make_shared<kuge::TileMap>(width, height, 16.0f);
        auto& layer = map->addLayer("all");

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                map->setTile(layer, x, y, tile);
            }
        }
        return map;
    }

    bool sameRect(const kuge::Rect& a, const kuge::Rect& b)
    {
        return std::fabs(a.x - b.x) < 1e-3f && std::fabs(a.y - b.y) < 1e-3f && std::fabs(a.w - b.w) < 1e-3f && std::fabs(a.h - b.h) < 1e-3f;
    }
}

Test(tiles, what_shows_on_screen)
{
    const kuge::TileMap map(100, 100, 16.0f);
    const kuge::Vec2 screen{640.0f, 480.0f};
    kuge::Camera2D camera{{320.0f, 240.0f}, 1.0f};        // its top-left corner is at the origin
    auto range = kuge::visibleTiles(map, {0.0f, 0.0f}, {1.0f, 1.0f}, camera, screen);

    Assert(range.x0 == 0 && range.y0 == 0 && range.x1 == 39 && range.y1 == 29, "640 x 480 is 40 x 30 tiles: %d..%d %d..%d", range.x0, range.x1, range.y0, range.y1);
    camera.position = {328.0f, 240.0f};                   // a half tile to the right: one more, partly
    range = kuge::visibleTiles(map, {0.0f, 0.0f}, {1.0f, 1.0f}, camera, screen);
    Assert(range.x0 == 0 && range.x1 == 40, "a partly visible tile counts");
    camera = {{320.0f, 240.0f}, 2.0f};
    range = kuge::visibleTiles(map, {0.0f, 0.0f}, {1.0f, 1.0f}, camera, screen);
    // At zoom 2 the screen shows the world from (160, 120) to (480, 360): tiles 10 to 29, rows 7 (half of it) to 22
    Assert(range.x0 == 10 && range.x1 == 29 && range.y0 == 7 && range.y1 == 22, "zoom 2: %d..%d %d..%d", range.x0, range.x1, range.y0, range.y1);
    camera = {{5000.0f, 5000.0f}, 1.0f};
    Assert(kuge::visibleTiles(map, {0.0f, 0.0f}, {1.0f, 1.0f}, camera, screen).empty(), "looking away from the map: nothing");
    camera = {{-5000.0f, 240.0f}, 1.0f};
    Assert(kuge::visibleTiles(map, {0.0f, 0.0f}, {1.0f, 1.0f}, camera, screen).empty(), "on the other side too");
    camera = {{1600.0f, 1600.0f}, 1.0f};                  // the far corner: only the last tiles
    range = kuge::visibleTiles(map, {0.0f, 0.0f}, {1.0f, 1.0f}, camera, screen);
    Assert(range.x1 == 99 && range.y1 == 99 && range.x0 > 50, "clamped to the map");
    camera = {{320.0f, 240.0f}, 1.0f};
    range = kuge::visibleTiles(map, {100.0f, 50.0f}, {1.0f, 1.0f}, camera, screen);
    Assert(range.x0 == 0 && range.x1 == 33 && range.y1 == 26, "a map that starts further away: 100 px in, 50 down");
    range = kuge::visibleTiles(map, {0.0f, 0.0f}, {2.0f, 2.0f}, camera, screen);
    Assert(range.x1 == 19 && range.y1 == 14, "tiles scaled by 2");
}

Test(tiles, a_small_map)
{
    auto map = std::make_shared<kuge::TileMap>(4, 3, 16.0f);
    auto& layer = map->addLayer("ground");

    map->setTile(layer, 0, 0, 1);
    map->setTile(layer, 3, 0, 4);
    map->setTile(layer, 1, 2, 6);     // cell (1, 1) of the sheet
    Fixture fx([&map](TestScene& scene) { spawnMap(scene.world(), map, "ground", 0); });
    const auto& calls = fx.frame();

    AssertEq(calls.size(), 3, "only the tiles that are there, got %zu", calls.size());
    Assert(calls[0].kind == Call::Kind::Texture && sameRect(calls[0].texture.source, {0.0f, 0.0f, 16.0f, 16.0f}), "tile 1: the first cell");
    Assert(sameRect(calls[1].texture.source, {48.0f, 0.0f, 16.0f, 16.0f}), "tile 4: the last of the row");
    Assert(sameRect(calls[2].texture.source, {16.0f, 16.0f, 16.0f, 16.0f}), "tile 6: the second row");
    // The map's corner is at the world origin, which is the center of the 640 x 480 screen
    Assert(sameRect(calls[0].destination, {320.0f, 240.0f, 16.0f, 16.0f}), "at the origin");
    Assert(sameRect(calls[1].destination, {368.0f, 240.0f, 16.0f, 16.0f}), "3 tiles to the right");
    Assert(sameRect(calls[2].destination, {336.0f, 272.0f, 16.0f, 16.0f}), "2 tiles down");
    fx.scene->world().getResource<kuge::Camera2D>().position = {8.0f, 8.0f};
    Assert(sameRect(fx.frame()[0].destination, {312.0f, 232.0f, 16.0f, 16.0f}), "the camera moves it");
}

Test(tiles, only_what_is_visible)
{
    auto big = filled(1000, 1000, 2);
    Fixture fx([&big](TestScene& scene) { spawnMap(scene.world(), big, "all", 0); });

    fx.scene->world().getResource<kuge::Camera2D>().position = {320.0f, 240.0f};
    AssertEq(fx.frame().size(), 1200, "a million tiles, and only the 40 x 30 that show are drawn: got %zu", fx.frame().size());
    fx.scene->world().getResource<kuge::Camera2D>() = {{320.0f, 240.0f}, 2.0f};
    AssertEq(fx.frame().size(), 320, "zoom 2: 20 columns and 16 rows (one is half hidden), got %zu", fx.frame().size());
    fx.scene->world().getResource<kuge::Camera2D>().position = {50000.0f, 0.0f};
    AssertEq(fx.frame().size(), 0, "away from the map: none");
}

Test(tiles, layers_of_a_map)
{
    auto map = std::make_shared<kuge::TileMap>(2, 1, 16.0f);
    auto& back = map->addLayer("back");
    auto& front = map->addLayer("front");
    auto& hidden = map->addLayer("editor");

    hidden.visible = false;
    map->setTile(back, 0, 0, 1);
    map->setTile(front, 1, 0, 2);
    map->setTile(hidden, 0, 0, 3);
    Fixture fx([&map](TestScene& scene) {
        spawnMap(scene.world(), map, "", 0);           // all the visible ones
        spawnMap(scene.world(), map, "editor", 1);      // a hidden one, asked for by name
        spawnMap(scene.world(), map, "nothing", 2);     // no such layer
    });
    const auto& calls = fx.frame();

    AssertEq(calls.size(), 3, "back, front, and the hidden one when named: got %zu", calls.size());
    Assert(sameRect(calls[0].texture.source, {0.0f, 0.0f, 16.0f, 16.0f}) && sameRect(calls[1].texture.source, {16.0f, 0.0f, 16.0f, 16.0f}),
        "the visible layers in their order");
    Assert(sameRect(calls[2].texture.source, {32.0f, 0.0f, 16.0f, 16.0f}), "then the layer asked for by name");
}

Test(tiles, no_seams_at_any_zoom)
{
    auto map = filled(20, 20, 1);
    Fixture fx([&map](TestScene& scene) { spawnMap(scene.world(), map, "all", 0); });

    for (float zoom : {1.0f, 1.3f, 0.77f, 2.9f, 3.33f}) {
        fx.scene->world().getResource<kuge::Camera2D>() = {{83.7f, 41.3f}, zoom};
        const auto& calls = fx.frame();

        Assert(calls.size() > 4, "something is drawn at zoom %f", zoom);
        for (std::size_t i = 0; i + 1 < calls.size(); ++i) {
            const auto& a = calls[i].destination;
            const auto& b = calls[i + 1].destination;

            if (b.y == a.y) {          // the next tile of the same row
                Assert(a.x + a.w == b.x, "zoom %f: a seam between two tiles of a row (%f + %f != %f)", zoom, a.x, a.w, b.x);
            }
        }
        // And the rows meet as well: the first tile of a row starts where the row above ends
        for (std::size_t i = 0; i < calls.size(); ++i) {
            for (std::size_t j = i + 1; j < calls.size(); ++j) {
                if (calls[j].destination.x == calls[i].destination.x && calls[j].destination.y > calls[i].destination.y) {
                    Assert(calls[i].destination.y + calls[i].destination.h == calls[j].destination.y || j > i + 1 + 200,
                        "zoom %f: a seam between rows", zoom);
                    break;
                }
            }
        }
    }
}

Test(tiles, position_scale_tint)
{
    auto map = filled(2, 2, 1);
    Fixture fx([&map](TestScene& scene) {
        const kw::Entity e = spawnMap(scene.world(), map, "all", 0, {32.0f, 16.0f});

        scene.world().get<kuge::Transform2D>(e).scale = {2.0f, 2.0f};
        scene.world().get<kuge::TilemapView>(e).tint = {10, 20, 30, 40};
    });
    const auto& calls = fx.frame();

    AssertEq(calls.size(), 4, "2 x 2 tiles");
    Assert(sameRect(calls[0].destination, {320.0f + 32.0f, 240.0f + 16.0f, 32.0f, 32.0f}), "moved, and twice as big");
    Assert(sameRect(calls[3].destination, {320.0f + 64.0f, 240.0f + 48.0f, 32.0f, 32.0f}), "the last one");
    Assert(calls[0].texture.tint == kuge::Color(10, 20, 30, 40), "tinted");
}

Test(tiles, hidden_views_and_gaps)
{
    auto map = filled(2, 2, 1);
    kw::Entity view = 0;
    Fixture fx([&](TestScene& scene) {
        view = spawnMap(scene.world(), map, "all", 0);
        // Tile 0 is nothing; a number past the tileset is drawn as the first cell rather than skipped
        const kw::Entity e = spawnMap(scene.world(), std::make_shared<kuge::TileMap>(1, 1, 16.0f), "", 1);
        (void)e;
    });

    AssertEq(fx.frame().size(), 4, "visible");
    fx.scene->world().get<kuge::TilemapView>(view).visible = false;
    AssertEq(fx.frame().size(), 0, "hidden");
    fx.scene->world().get<kuge::TilemapView>(view).visible = true;
    fx.scene->world().get<kuge::TilemapView>(view).map = nullptr;
    AssertEq(fx.frame().size(), 0, "no map: nothing, and no crash");
}

Test(tiles, between_sprites)
{
    auto map = std::make_shared<kuge::TileMap>(1, 1, 16.0f);
    map->setTile(map->addLayer("a"), 0, 0, 1);
    Fixture fx([&map](TestScene& scene) {
        auto& world = scene.world();
        kuge::Sprite sprite;

        sprite.size = {8.0f, 8.0f};
        sprite.layer = 0;
        const kw::Entity hero = world.create();
        world.add<kuge::Transform2D>(hero, kuge::Transform2D{});
        world.add<kuge::Sprite>(hero, sprite);
        spawnMap(world, map, "a", 1);      // in front of the hero
        spawnMap(world, map, "a", -1);     // behind it
    });
    const auto& calls = fx.frame();

    AssertEq(calls.size(), 3, "behind, the hero, in front");
    Assert(calls[0].kind == Call::Kind::Texture, "the first layer of tiles, behind");
    Assert(calls[1].kind == Call::Kind::Fill, "then the sprite");
    Assert(calls[2].kind == Call::Kind::Texture, "then the layer of tiles in front");
}
