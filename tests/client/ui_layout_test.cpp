extern "C" {
    #include "kronklab/kronklab.h"
}
#include "ui_fixture.hpp"

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using kuge::Anchor;

    const kuge::Rect& rectOf(Fixture& fx, kw::Entity entity)
    {
        return fx.scene->world().getResource<kuge::UiLayoutResult>().rects.at(entity);
    }

    bool shown(Fixture& fx, kw::Entity entity)
    {
        return fx.scene->world().getResource<kuge::UiLayoutResult>().rects.count(entity) != 0;
    }
}

Test(uilayout, a_label_fits_its_text)
{
    FontFile font;
    kw::Entity label = 0, wide = 0, tall = 0;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            label = uiLabel(w, "Hello", {});
            wide = uiLabel(w, "one\ntwo lines", {});
            tall = uiLabel(w, "", {});
        });
    });

    Assert(sameRect(rectOf(fx, label), {0.0f, 0.0f, 40.0f, 12.0f}), "5 letters, one line");
    Assert(sameRect(rectOf(fx, wide), {0.0f, 0.0f, 72.0f, 24.0f}), "the longest line (\"two lines\": 9 letters), two lines");
    Assert(sameRect(rectOf(fx, tall), {0.0f, 0.0f, 0.0f, 12.0f}), "no text: no width, a line high");
}

Test(uilayout, anchors_on_the_screen)
{
    FontFile font;
    kw::Entity nodes[9] = {};
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            for (int i = 0; i < 9; ++i) {
                nodes[i] = uiNode(w, {.anchor = static_cast<Anchor>(i), .size = {100.0f, 50.0f}});
            }
        });
    });
    // The screen is 640 x 480, the node 100 x 50
    const float x[3] = {0.0f, 270.0f, 540.0f};
    const float y[3] = {0.0f, 215.0f, 430.0f};

    for (int i = 0; i < 9; ++i) {
        Assert(sameRect(rectOf(fx, nodes[i]), {x[i % 3], y[i / 3], 100.0f, 50.0f}), "anchor %d: got %f, %f", i, rectOf(fx, nodes[i]).x, rectOf(fx, nodes[i]).y);
    }
}

Test(uilayout, offset_moves_it)
{
    FontFile font;
    kw::Entity node = 0;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) { node = uiNode(w, {.anchor = Anchor::Center, .offset = {5.0f, -5.0f}, .size = {100.0f, 50.0f}}); });
    });

    Assert(sameRect(rectOf(fx, node), {275.0f, 210.0f, 100.0f, 50.0f}), "the centered node, moved by its offset");
}

Test(uilayout, child_anchored_in_parent)
{
    FontFile font;
    kw::Entity parent = 0, child = 0, grandchild = 0;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            parent = uiNode(w, {.anchor = Anchor::Center, .size = {200.0f, 100.0f}});
            child = uiChild(w, parent, {.anchor = Anchor::BottomRight, .size = {50.0f, 20.0f}});
            grandchild = uiChild(w, child, {.anchor = Anchor::Center, .size = {10.0f, 10.0f}});
        });
    });

    Assert(sameRect(rectOf(fx, parent), {220.0f, 190.0f, 200.0f, 100.0f}), "the parent");
    Assert(sameRect(rectOf(fx, child), {370.0f, 270.0f, 50.0f, 20.0f}), "in its bottom right corner");
    Assert(sameRect(rectOf(fx, grandchild), {390.0f, 275.0f, 10.0f, 10.0f}), "and one more level");
}

Test(uilayout, buttons_fit_their_text)
{
    FontFile font;
    kw::Entity button = 0;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) { button = uiButton(w, "Play", uiNode(w, {.size = {300.0f, 300.0f}})); });
    });

    Assert(sameRect(rectOf(fx, button), {0.0f, 0.0f, 52.0f, 32.0f}), "4 letters (32) + 10 of padding on each side, 12 + 20 high");
}

