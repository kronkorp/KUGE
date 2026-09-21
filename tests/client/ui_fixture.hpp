#pragma once

#include "client_fixture.hpp"
#include "ui/Ui.hpp"
#include <filesystem>
#include <fstream>
#include <unistd.h>

// The interface on the dummy backend, with the dummy font: a letter is 8 pixels wide, a line 12 high

enum class UiAct : std::uint8_t { Up, Down, Left, Right, Accept, Cancel, Click };

struct FontFile
{
    std::filesystem::path path;

    FontFile() : path(std::filesystem::temp_directory_path() / ("kuge_test_" + std::to_string(::getpid()) + "_font.ttf"))
    {
        std::ofstream(path) << "not really a font: the dummy loader only needs a file";
    }
    ~FontFile() { std::filesystem::remove(path); }
};

inline kuge::UiActions uiActions(void)
{
    return {.up = kuge::actionId(UiAct::Up), .down = kuge::actionId(UiAct::Down), .left = kuge::actionId(UiAct::Left),
            .right = kuge::actionId(UiAct::Right), .accept = kuge::actionId(UiAct::Accept),
            .cancel = kuge::actionId(UiAct::Cancel), .click = kuge::actionId(UiAct::Click)};
}

//! Puts the interface in a scene, with a font, and lets it build its nodes
inline void withUi(TestScene& scene, const FontFile& font, const std::function<void(kw::World&)>& build)
{
    kuge::installUi(scene.setup(), uiActions());
    auto* client = scene.ctx().engine().module<kuge::ClientModule>();

    scene.world().getResource<kuge::UiTheme>().font = client->loadFont(font.path, 16);
    build(scene.world());
}

inline kw::Entity uiNode(kw::World& world, kuge::UiNode node)
{
    const kw::Entity entity = world.create();

    world.add<kuge::UiNode>(entity, node);
    return entity;
}

inline kw::Entity uiChild(kw::World& world, kw::Entity parent, kuge::UiNode node = {})
{
    node.parent = parent;
    node.hasParent = true;
    return uiNode(world, node);
}

inline kw::Entity uiButton(kw::World& world, const char* text, kw::Entity parent, bool enabled = true)
{
    const kw::Entity entity = uiChild(world, parent);

    world.add<kuge::UiButton>(entity, kuge::UiButton{.text = text, .enabled = enabled});
    return entity;
}

inline kw::Entity uiLabel(kw::World& world, const char* text, kuge::UiNode node = {})
{
    const kw::Entity entity = uiNode(world, node);
    kuge::UiLabel label;

    label.text = text;
    world.add<kuge::UiLabel>(entity, label);
    return entity;
}

inline void bindUiKeys(kuge::InputMap& input)
{
    input.bind(UiAct::Up, kuge::Key::Up);
    input.bind(UiAct::Down, kuge::Key::Down);
    input.bind(UiAct::Left, kuge::Key::Left);
    input.bind(UiAct::Right, kuge::Key::Right);
    input.bind(UiAct::Accept, kuge::Key::Enter);
    input.bind(UiAct::Cancel, kuge::Key::Escape);
    input.bind(UiAct::Click, kuge::MouseButton::Left);
}

inline bool sameRect(const kuge::Rect& a, const kuge::Rect& b)
{
    return std::fabs(a.x - b.x) < 1e-3f && std::fabs(a.y - b.y) < 1e-3f && std::fabs(a.w - b.w) < 1e-3f && std::fabs(a.h - b.h) < 1e-3f;
}
