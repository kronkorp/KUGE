extern "C" {
    #include "kronklab/kronklab.h"
}
#include "client_fixture.hpp"
#include "render/Components.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using Call = kuge::DummyRenderer::Call;

    bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

    bool sameRect(const kuge::Rect& a, const kuge::Rect& b)
    {
        return near(a.x, b.x) && near(a.y, b.y) && near(a.w, b.w) && near(a.h, b.h);
    }

    kw::Entity spawn(kw::World& world, kuge::Vec2 position, kuge::Sprite sprite)
    {
        const kw::Entity entity = world.create();

        world.add<kuge::Transform2D>(entity, kuge::Transform2D{position});
        world.add<kuge::Sprite>(entity, sprite);
        return entity;
    }

    kuge::Sprite box(float w, float h, kuge::Color tint = kuge::colors::Red)
    {
        kuge::Sprite sprite;

        sprite.size = {w, h};
        sprite.tint = tint;
        return sprite;
    }

    // Moves what has a Transform2D 10 pixels to the right at each tick
    class Mover : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override
            {
                auto view = world.view<kuge::Transform2D>();

                for (kw::Entity entity : view) {
                    world.get<kuge::Transform2D>(entity).position.x += 10.0f;
                }
                return true;
            }
    };
}

Test(sprites, plain_rectangle_at_the_center)
{
    Fixture fx([](TestScene& scene) { spawn(scene.world(), {0.0f, 0.0f}, box(100.0f, 50.0f)); });
    const auto& calls = fx.frame();

    AssertEq(calls.size(), 1, "one draw call, got %zu", calls.size());
    Assert(calls[0].kind == Call::Kind::Fill, "a plain filled rectangle");
    Assert(sameRect(calls[0].destination, {270.0f, 215.0f, 100.0f, 50.0f}), "centered on the 640x480 screen");
    Assert(calls[0].color == kuge::colors::Red, "with its color");
}

Test(sprites, pivot_is_the_position)
{
    Fixture fx([](TestScene& scene) {
        kuge::Sprite sprite = box(100.0f, 50.0f);

        sprite.pivot = {0.0f, 0.0f};
        spawn(scene.world(), {10.0f, 20.0f}, sprite);
    });

    Assert(sameRect(fx.frame()[0].destination, {330.0f, 260.0f, 100.0f, 50.0f}), "the top-left corner is at the position");
}

Test(sprites, scale_and_camera)
{
    Fixture fx([](TestScene& scene) {
        const kw::Entity entity = spawn(scene.world(), {100.0f, 0.0f}, box(10.0f, 10.0f));

        scene.world().get<kuge::Transform2D>(entity).scale = {2.0f, 3.0f};
    });

    // At (100, 0): 100 pixels right of the center, scaled 2 by 3
    Assert(sameRect(fx.frame()[0].destination, {410.0f, 225.0f, 20.0f, 30.0f}), "scaled by its scale");
    // camera on the sprite: it is at the center, and zoom scales everything
    fx.scene->world().getResource<kuge::Camera2D>() = {{100.0f, 0.0f}, 2.0f};
    Assert(sameRect(fx.frame()[0].destination, {300.0f, 210.0f, 40.0f, 60.0f}), "twice as big, at the center");
    fx.scene->world().getResource<kuge::Camera2D>() = {{0.0f, 0.0f}, 1.0f};
    Assert(sameRect(fx.frame()[0].destination, {410.0f, 225.0f, 20.0f, 30.0f}), "back to no camera");
}

Test(sprites, layers_then_z_then_entity)
{
    Fixture fx([](TestScene& scene) {
        auto& world = scene.world();
        auto make = [&](int layer, float z, std::uint8_t id) {
            kuge::Sprite sprite = box(10.0f, 10.0f, {id, 0, 0, 255});

            sprite.layer = layer;
            sprite.z = z;
            spawn(world, {0.0f, 0.0f}, sprite);
        };

        make(1, 0.0f, 1);   // A: on top
        make(0, 5.0f, 2);   // B
        make(0, 1.0f, 3);   // C
        make(0, 1.0f, 4);   // D: same as C, created after it
    });
    const auto& calls = fx.frame();

    AssertEq(calls.size(), 4, "all drawn");
    AssertEq(calls[0].color.r, 3, "C first: lowest layer, lowest z");
    AssertEq(calls[1].color.r, 4, "D next: same as C, later entity");
    AssertEq(calls[2].color.r, 2, "B: higher z");
    AssertEq(calls[3].color.r, 1, "A last: higher layer");
}