Test(uilayout, a_column)
{
    FontFile font;
    kw::Entity menu = 0, a = 0, play = 0, options = 0;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            menu = uiNode(w, {.anchor = Anchor::Center});
            w.add<kuge::UiStack>(menu, kuge::UiStack{.spacing = 4.0f, .padding = 8.0f});
            a = uiButton(w, "A", menu);
            play = uiButton(w, "Play", menu);
            options = uiButton(w, "Options", menu);
        });
    });

    // Widest is Options: 7 letters = 56 + 20 = 76. Width 76 + 16, height 3 x 32 + 2 x 4 + 16
    Assert(sameRect(rectOf(fx, menu), {274.0f, 180.0f, 92.0f, 120.0f}), "as big as its children, centered");
    Assert(sameRect(rectOf(fx, a), {282.0f, 188.0f, 76.0f, 32.0f}), "the first, stretched to the width");
    Assert(sameRect(rectOf(fx, play), {282.0f, 224.0f, 76.0f, 32.0f}), "the second, 4 pixels under");
    Assert(sameRect(rectOf(fx, options), {282.0f, 260.0f, 76.0f, 32.0f}), "the third");
}

Test(uilayout, a_row_and_its_alignment)
{
    FontFile font;
    kw::Entity rows[4] = {}, small[4] = {}, big[4] = {};
    const kuge::Align aligns[4] = {kuge::Align::Start, kuge::Align::Center, kuge::Align::End, kuge::Align::Stretch};
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            for (int i = 0; i < 4; ++i) {
                rows[i] = uiNode(w, {.offset = {0.0f, 100.0f * static_cast<float>(i)}, .size = {}});
                w.add<kuge::UiStack>(rows[i], kuge::UiStack{.direction = kuge::Direction::Horizontal, .spacing = 10.0f, .padding = 5.0f, .align = aligns[i]});
                small[i] = uiChild(w, rows[i], {.size = {20.0f, 10.0f}});
                big[i] = uiChild(w, rows[i], {.size = {30.0f, 40.0f}});
            }
        });
    });

    // The row is 5 + 20 + 10 + 30 + 5 = 70 wide, 5 + 40 + 5 = 50 high; its children go along it
    for (int i = 0; i < 4; ++i) {
        const float top = 100.0f * static_cast<float>(i);

        Assert(sameRect(rectOf(fx, rows[i]), {0.0f, top, 70.0f, 50.0f}), "row %d", i);
        Assert(rectOf(fx, small[i]).x == 5.0f && rectOf(fx, big[i]).x == 35.0f, "side by side, 10 apart");
    }
    // The rows start at 0, 100, 200 and 300
    AssertEq(rectOf(fx, small[0]).y, 5.0f, "start: at the top, under the padding");
    AssertEq(rectOf(fx, small[1]).y, 100.0f + 5.0f + (40.0f - 10.0f) / 2.0f, "center: half of what the bigger child leaves");
    AssertEq(rectOf(fx, small[2]).y, 200.0f + 5.0f + (40.0f - 10.0f), "end: at the bottom");
    Assert(rectOf(fx, small[3]).y == 305.0f && rectOf(fx, small[3]).h == 10.0f, "stretch: a child that has a size keeps it");
}

Test(uilayout, stretch_fills_what_is_free)
{
    FontFile font;
    kw::Entity column = 0, free = 0, fixed = 0;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            column = uiNode(w, {.size = {200.0f, 0.0f}});           // only its width is decided
            w.add<kuge::UiStack>(column, kuge::UiStack{.padding = 10.0f});
            free = uiButton(w, "Free", column);
            fixed = uiChild(w, column, {.size = {50.0f, 20.0f}});
        });
    });

    AssertEq(rectOf(fx, free).w, 180.0f, "a child with no width takes the room: 200 - 2 x 10");
    AssertEq(rectOf(fx, fixed).w, 50.0f, "one that has a width keeps it");
    AssertEq(rectOf(fx, column).h, 10.0f + 32.0f + 8.0f + 20.0f + 10.0f, "and the column is as high as what it holds");
}

