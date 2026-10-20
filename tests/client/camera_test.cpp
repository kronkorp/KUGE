extern "C" {
    #include "kronklab/kronklab.h"
}
#include "client_fixture.hpp"
#include "render/Components.hpp"
#include <cmath>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using Call = kuge::DummyRenderer::Call;
    using Fit = kuge::Camera2D::Fit;

    bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }
    bool near(kuge::Vec2 a, kuge::Vec2 b) { return near(a.x, b.x) && near(a.y, b.y); }
    bool near(const kuge::Rect& a, const kuge::Rect& b) { return near(a.x, b.x) && near(a.y, b.y) && near(a.w, b.w) && near(a.h, b.h); }

    kuge::Camera2D camera(kuge::Vec2 logical, Fit fit)
    {
        kuge::Camera2D result;

        result.logicalSize = logical;
        result.fit = fit;
        return result;
    }
}

Test(camera, no_logical_size_is_as_before)
{
    kuge::Camera2D cam;

    cam.zoom = 2.0f;
    cam.position = {10.0f, 20.0f};
    const kuge::Vec2 window{1280.0f, 720.0f};

    Assert(near(cam.visibleSize(window), {640.0f, 360.0f}), "the window divided by the zoom");
    Assert(near(cam.worldToScreen({10.0f, 20.0f}, window), {640.0f, 360.0f}), "the position is the center");
    Assert(near(cam.view(window).area, {0.0f, 0.0f, 1280.0f, 720.0f}), "all of the window");
}

Test(camera, letterbox_shows_the_same)
{
    const kuge::Camera2D cam = camera({640.0f, 360.0f}, Fit::Letterbox);

    Assert(near(cam.visibleSize({1280.0f, 720.0f}), {640.0f, 360.0f}), "640 x 360 in 1280 x 720");
    Assert(near(cam.visibleSize({1920.0f, 1080.0f}), {640.0f, 360.0f}), "and in 1920 x 1080");
    Assert(near(cam.visibleSize({1280.0f, 800.0f}), {640.0f, 360.0f}), "and in a taller window");
    const kuge::CameraView tall = cam.view({1280.0f, 800.0f});

    Assert(near(tall.area, {0.0f, 40.0f, 1280.0f, 720.0f}), "bars at the top and the bottom: %f", tall.area.y);
    const kuge::CameraView wide = cam.view({1920.0f, 720.0f});

    Assert(near(wide.area, {320.0f, 0.0f, 1280.0f, 720.0f}), "or at the sides");
}

Test(camera, integer_letterbox_scale)
{
    const kuge::Camera2D cam = camera({640.0f, 360.0f}, Fit::IntegerLetterbox);
    const kuge::CameraView big = cam.view({1920.0f, 1100.0f});

    Assert(near(big.scale, {3.0f, 3.0f}), "a whole number of pixels: x3, got %f", big.scale.x);
    Assert(near(big.area, {0.0f, 10.0f, 1920.0f, 1080.0f}), "and bars");
    Assert(near(cam.view({1400.0f, 800.0f}).scale, {2.0f, 2.0f}), "2.18 is cut down to 2");
    Assert(near(cam.view({480.0f, 270.0f}).scale, {0.75f, 0.75f}), "a window smaller than the screen: a letterbox");
}

Test(camera, fit_height_and_stretch)
{
    const kuge::Camera2D height = camera({640.0f, 360.0f}, Fit::FitHeight);

    Assert(near(height.visibleSize({1920.0f, 720.0f}), {960.0f, 360.0f}), "a wider window shows more at the sides");
    Assert(near(height.visibleSize({800.0f, 720.0f}), {400.0f, 360.0f}), "a narrower one less");
    const kuge::Camera2D stretch = camera({640.0f, 360.0f}, Fit::Stretch);

    Assert(near(stretch.visibleSize({1920.0f, 800.0f}), {640.0f, 360.0f}), "stretched: the same world");
    Assert(near(stretch.view({1920.0f, 800.0f}).scale, {3.0f, 800.0f / 360.0f}), "with pixels that are not square");
}

Test(camera, screen_and_world_are_inverses)
{
    for (const Fit fit : {Fit::Stretch, Fit::Letterbox, Fit::IntegerLetterbox, Fit::FitHeight}) {
        kuge::Camera2D cam = camera({640.0f, 360.0f}, fit);

        cam.zoom = 1.5f;
        cam.position = {33.0f, -12.0f};
        cam.viewport = kuge::Rect{100.0f, 50.0f, 1000.0f, 900.0f};
        const kuge::Vec2 window{1280.0f, 1000.0f};
        const kuge::Vec2 point{-17.0f, 200.0f};
        const kuge::Vec2 back = cam.screenToWorld(cam.worldToScreen(point, window), window);

        Assert(near(back, point), "fit %d: back at (%f, %f)", static_cast<int>(fit), back.x, back.y);
    }
}

Test(camera, letterbox_draws_in_its_area)
{
    Fixture fx([](TestScene& scene) {
        const kw::Entity entity = scene.world().create();
        kuge::Sprite sprite;

        sprite.size = {64.0f, 36.0f};
        scene.world().add<kuge::Transform2D>(entity, kuge::Transform2D{{0.0f, 0.0f}});
        scene.world().add<kuge::Sprite>(entity, sprite);
        scene.world().getResource<kuge::Camera2D>() = camera({640.0f, 360.0f}, Fit::Letterbox);
    }, {1280.0f, 800.0f});
    const auto& calls = fx.frame();

    AssertEq(calls.size(), 1, "one draw call, got %zu", calls.size());
    Assert(near(calls[0].destination, {576.0f, 364.0f, 128.0f, 72.0f}), "twice as big, at the center of the area");
    Assert(calls[0].clip && near(*calls[0].clip, {0.0f, 40.0f, 1280.0f, 720.0f}), "and cut at the bars");
    fx.dummy.window->setSize(1920, 1080);
    const auto& bigger = fx.frame();

    Assert(near(bigger[0].destination, {864.0f, 486.0f, 192.0f, 108.0f}), "the same part of the world in 1920 x 1080");
}

Test(camera, viewport_is_its_own_part)
{
    Fixture fx([](TestScene& scene) {
        const kw::Entity entity = scene.world().create();
        kuge::Sprite sprite;

        sprite.size = {10.0f, 10.0f};
        scene.world().add<kuge::Transform2D>(entity, kuge::Transform2D{{0.0f, 0.0f}});
        scene.world().add<kuge::Sprite>(entity, sprite);
        scene.world().getResource<kuge::Camera2D>().viewport = kuge::Rect{320.0f, 0.0f, 320.0f, 480.0f};
    });
    const auto& calls = fx.frame();

    AssertEq(calls.size(), 1, "one draw call");
    Assert(near(calls[0].destination, {475.0f, 235.0f, 10.0f, 10.0f}), "centered in its half: %f", calls[0].destination.x);
    Assert(calls[0].clip && near(*calls[0].clip, {320.0f, 0.0f, 320.0f, 480.0f}), "and cut to it");
}