Test(sprites, order_does_not_follow_storage)
{
    // Removing and adding entities changes how the World keeps them
    Fixture fx([](TestScene& scene) {
        auto& world = scene.world();
        std::vector<kw::Entity> entities;

        for (int i = 0; i < 6; ++i) {
            entities.push_back(spawn(world, {0.0f, 0.0f}, box(10.0f, 10.0f)));
        }
        world.remove(entities[1]);
        world.remove(entities[3]);
        for (int i = 0; i < 3; ++i) {
            spawn(world, {0.0f, 0.0f}, box(10.0f, 10.0f));
        }
        // Tint = the entity, so that the calls tell who was drawn when
        auto view = world.view<kuge::Sprite>();
        for (kw::Entity entity : view) {
            world.get<kuge::Sprite>(entity).tint = {static_cast<std::uint8_t>(entity), 0, 0, 255};
        }
    });
    const auto& calls = fx.frame();
    std::vector<int> drawn;

    for (const auto& call : calls) {
        drawn.push_back(call.color.r);
    }
    AssertEq(drawn.size(), 7, "7 entities left, got %zu", drawn.size());
    Assert(std::is_sorted(drawn.begin(), drawn.end()), "drawn by entity, whatever the storage");
}

Test(sprites, hidden_and_incomplete_skipped)
{
    Fixture fx([](TestScene& scene) {
        auto& world = scene.world();
        kuge::Sprite hidden = box(10.0f, 10.0f);

        hidden.visible = false;
        spawn(world, {0.0f, 0.0f}, hidden);

        const kw::Entity noSprite = world.create();
        world.add<kuge::Transform2D>(noSprite, kuge::Transform2D{});

        const kw::Entity noTransform = world.create();
        world.add<kuge::Sprite>(noTransform, box(10.0f, 10.0f));
    });

    AssertEq(fx.frame().size(), 0, "nothing to draw");
}

Test(sprites, offscreen_is_skipped)
{
    Fixture fx([](TestScene& scene) {
        auto& world = scene.world();

        spawn(world, {1000.0f, 0.0f}, box(10.0f, 10.0f));     // far right
        spawn(world, {0.0f, -600.0f}, box(10.0f, 10.0f));     // far above
        spawn(world, {-325.0f, 0.0f}, box(100.0f, 10.0f));    // half in: x from -55 to 45
        spawn(world, {-400.0f, 0.0f}, box(100.0f, 10.0f));    // x from -130 to -30: out
    });

    AssertEq(fx.frame().size(), 1, "only the one that shows, got %zu", fx.frame().size());
}

Test(sprites, turned_ones_reach_further)
{
    Fixture fx([](TestScene& scene) {
        auto& world = scene.world();
        const kw::Entity turned = spawn(world, {-380.0f, 0.0f}, box(100.0f, 100.0f));   // x from -110 to -10

        world.get<kuge::Transform2D>(turned).rotation = 45.0f;
        spawn(world, {-380.0f, 200.0f}, box(100.0f, 100.0f));                            // same, not turned
    });

    AssertEq(fx.frame().size(), 1, "the turned one may reach the screen, the other cannot");
}