Test(uilayout, hidden_nodes_are_gone)
{
    FontFile font;
    kw::Entity menu = 0, a = 0, hidden = 0, b = 0, inside = 0;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            menu = uiNode(w, {});
            w.add<kuge::UiStack>(menu, kuge::UiStack{.spacing = 4.0f, .padding = 0.0f});
            a = uiButton(w, "A", menu);
            hidden = uiButton(w, "B", menu);
            b = uiButton(w, "C", menu);
            inside = uiChild(w, hidden, {.size = {5.0f, 5.0f}});
            w.get<kuge::UiNode>(hidden).visible = false;
        });
    });

    Assert(!shown(fx, hidden) && !shown(fx, inside), "a hidden node and what is inside it");
    Assert(rectOf(fx, b).y == rectOf(fx, a).y + 32.0f + 4.0f, "it takes no room in the stack: the next one follows the first");
    fx.scene->world().get<kuge::UiNode>(hidden).visible = true;
    fx.frame();
    Assert(shown(fx, hidden) && shown(fx, inside), "shown again");
    Assert(rectOf(fx, b).y == rectOf(fx, a).y + 2.0f * (32.0f + 4.0f), "and it takes its place");
}

Test(uilayout, drawing_order)
{
    FontFile font;
    kw::Entity back = 0, parent = 0, child = 0, front = 0, low = 0;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            parent = uiNode(w, {.size = {100.0f, 100.0f}, .layer = 0});
            child = uiChild(w, parent, {.size = {10.0f, 10.0f}, .layer = 0});
            front = uiNode(w, {.size = {10.0f, 10.0f}, .layer = 5});
            back = uiNode(w, {.size = {10.0f, 10.0f}, .layer = -1});
            low = uiChild(w, front, {.size = {5.0f, 5.0f}, .layer = 0});
        });
    });
    const auto& order = fx.scene->world().getResource<kuge::UiLayoutResult>().drawOrder;
    auto at = [&order](kw::Entity e) { return std::find(order.begin(), order.end(), e) - order.begin(); };

    Assert(at(back) < at(parent), "a lower layer first");
    Assert(at(parent) < at(child), "a parent before what is inside it");
    Assert(at(child) < at(front), "a higher layer after");
    Assert(at(front) < at(low), "a child of a node of a high layer is drawn over it too, whatever its own layer");
}

Test(uilayout, broken_trees)
{
    FontFile font;
    kw::Entity orphan = 0, self = 0, a = 0, b = 0;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            orphan = uiNode(w, {.parent = 9999, .hasParent = true, .size = {10.0f, 10.0f}});   // its parent does not exist
            self = uiNode(w, {.size = {10.0f, 10.0f}});
            w.get<kuge::UiNode>(self).parent = self;
            w.get<kuge::UiNode>(self).hasParent = true;
            a = uiNode(w, {.size = {10.0f, 10.0f}});
            b = uiChild(w, a, {.size = {10.0f, 10.0f}});
            w.get<kuge::UiNode>(a).parent = b;                                                  // a and b are each other's parent
            w.get<kuge::UiNode>(a).hasParent = true;
        });
    });

    Assert(shown(fx, orphan), "without a parent that exists, it goes on the screen");
    Assert(shown(fx, self), "and so does one that is its own parent");
    Assert(!shown(fx, a) && !shown(fx, b), "a loop is not on the screen, and does not hang");
}

Test(uilayout, follows_the_screen)
{
    FontFile font;
    kw::Entity node = 0;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) { node = uiNode(w, {.anchor = Anchor::BottomRight, .size = {100.0f, 50.0f}}); });
    });

    Assert(sameRect(rectOf(fx, node), {540.0f, 430.0f, 100.0f, 50.0f}), "at 640 x 480");
    fx.dummy.renderer->resize({1000.0f, 800.0f});
    fx.frame();
    Assert(sameRect(rectOf(fx, node), {900.0f, 750.0f, 100.0f, 50.0f}), "the window grew: it is still in the corner");
}

Test(uilayout, without_a_font)
{
    Fixture fx([](TestScene& scene) {
        kuge::installUi(scene.setup(), uiActions());        // the theme has no font
        auto& w = scene.world();

        uiLabel(w, "no font", {});
        uiButton(w, "no font", uiNode(w, {.anchor = Anchor::Center}));
    });

    const auto& calls = fx.frame();

    AssertEq(fx.scene->world().getResource<kuge::UiLayoutResult>().rects.size(), 3, "everything has a place");
    AssertEq(calls.size(), 1, "the button is a box (only its text is missing), the label draws nothing: got %zu", calls.size());
    Assert(calls[0].kind == kuge::DummyRenderer::Call::Kind::Fill, "a plain box");
}
