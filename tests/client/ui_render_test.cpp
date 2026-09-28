extern "C" {
    #include "kronklab/kronklab.h"
}
#include "ui_fixture.hpp"

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using Call = kuge::DummyRenderer::Call;
    using Kind = Call::Kind;

    std::size_t count(const std::vector<Call>& calls, Kind kind)
    {
        return static_cast<std::size_t>(std::count_if(calls.begin(), calls.end(), [kind](const Call& c) { return c.kind == kind; }));
    }
}

Test(uirender, a_panel)
{
    FontFile font;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            const kw::Entity p = uiNode(w, {.size = {100.0f, 60.0f}});

            w.add<kuge::UiPanel>(p, kuge::UiPanel{.fill = {1, 2, 3, 255}, .border = {9, 8, 7, 255}, .borderWidth = 2.0f});
        });
    });
    const auto& calls = fx.frame();

    AssertEq(calls.size(), 5, "the fill and four sides of border, got %zu", calls.size());
    Assert(calls[0].kind == Kind::Fill && calls[0].destination == kuge::Rect(0.0f, 0.0f, 100.0f, 60.0f) && calls[0].color == kuge::Color(1, 2, 3, 255),
        "the fill, whole");
    Assert(calls[1].destination == kuge::Rect(0.0f, 0.0f, 100.0f, 2.0f) && calls[1].color == kuge::Color(9, 8, 7, 255), "the top");
    Assert(calls[2].destination == kuge::Rect(0.0f, 58.0f, 100.0f, 2.0f), "the bottom");
    Assert(calls[3].destination == kuge::Rect(0.0f, 2.0f, 2.0f, 56.0f), "the left, between the two");
    Assert(calls[4].destination == kuge::Rect(98.0f, 2.0f, 2.0f, 56.0f), "the right");
}

Test(uirender, a_panel_without_border)
{
    FontFile font;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            const kw::Entity p = uiNode(w, {.size = {100.0f, 60.0f}});

            w.add<kuge::UiPanel>(p, kuge::UiPanel{.borderWidth = 0.0f});
        });
    });

    AssertEq(fx.frame().size(), 1, "just the fill");
}

Test(uirender, a_label)
{
    FontFile font;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            const kw::Entity l = uiLabel(w, "Score", {.offset = {10.0f, 20.0f}});

            w.get<kuge::UiLabel>(l).color = {200, 100, 50, 255};
        });
    });
    const auto& calls = fx.frame();

    AssertEq(calls.size(), 1, "one text");
    Assert(calls[0].kind == Kind::Texture && calls[0].destination == kuge::Rect(10.0f, 20.0f, 40.0f, 12.0f), "at its place");
    Assert(calls[0].texture.tint == kuge::Color(200, 100, 50, 255), "in its color");
}

Test(uirender, text_alignment)
{
    FontFile font;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            const kuge::TextAlign aligns[3] = {kuge::TextAlign::Left, kuge::TextAlign::Center, kuge::TextAlign::Right};

            for (int i = 0; i < 3; ++i) {
                const kw::Entity l = uiLabel(w, "abcd", {.offset = {0.0f, 100.0f * static_cast<float>(i)}, .size = {100.0f, 20.0f}});

                w.get<kuge::UiLabel>(l).align = aligns[i];
            }
        });
    });
    const auto& calls = fx.frame();

    AssertEq(calls.size(), 3, "three texts");
    AssertEq(calls[0].destination.x, 0.0f, "left");
    AssertEq(calls[1].destination.x, 34.0f, "centered: (100 - 32) / 2");
    AssertEq(calls[2].destination.x, 68.0f, "right: 100 - 32");
}

Test(uirender, a_wrapped_label)
{
    FontFile font;
    kw::Entity label = 0;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            label = uiLabel(w, "one two three", {});
            w.get<kuge::UiLabel>(label).wrapWidth = 48;
        });
    });

    AssertEq(fx.scene->world().getResource<kuge::UiLayoutResult>().rects.at(label).h, 36.0f, "three lines: the node is as high as the text");
    AssertEq(fx.frame()[0].destination.h, 36.0f, "and so is the picture");
}