Test(sprites, textured_sprites)
{
    std::shared_ptr<kuge::Texture> texture;
    Fixture fx([&texture](TestScene& scene) {
        auto& world = scene.world();
        auto& renderer = *world.getResource<kuge::Ref<kuge::IRenderer2D>>();
        std::vector<std::uint8_t> pixels(4 * 2 * 4, 200);
        kuge::Sprite sprite;

        texture = kuge::Texture::fromPixels(renderer, 4, 2, pixels);
        sprite.texture = texture;
        sprite.tint = {10, 20, 30, 255};
        sprite.flipX = true;
        spawn(world, {0.0f, 0.0f}, sprite);

        kuge::Sprite part;
        part.texture = texture;
        part.source = {1.0f, 0.0f, 2.0f, 2.0f};
        spawn(world, {0.0f, 100.0f}, part);
    });
    const auto& calls = fx.frame();

    AssertEq(calls.size(), 2, "two textured sprites");
    Assert(calls[0].kind == Call::Kind::Texture && calls[0].texture.texture == texture->id(), "the texture");
    Assert(sameRect(calls[0].destination, {318.0f, 239.0f, 4.0f, 2.0f}), "the size of the picture");
    Assert(calls[0].texture.flipX && !calls[0].texture.flipY, "flipped");
    Assert(calls[0].texture.tint == kuge::Color(10, 20, 30, 255), "tinted");
    Assert(sameRect(calls[1].texture.source, {1.0f, 0.0f, 2.0f, 2.0f}), "a part of it");
    Assert(sameRect(calls[1].destination, {319.0f, 339.0f, 2.0f, 2.0f}), "and it takes the size of the part");
    // A texture must not outlive the renderer, which goes with the fixture
    texture.reset();
}

Test(sprites, a_sheet_gives_the_cell)
{
    std::shared_ptr<kuge::Spritesheet> sheet;
    Fixture fx([&sheet](TestScene& scene) {
        auto& world = scene.world();
        auto& renderer = *world.getResource<kuge::Ref<kuge::IRenderer2D>>();
        std::vector<std::uint8_t> pixels(32 * 16 * 4, 200);
        kuge::Sprite cell, outOfRange, both, noPicture;

        sheet = std::make_shared<kuge::Spritesheet>();
        sheet->texture = kuge::Texture::fromPixels(renderer, 32, 16, pixels);   // 4 x 2 cells of 8 x 8
        sheet->frameWidth = 8;
        sheet->frameHeight = 8;
        cell.sheet = sheet;
        cell.frame = 5;
        spawn(world, {0.0f, 0.0f}, cell);
        outOfRange.sheet = sheet;
        outOfRange.frame = 99;
        spawn(world, {0.0f, 100.0f}, outOfRange);
        both.texture = kuge::Texture::fromPixels(renderer, 4, 4, std::span(pixels).first(4 * 4 * 4));
        both.source = {1.0f, 1.0f, 2.0f, 2.0f};
        both.sheet = sheet;
        both.frame = 2;
        spawn(world, {0.0f, 200.0f}, both);
        noPicture.sheet = std::make_shared<kuge::Spritesheet>();   // cells of 16 x 16, not loaded yet
        spawn(world, {0.0f, -100.0f}, noPicture);
    });
    const auto& calls = fx.frame();
    std::vector<Call> pictures;
    std::vector<Call> fills;

    for (const Call& call : calls) {
        (call.kind == Call::Kind::Texture ? pictures : fills).push_back(call);
    }
    AssertEq(pictures.size(), 3, "three sprites drawn from the sheet, got %zu", pictures.size());
    for (const Call& call : pictures) {
        Assert(call.texture.texture == sheet->texture->id(), "with the picture of the sheet");
    }
    Assert(sameRect(pictures[0].texture.source, {8.0f, 8.0f, 8.0f, 8.0f}), "cell 5: second row, second column");
    Assert(sameRect(pictures[0].destination, {316.0f, 236.0f, 8.0f, 8.0f}), "at the size of a cell");
    Assert(sameRect(pictures[1].texture.source, {0.0f, 0.0f, 8.0f, 8.0f}), "out of range: the first cell");
    Assert(sameRect(pictures[2].texture.source, {16.0f, 0.0f, 8.0f, 8.0f}), "the sheet wins over a texture and a source");
    AssertEq(fills.size(), 1, "a sheet without its picture...");
    Assert(sameRect(fills[0].destination, {312.0f, 132.0f, 16.0f, 16.0f}), "...is a plain rectangle the size of a cell");
    // A texture must not outlive the renderer, which goes with the fixture
    sheet.reset();
}

