extern "C" {
    #include "kronklab/kronklab.h"
}
#include "client_fixture.hpp"
#include "render/Components.hpp"

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    // A scene pushed over the game: it draws one box and lets the game under it show
    struct Menu : kuge::ClientScene
    {
        void onEnter(void) override
        {
            installClientSystems();
            const kw::Entity entity = world().create();
            kuge::Sprite sprite;

            sprite.size = {20.0f, 20.0f};
            sprite.tint = kuge::colors::Blue;
            world().add<kuge::Transform2D>(entity, kuge::Transform2D{{100.0f, 0.0f}});
            world().add<kuge::Sprite>(entity, sprite);
        }

        kuge::OverlayMode overlay(void) const override { return kuge::OverlayMode::Transparent; }
    };
}

Test(overlay, game_is_drawn_under_menu)
{
    Fixture fx([](TestScene& scene) {
        const kw::Entity entity = scene.world().create();
        kuge::Sprite sprite;

        sprite.size = {20.0f, 20.0f};
        sprite.tint = kuge::colors::Red;
        scene.world().add<kuge::Transform2D>(entity, kuge::Transform2D{{0.0f, 0.0f}});
        scene.world().add<kuge::Sprite>(entity, sprite);
    });

    fx.engine.scenes().push<Menu>();
    const auto& calls = fx.frame();

    Assert(calls.size() == 2, "both are drawn: %zu draw calls", calls.size());
    Assert(calls.size() == 2 && calls[0].color == kuge::colors::Red && calls[1].color == kuge::colors::Blue,
        "the game first, the menu over it");
}