Test(uirender, buttons_show_their_state)
{
    FontFile font;
    kw::Entity a = 0, b = 0, c = 0;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            const kw::Entity column = uiNode(w, {});

            w.add<kuge::UiStack>(column, kuge::UiStack{});
            a = uiButton(w, "A", column);
            b = uiButton(w, "B", column);
            c = uiButton(w, "C", column, false);
        });
    });
    const auto theme = fx.scene->world().getResource<kuge::UiTheme>();
    auto fillOf = [&](kw::Entity e, const std::vector<Call>& calls) {
        const auto& rect = fx.scene->world().getResource<kuge::UiLayoutResult>().rects.at(e);

        for (const auto& call : calls) {
            if (call.kind == Kind::Fill && call.destination == rect) {
                return call.color;
            }
        }
        return kuge::Color{1, 1, 1, 1};
    };

    bindUiKeys(fx.client->input());
    // Before any tick: nothing has the focus
    auto calls = fx.frame();
    Assert(fillOf(a, calls) == theme.buttonFill && fillOf(b, calls) == theme.buttonFill, "normal");
    Assert(fillOf(c, calls) == theme.buttonDisabled, "disabled");

    calls = fx.frame(1.0 / 60.0);                              // a tick: the first button gets the focus
    Assert(fillOf(a, calls) == theme.buttonHover, "focused: the hover color");
    Assert(count(calls, Kind::Fill) == 3 + 4, "with a ring of four sides around it (three buttons + 4)");
    fx.dummy.input->push(kuge::KeyEvent{kuge::Key::Enter, true});
    calls = fx.frame(1.0 / 60.0);
    Assert(fillOf(a, calls) == theme.buttonPressed, "held down: the pressed color");
    Assert(fillOf(b, calls) == theme.buttonFill, "the others stay as they were");
}

Test(uirender, the_focus_ring)
{
    FontFile font;
    kw::Entity only = 0;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            only = uiNode(w, {.offset = {50.0f, 50.0f}, .size = {100.0f, 40.0f}});
            w.add<kuge::UiButton>(only, kuge::UiButton{.text = "Go"});
        });
    });
    const auto ring = fx.scene->world().getResource<kuge::UiTheme>().focusRing;

    bindUiKeys(fx.client->input());
    const auto& calls = fx.frame(1.0 / 60.0);
    std::size_t sides = 0;

    for (const auto& call : calls) {
        sides += (call.kind == Kind::Fill && call.color == ring) ? 1 : 0;
    }
    AssertEq(sides, 4, "four sides");
    Assert(calls[1].destination == kuge::Rect(50.0f, 50.0f, 100.0f, 3.0f), "the top side is 3 pixels thick, on the border of the button");
}

Test(uirender, a_disabled_text_is_grey)
{
    FontFile font;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            const kw::Entity column = uiNode(w, {});

            w.add<kuge::UiStack>(column, kuge::UiStack{});
            uiButton(w, "On", column);
            uiButton(w, "Off", column, false);
        });
    });
    const auto theme = fx.scene->world().getResource<kuge::UiTheme>();
    const auto& calls = fx.frame();
    std::vector<kuge::Color> tints;

    for (const auto& call : calls) {
        if (call.kind == Kind::Texture) {
            tints.push_back(call.texture.tint);
        }
    }
    AssertEq(tints.size(), 2, "two texts");
    Assert(tints[0] == theme.buttonText && tints[1] == theme.disabledText, "the disabled one is greyed");
}

Test(uirender, a_button_text_is_centered)
{
    FontFile font;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            const kw::Entity b = uiNode(w, {.size = {100.0f, 40.0f}});

            w.add<kuge::UiButton>(b, kuge::UiButton{.text = "Play"});     // 32 x 12
        });
    });
    const auto& calls = fx.frame();

    Assert(calls.back().kind == Kind::Texture && calls.back().destination == kuge::Rect(34.0f, 14.0f, 32.0f, 12.0f),
        "in the middle: ((100 - 32) / 2, (40 - 12) / 2)");
}

Test(uirender, order_of_drawing)
{
    FontFile font;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            const kw::Entity panel = uiNode(w, {.size = {200.0f, 100.0f}});

            w.add<kuge::UiPanel>(panel, kuge::UiPanel{.borderWidth = 0.0f});
            const kw::Entity label = uiChild(w, panel, {});
            kuge::UiLabel text;

            text.text = "in the panel";
            w.add<kuge::UiLabel>(label, text);
        });
    });
    const auto& calls = fx.frame();

    AssertEq(calls.size(), 2, "the panel, then its text");
    Assert(calls[0].kind == Kind::Fill && calls[1].kind == Kind::Texture, "the panel is behind what it holds");
}

Test(uirender, the_ui_is_over_the_game)
{
    FontFile font;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            const kw::Entity sprite = w.create();
            kuge::Sprite s;

            s.size = {10.0f, 10.0f};
            w.add<kuge::Transform2D>(sprite, kuge::Transform2D{});
            w.add<kuge::Sprite>(sprite, s);
            const kw::Entity panel = uiNode(w, {.size = {50.0f, 50.0f}});

            w.add<kuge::UiPanel>(panel, kuge::UiPanel{.borderWidth = 0.0f});
        });
    });
    const auto& calls = fx.frame();

    AssertEq(calls.size(), 2, "a sprite and a panel");
    Assert(calls[0].destination.w == 10.0f && calls[1].destination.w == 50.0f, "the panel is drawn last: over the world");
}