Test(sprites, turned_rects_use_white_pixel)
{
    Fixture fx([](TestScene& scene) {
        auto& world = scene.world();
        const kw::Entity turned = spawn(world, {0.0f, 0.0f}, box(20.0f, 10.0f));
        kuge::Sprite flipped = box(20.0f, 10.0f);

        world.get<kuge::Transform2D>(turned).rotation = 90.0f;
        flipped.flipX = true;
        spawn(world, {0.0f, 50.0f}, flipped);
        spawn(world, {0.0f, 100.0f}, box(20.0f, 10.0f));
    });
    const auto& calls = fx.frame();
    const auto white = fx.scene->world().getResource<kuge::WhitePixel>().texture->id();

    AssertEq(calls.size(), 3, "three rectangles");
    Assert(calls[0].kind == Call::Kind::Texture && calls[0].texture.texture == white, "turned: the white pixel, stretched");
    Assert(near(calls[0].texture.rotation, 90.0f), "at its angle");
    Assert(calls[0].texture.tint == kuge::colors::Red, "in its color");
    Assert(calls[1].kind == Call::Kind::Texture && calls[1].texture.flipX, "flipped: the same way");
    Assert(calls[2].kind == Call::Kind::Fill, "the plain one stays a fill");
}

Test(sprites, movement_is_smoothed)
{
    Fixture fx([](TestScene& scene) {
        auto& world = scene.world();
        const kw::Entity entity = spawn(world, {0.0f, 0.0f}, box(100.0f, 100.0f));

        world.add<kuge::PreviousTransform2D>(entity, kuge::PreviousTransform2D{});
        scene.addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Mover>());
    });

    // One tick: the entity is at 10, and the frame is right at that tick: the
    // screen shows where it was, one tick behind
    Assert(near(fx.frame(1.0 / 60.0)[0].destination.x, 270.0f), "at the tick: where it was (0)");
    Assert(near(fx.frame(0.5 / 60.0)[0].destination.x, 275.0f), "half way: 5");
    Assert(near(fx.frame(0.5 / 60.0)[0].destination.x, 280.0f), "next tick: 10");
    Assert(near(fx.frame(0.25 / 60.0)[0].destination.x, 282.5f), "a quarter: 12.5");
}

Test(sprites, without_previous_no_smoothing)
{
    Fixture fx([](TestScene& scene) {
        spawn(scene.world(), {0.0f, 0.0f}, box(100.0f, 100.0f));
        scene.addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Mover>());
    });

    Assert(near(fx.frame(1.5 / 60.0)[0].destination.x, 280.0f), "drawn where it is: 10");
}

Test(sprites, angles_go_the_short_way)
{
    Assert(near(kuge::lerpAngle(0.0f, 90.0f, 0.5f), 45.0f), "simple");
    Assert(near(std::fmod(kuge::lerpAngle(350.0f, 10.0f, 0.5f) + 360.0f, 360.0f), 0.0f), "350 to 10 goes through 0");
    Assert(near(std::fmod(kuge::lerpAngle(10.0f, 350.0f, 0.5f) + 360.0f, 360.0f), 0.0f), "and back");
    Assert(near(kuge::lerpAngle(30.0f, 30.0f, 0.7f), 30.0f), "no change");
    Assert(near(kuge::lerpAngle(0.0f, 90.0f, 0.0f), 0.0f) && near(kuge::lerpAngle(0.0f, 90.0f, 1.0f), 90.0f), "ends");
}

Test(sprites, camera_maps_both_ways)
{
    const kuge::Camera2D camera{{100.0f, 50.0f}, 2.0f};
    const kuge::Vec2 screen{640.0f, 480.0f};
    const kuge::Vec2 world{130.0f, 20.0f};
    const kuge::Vec2 there = camera.worldToScreen(world, screen);
    const kuge::Vec2 back = camera.screenToWorld(there, screen);

    Assert(near(camera.worldToScreen(camera.position, screen).x, 320.0f), "what the camera looks at is at the center");
    Assert(near(there.x, 380.0f) && near(there.y, 180.0f), "scaled by the zoom around the center");
    Assert(near(back.x, world.x) && near(back.y, world.y), "and it goes back");
}
