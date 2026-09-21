extern "C" {
    #include "kronklab/kronklab.h"
}
#include "audio_helpers.hpp"
#include "client_fixture.hpp"
#include "Serializer.hpp"
#include <chrono>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    // A BMP of one color (24 bits, rows padded to 4 bytes)
    std::vector<std::uint8_t> solidBmp(int width, int height, std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        const int stride = (width * 3 + 3) / 4 * 4;
        kuge::ByteWriter out;

        out.write<char>('B');
        out.write<char>('M');
        out.write<std::uint32_t>(static_cast<std::uint32_t>(54 + stride * height));
        out.write<std::uint32_t>(0);
        out.write<std::uint32_t>(54);
        out.write<std::uint32_t>(40);
        out.write<std::int32_t>(width);
        out.write<std::int32_t>(height);
        out.write<std::uint16_t>(1);
        out.write<std::uint16_t>(24);
        out.write<std::uint32_t>(0);
        out.write<std::uint32_t>(static_cast<std::uint32_t>(stride * height));
        for (int i = 0; i < 4; ++i) { out.write<std::uint32_t>(0); }
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < stride; ++x) {
                out.write<std::uint8_t>(x < width * 3 ? (x % 3 == 0 ? b : x % 3 == 1 ? g : r) : 0);
            }
        }
        return out.bytes();
    }

    struct Picture
    {
        std::filesystem::path path;

        explicit Picture(const char* name)
            : path(std::filesystem::temp_directory_path() / ("kuge_test_" + std::to_string(::getpid()) + "_" + name + ".bmp")) {}
        ~Picture() { std::filesystem::remove(path); }

        // The date is set too, so that a test does not depend on the clock's grain
        void write(std::vector<std::uint8_t> bytes, int seconds)
        {
            kuge::writeFile(path, bytes);
            std::filesystem::last_write_time(path, std::filesystem::file_time_type{} + std::chrono::seconds(1000 + seconds));
        }
    };
}

Test(hot_reload, a_texture_changes)
{
    Fixture fx;
    Picture picture("hr_tex");

    picture.write(solidBmp(2, 2, 255, 0, 0), 1);
    auto texture = fx.client->textures().load(picture.path);
    const auto* before = fx.dummy.renderer->texture(texture->id());

    Assert(before && before->rgba[0] == 255 && before->rgba[1] == 0, "red at first");
    AssertEq(fx.client->reloadAssets(), 0, "nothing changed");
    picture.write(solidBmp(2, 2, 0, 0, 255), 2);
    AssertEq(fx.client->reloadAssets(), 1, "the file changed: one reload");
    const auto* after = fx.dummy.renderer->texture(texture->id());

    Assert(after && after->rgba[0] == 0 && after->rgba[2] == 255, "the same texture is blue now");
    AssertEq(fx.dummy.renderer->textureCount(), 2, "the old picture is given back (this one and the white pixel)");
    Assert(fx.client->textures().load(picture.path) == texture, "it is still the same object");
}

Test(hot_reload, the_size_may_change)
{
    Fixture fx;
    Picture picture("hr_size");

    picture.write(solidBmp(2, 2, 255, 0, 0), 1);
    auto texture = fx.client->textures().load(picture.path);

    picture.write(solidBmp(4, 3, 0, 255, 0), 2);
    fx.client->reloadAssets();
    AssertEq(texture->width(), 4, "the new width");
    AssertEq(texture->height(), 3, "and height");
    Assert(texture->size() == kuge::Vec2(4.0f, 3.0f), "and size");
    const auto* info = fx.dummy.renderer->texture(texture->id());
    Assert(info && info->width == 4 && info->rgba.size() == 4 * 3 * 4, "the renderer has all of it");
}

Test(hot_reload, a_bad_file_changes_nothing)
{
    Fixture fx;
    Picture picture("hr_bad");

    picture.write(solidBmp(2, 2, 255, 0, 0), 1);
    auto texture = fx.client->textures().load(picture.path);
    const kuge::TextureId id = texture->id();

    picture.write({'n', 'o', 't', ' ', 'a', ' ', 'p', 'i', 'c'}, 2);
    AssertEq(fx.client->reloadAssets(), 0, "not reloaded");
    Assert(texture->id() == id && texture->width() == 2, "the texture is as it was");
    const auto* info = fx.dummy.renderer->texture(id);
    Assert(info && info->rgba[0] == 255, "and still red");
    picture.write(solidBmp(2, 2, 0, 0, 255), 3);
    AssertEq(fx.client->reloadAssets(), 1, "the next good file is taken");
}

Test(hot_reload, a_drawn_sprite_follows)
{
    Picture picture("hr_sprite");

    picture.write(solidBmp(2, 2, 255, 0, 0), 1);
    Fixture fx([&picture](TestScene& scene) {
        const kw::Entity e = scene.world().create();
        kuge::Sprite sprite;

        sprite.texture = scene.world().getResource<kuge::Ref<kuge::AssetManager<kuge::Texture>>>()->load(picture.path);
        sprite.size = {16.0f, 16.0f};
        scene.world().add<kuge::Transform2D>(e, kuge::Transform2D{{100.0f, 100.0f}});
        scene.world().add<kuge::PreviousTransform2D>(e, kuge::PreviousTransform2D{{{100.0f, 100.0f}}});
        scene.world().add<kuge::Sprite>(e, sprite);
    });
    auto textureDrawn = [&fx](void) {
        std::optional<kuge::TextureId> id;

        for (const auto& call : fx.frame()) {
            if (call.kind == kuge::DummyRenderer::Call::Kind::Texture && call.texture.destination.w == 16.0f) {
                id = call.texture.texture;
            }
        }
        return id;
    };
    const auto first = textureDrawn();

    Assert(first.has_value() && fx.dummy.renderer->texture(*first)->rgba[0] == 255, "the sprite is drawn red");
    picture.write(solidBmp(2, 2, 0, 0, 255), 2);
    fx.client->reloadAssets();
    const auto second = textureDrawn();

    Assert(second.has_value() && fx.dummy.renderer->texture(*second)->rgba[2] == 255, "the next frame draws it blue, with nothing done to the sprite");
}

Test(hot_reload, sounds_change)
{
    Fixture fx;
    SoundFile file("hr_sound");
    auto sound = fx.client->sounds().load(file.path);
    const kuge::SoundId id = sound->id();

    AssertEq(fx.client->reloadAssets(), 0, "nothing changed");
    kuge::writeFile(file.path, makeWav(0.3));
    std::filesystem::last_write_time(file.path, std::filesystem::file_time_type{} + std::chrono::seconds(5000));
    AssertEq(fx.client->reloadAssets(), 1, "the file changed");
    Assert(sound->id() != id, "the sound has another id: it was loaded again");
    AssertEq(fx.dummy.audio->soundCount(), 1, "and the old one was given back");
    Assert(fx.client->sounds().load(file.path) == sound, "the same object");
}

Test(hot_reload, watching_is_by_itself)
{
    auto dummy = kuge::makeDummyBackend();
    auto* renderer = dummy.renderer;
    kuge::Engine engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed});
    auto& client = engine.addModule<kuge::ClientModule>(std::move(dummy.backend), kuge::ClientConfig{.watchAssets = 1e-9});
    Picture picture("hr_watch");

    engine.scenes().change<TestScene>();       // a loop only runs with a scene
    picture.write(solidBmp(2, 2, 255, 0, 0), 1);
    auto texture = client.textures().load(picture.path);
    engine.step(0.0);
    picture.write(solidBmp(2, 2, 0, 0, 255), 2);
    engine.step(0.0);
    const auto* info = renderer->texture(texture->id());

    Assert(info && info->rgba[2] == 255, "a loop later, the new picture is there");
}

Test(hot_reload, not_watching_by_default)
{
    Fixture fx;
    Picture picture("hr_off");

    picture.write(solidBmp(2, 2, 255, 0, 0), 1);
    auto texture = fx.client->textures().load(picture.path);
    picture.write(solidBmp(2, 2, 0, 0, 255), 2);
    fx.engine.step(0.0);
    fx.engine.step(0.0);
    const auto* info = fx.dummy.renderer->texture(texture->id());

    Assert(info && info->rgba[0] == 255, "nothing is reloaded unless it is asked");
}
