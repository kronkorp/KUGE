extern "C" {
    #include "kronklab/kronklab.h"
}
#include "ui_fixture.hpp"
#include <vector>

// NOTE: kronklab test names are limited to 31 characters.
// The widgets that hold a value or a picture (check box, slider, scroll, image), on the dummy backend and font
// (a letter is 8 pixels wide, a line 12 high).

namespace
{
    using Kind = kuge::UiEvent::Kind;
    using Call = kuge::DummyRenderer::Call;

    // A column in the middle of the screen, with whatever `build` puts in it, and what is needed to play with it
    struct Widgets
    {
        FontFile                 font;
        kw::Entity               column = 0;
        std::unique_ptr<Fixture> fx;

        explicit Widgets(const std::function<void(kw::World&, kw::Entity)>& build, kuge::Vec2 screen = {640.0f, 480.0f})
        {
            fx = std::make_unique<Fixture>([&](TestScene& scene) {
                withUi(scene, font, [&](kw::World& w) {
                    column = uiNode(w, {.anchor = kuge::Anchor::Center});
                    w.add<kuge::UiStack>(column, kuge::UiStack{});
                    build(w, column);
                });
            }, screen);
            bindUiKeys(fx->client->input());
        }

        kw::World& world(void) { return fx->scene->world(); }
        const kuge::UiState& state(void) { return world().getResource<kuge::UiState>(); }
        const std::vector<kuge::UiEvent>& events(void) { return world().getResource<kuge::UiEvents>().list; }
        template<typename C> C& get(kw::Entity entity) { return world().get<C>(entity); }
        kuge::Rect rectOf(kw::Entity entity) { return world().getResource<kuge::UiLayoutResult>().rects.at(entity); }

        std::size_t count(Kind kind, kw::Entity entity)
        {
            return static_cast<std::size_t>(std::count_if(events().begin(), events().end(),
                [&](const auto& e) { return e.kind == kind && e.entity == entity; }));
        }

        const std::vector<Call>& tick(void) { return fx->frame(1.0 / 60.0); }
        void press(kuge::Key key) { fx->dummy.input->push(kuge::KeyEvent{key, true}); tick(); }
        void release(kuge::Key key) { fx->dummy.input->push(kuge::KeyEvent{key, false}); }
        void tap(kuge::Key key) { press(key); release(key); tick(); }
        void mouseTo(kuge::Vec2 at) { fx->dummy.input->push(kuge::MouseMoveEvent{at, {}}); }
        void mouseDown(kuge::Vec2 at) { fx->dummy.input->push(kuge::MouseButtonEvent{kuge::MouseButton::Left, true, at}); tick(); }
        void mouseUp(kuge::Vec2 at) { fx->dummy.input->push(kuge::MouseButtonEvent{kuge::MouseButton::Left, false, at}); tick(); }
        void click(kuge::Vec2 at) { mouseTo(at); mouseDown(at); mouseUp(at); }
        void wheel(float notches) { fx->dummy.input->push(kuge::MouseWheelEvent{{0.0f, notches}}); tick(); }
    };

    kw::Entity checkbox(kw::World& w, kw::Entity parent, const char* text, bool checked = false, bool enabled = true)
    {
        const kw::Entity box = uiChild(w, parent);

        w.add<kuge::UiCheckbox>(box, kuge::UiCheckbox{.text = text, .checked = checked, .enabled = enabled});
        return box;
    }
}

// -- Check box --------------------------------------------------------------------------------------------------
Test(uibox, accept_flips_it)
{
    kw::Entity box = 0;
    Widgets ui([&](kw::World& w, kw::Entity column) { box = checkbox(w, column, "Hurry mode"); });

    ui.tick();
    Assert(ui.state().hasFocus && ui.state().focus == box, "it takes the focus like a button");
    Assert(!ui.get<kuge::UiCheckbox>(box).checked, "not ticked at first");
    ui.press(kuge::Key::Enter);
    ui.release(kuge::Key::Enter);
    Assert(ui.get<kuge::UiCheckbox>(box).checked, "ticked by Enter");
    AssertEq(ui.count(Kind::Changed, box), 1, "and it says so");
    AssertEq(ui.count(Kind::Activated, box), 0, "it is not a press");
    ui.tick();
    AssertEq(ui.events().size(), 0, "nothing more while it is left alone");
    ui.press(kuge::Key::Enter);
    Assert(!ui.get<kuge::UiCheckbox>(box).checked, "unticked by the next press");
    AssertEq(ui.count(Kind::Changed, box), 1, "and it says so again");
}

Test(uibox, a_click_flips_it)
{
    kw::Entity first = 0;
    kw::Entity second = 0;
    Widgets ui([&](kw::World& w, kw::Entity column) {
        first = checkbox(w, column, "One");
        second = checkbox(w, column, "Two");
    });

    ui.tick();
    ui.click(ui.rectOf(second).center());
    Assert(ui.get<kuge::UiCheckbox>(second).checked, "the one under the mouse is ticked");
    Assert(!ui.get<kuge::UiCheckbox>(first).checked, "and not the other");
    Assert(ui.state().focus == second, "it has the focus");
    ui.click({5.0f, 5.0f});
    Assert(ui.get<kuge::UiCheckbox>(second).checked, "a click elsewhere changes nothing");
}

Test(uibox, a_disabled_one_stays)
{
    kw::Entity off = 0;
    kw::Entity on = 0;
    Widgets ui([&](kw::World& w, kw::Entity column) {
        off = checkbox(w, column, "Off", false, false);
        on = checkbox(w, column, "On");
    });

    ui.tick();
    Assert(ui.state().focus == on, "the focus skips it");
    ui.click(ui.rectOf(off).center());
    Assert(!ui.get<kuge::UiCheckbox>(off).checked, "a click does not tick it");
    AssertEq(ui.count(Kind::Changed, off), 0, "and says nothing");
}

Test(uibox, the_focus_goes_through)
{
    kw::Entity a = 0;
    kw::Entity b = 0;
    kw::Entity button = 0;
    Widgets ui([&](kw::World& w, kw::Entity column) {
        a = checkbox(w, column, "A");
        b = checkbox(w, column, "B");
        button = uiButton(w, "Go", column);
    });

    ui.tick();
    ui.tap(kuge::Key::Down);
    Assert(ui.state().focus == b, "down reaches the second");
    ui.tap(kuge::Key::Down);
    Assert(ui.state().focus == button, "and the button after it");
    ui.tap(kuge::Key::Tab);
    Assert(ui.state().focus == a, "Tab goes round");
    ui.tap(kuge::Key::Enter);
    Assert(ui.get<kuge::UiCheckbox>(a).checked && !ui.get<kuge::UiCheckbox>(b).checked, "the right one is ticked");
}

Test(uibox, it_is_drawn)
{
    kw::Entity box = 0;
    Widgets ui([&](kw::World& w, kw::Entity column) { box = checkbox(w, column, "Sound"); });
    const kuge::UiTheme theme;
    const auto marks = [&](const std::vector<Call>& calls) {
        return std::count_if(calls.begin(), calls.end(), [&](const Call& c) { return c.kind == Call::Kind::Fill && c.color == theme.checkMark && c.destination.w == 6.0f; });   // (the ring is that color too)
    };
    const auto before = ui.tick();
    const kuge::Rect rect = ui.rectOf(box);

    AssertEq(rect.h, 12.0f, "the box is one line high");
    AssertEq(rect.w, 12.0f + 8.0f + 40.0f, "a square, a gap and the text, got %f", rect.w);
    AssertEq(marks(before), 0, "no mark while it is not ticked");
    ui.get<kuge::UiCheckbox>(box).checked = true;
    const auto& after = ui.tick();

    AssertEq(marks(after), 1, "a mark inside the box once ticked");
    const auto texts = std::count_if(after.begin(), after.end(), [](const Call& c) { return c.kind == Call::Kind::Texture; });

    AssertEq(texts, 1, "and its text");
}

// -- Slider -------------------------------------------------------------------------------------------------------
namespace
{
    kw::Entity slider(kw::World& w, kw::Entity parent, kuge::UiSlider value = {})
    {
        const kw::Entity entity = uiChild(w, parent);

        w.add<kuge::UiSlider>(entity, value);
        return entity;
    }

    bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
}

Test(uislider, arrows_step_the_value)
{
    kw::Entity s = 0;
    Widgets ui([&](kw::World& w, kw::Entity column) { s = slider(w, column, {.value = 0.5f, .step = 0.25f}); });

    ui.tick();
    ui.tap(kuge::Key::Right);
    Assert(near(ui.get<kuge::UiSlider>(s).value, 0.75f), "right: up by one step");
    AssertEq(ui.count(Kind::Changed, s), 0, "(the event was for the tick of the press)");
    ui.press(kuge::Key::Right);
    ui.release(kuge::Key::Right);
    Assert(near(ui.get<kuge::UiSlider>(s).value, 1.0f), "up to the max");
    AssertEq(ui.count(Kind::Changed, s), 1, "and it says so");
    ui.press(kuge::Key::Right);
    ui.release(kuge::Key::Right);
    Assert(near(ui.get<kuge::UiSlider>(s).value, 1.0f), "never beyond it");
    AssertEq(ui.count(Kind::Changed, s), 0, "and it has nothing to say");
    ui.press(kuge::Key::Left);
    ui.release(kuge::Key::Left);
    Assert(near(ui.get<kuge::UiSlider>(s).value, 0.75f), "left: the other way");
    AssertEq(ui.count(Kind::Changed, s), 1, "and says so");
    for (int i = 0; i < 5; ++i) {
        ui.tap(kuge::Key::Left);
    }
    Assert(near(ui.get<kuge::UiSlider>(s).value, 0.0f), "down to the min and no further, got %f", ui.get<kuge::UiSlider>(s).value);
}

Test(uislider, left_and_right_keep_the_focus)
{
    kw::Entity s = 0;
    kw::Entity button = 0;
    Widgets ui([&](kw::World& w, kw::Entity column) {
        const kw::Entity row = uiChild(w, column);

        w.add<kuge::UiStack>(row, kuge::UiStack{.direction = kuge::Direction::Horizontal});
        s = slider(w, row);
        button = uiButton(w, "Reset", row);
    });

    ui.tick();
    Assert(ui.state().focus == s, "the slider has the focus");
    ui.tap(kuge::Key::Right);
    Assert(ui.state().focus == s, "right moves its value, not the focus, though a button is on its right");
    Assert(ui.get<kuge::UiSlider>(s).value > 0.0f, "");
    ui.tap(kuge::Key::Tab);
    Assert(ui.state().focus == button, "Tab leaves it");
    ui.tap(kuge::Key::Left);
    Assert(ui.state().focus == s, "from a button left still moves the focus to it");
}

Test(uislider, a_click_sets_the_value)
{
    kw::Entity s = 0;
    Widgets ui([&](kw::World& w, kw::Entity column) { s = slider(w, column); });

    ui.tick();
    const kuge::Rect rect = ui.rectOf(s);

    ui.click({rect.x + 0.75f * rect.w, rect.center().y});
    Assert(near(ui.get<kuge::UiSlider>(s).value, 0.75f), "three quarters: 0.75, got %f", ui.get<kuge::UiSlider>(s).value);
    ui.click({rect.x + 0.25f * rect.w, rect.center().y});
    Assert(near(ui.get<kuge::UiSlider>(s).value, 0.25f), "a quarter");
    ui.click({5.0f, 5.0f});
    Assert(near(ui.get<kuge::UiSlider>(s).value, 0.25f), "a click elsewhere leaves it");
}

Test(uislider, a_range_that_is_not_0_to_1)
{
    kw::Entity s = 0;
    Widgets ui([&](kw::World& w, kw::Entity column) { s = slider(w, column, {.value = 10.0f, .min = 10.0f, .max = 20.0f}); });

    ui.tick();
    const kuge::Rect rect = ui.rectOf(s);

    ui.click({rect.x + 0.5f * rect.w, rect.center().y});
    Assert(near(ui.get<kuge::UiSlider>(s).value, 15.0f), "the middle of 10 to 20, got %f", ui.get<kuge::UiSlider>(s).value);
}

Test(uislider, dragging_follows_the_mouse)
{
    kw::Entity s = 0;
    Widgets ui([&](kw::World& w, kw::Entity column) { s = slider(w, column, {.value = 0.5f}); });

    ui.tick();
    const kuge::Rect rect = ui.rectOf(s);
    const float y = rect.center().y;

    ui.mouseTo({rect.x + 0.5f * rect.w, y});
    ui.mouseDown({rect.x + 0.5f * rect.w, y});
    Assert(ui.get<kuge::UiSlider>(s).dragging, "the mouse holds the handle");
    ui.mouseTo({rect.x + 0.8f * rect.w, y});
    ui.tick();
    Assert(near(ui.get<kuge::UiSlider>(s).value, 0.8f), "it follows the mouse, got %f", ui.get<kuge::UiSlider>(s).value);
    AssertEq(ui.count(Kind::Changed, s), 1, "one Changed for the tick");
    ui.mouseTo({rect.x - 100.0f, y + 300.0f});
    ui.tick();
    AssertEq(ui.get<kuge::UiSlider>(s).value, 0.0f, "to the left of the slider (and far from it): the min");
    ui.mouseTo({rect.right() + 100.0f, y});
    ui.tick();
    AssertEq(ui.get<kuge::UiSlider>(s).value, 1.0f, "to the right: the max");
    ui.mouseUp({rect.right() + 100.0f, y});
    Assert(!ui.get<kuge::UiSlider>(s).dragging, "let go");
    ui.mouseTo({rect.x + 0.3f * rect.w, y});
    ui.tick();
    AssertEq(ui.get<kuge::UiSlider>(s).value, 1.0f, "and it stays where it was left");
}

Test(uislider, a_disabled_one_stays)
{
    kw::Entity off = 0;
    kw::Entity on = 0;
    Widgets ui([&](kw::World& w, kw::Entity column) {
        off = slider(w, column, {.value = 0.5f, .enabled = false});
        on = slider(w, column, {.value = 0.5f});
    });

    ui.tick();
    const kuge::Rect rect = ui.rectOf(off);

    Assert(ui.state().focus == on, "the focus skips it");
    ui.click({rect.x + 0.9f * rect.w, rect.center().y});
    AssertEq(ui.get<kuge::UiSlider>(off).value, 0.5f, "a click does not move it");
    AssertEq(ui.get<kuge::UiSlider>(on).value, 0.5f, "nor the other");
    ui.get<kuge::UiSlider>(off).enabled = true;
    ui.tick();
    ui.tap(kuge::Key::Up);
    Assert(ui.state().focus == off, "up reaches it once it is enabled");
    ui.tap(kuge::Key::Right);
    Assert(ui.get<kuge::UiSlider>(off).value > 0.5f, "and right moves it");
}

Test(uislider, the_handle_is_drawn)
{
    kw::Entity s = 0;
    Widgets ui([&](kw::World& w, kw::Entity column) { s = slider(w, column, {.value = 0.5f}); });
    const auto& calls = ui.tick();
    const kuge::Rect rect = ui.rectOf(s);
    const kuge::UiTheme theme;
    const Call* handle = nullptr;
    const Call* filled = nullptr;

    for (const Call& call : calls) {
        if (call.kind == Call::Kind::Fill && call.color == theme.buttonHover && call.destination.w == 12.0f) { handle = &call; }
        if (call.kind == Call::Kind::Fill && call.color == theme.sliderFill) { filled = &call; }
    }
    Assert(handle != nullptr, "a handle");
    AssertEq(handle->destination.center().x, rect.center().x, "in the middle of the track at 0.5");
    Assert(filled != nullptr, "and the track up to it");
    AssertEq(filled->destination.w, rect.w / 2.0f, "half of it");
}

// -- Scroll: a stack longer than its node, and the mouse wheel -----------------------------------------------------
namespace
{
    // A node of 100 x 100 at the top left, with `count` children of 100 x 30 (no spacing, no padding): 30 x count long
    struct ScrollList
    {
        kw::Entity              scroll = 0;
        std::vector<kw::Entity> items;
    };

    ScrollList scrollList(kw::World& w, int count, bool buttons)
    {
        ScrollList list;

        list.scroll = uiNode(w, {.size = {100.0f, 100.0f}});
        w.add<kuge::UiStack>(list.scroll, kuge::UiStack{.spacing = 0.0f, .padding = 0.0f});
        w.add<kuge::UiScroll>(list.scroll, kuge::UiScroll{});
        for (int i = 0; i < count; ++i) {
            const kw::Entity item = uiChild(w, list.scroll, {.size = {100.0f, 30.0f}});

            if (buttons) {
                w.add<kuge::UiButton>(item, kuge::UiButton{.text = "x"});
            } else {
                w.add<kuge::UiPanel>(item, kuge::UiPanel{.borderWidth = 0.0f});
            }
            list.items.push_back(item);
        }
        return list;
    }

    // The fills of the items that were drawn, by the top of their rectangle
    std::vector<float> drawnTops(const std::vector<Call>& calls)
    {
        std::vector<float> tops;

        for (const Call& call : calls) {
            tops.push_back(call.destination.y);
        }
        return tops;
    }
}

Test(uiscroll, only_what_is_inside)
{
    ScrollList list;
    Widgets ui([&](kw::World& w, kw::Entity) { list = scrollList(w, 10, false); });
    const auto& calls = ui.tick();

    AssertEq(calls.size(), 4, "three items and the edge of a fourth, got %zu", calls.size());
    for (const Call& call : calls) {
        Assert(call.clip.has_value() && *call.clip == kuge::Rect(0.0f, 0.0f, 100.0f, 100.0f), "cut to the node");
    }
    Assert(drawnTops(calls) == std::vector<float>({0.0f, 30.0f, 60.0f, 90.0f}), "the first four, one below the other");
    const auto& layout = ui.world().getResource<kuge::UiLayoutResult>();

    AssertEq(layout.rects.count(list.items[9]), 1, "the ones that are out are placed all the same");
    AssertEq(ui.get<kuge::UiScroll>(list.scroll).extent, 300.0f, "the whole length is known");
}

Test(uiscroll, the_wheel_moves_it)
{
    ScrollList list;
    Widgets ui([&](kw::World& w, kw::Entity) { list = scrollList(w, 10, false); });
    auto scroll = [&]() -> kuge::UiScroll& { return ui.get<kuge::UiScroll>(list.scroll); };

    ui.mouseTo({50.0f, 50.0f});
    ui.tick();
    ui.wheel(-1.0f);
    AssertEq(scroll().offset, 40.0f, "a notch down: one notch of content");
    Assert(drawnTops(ui.tick()).front() == -10.0f, "and the content went up by it: the second item, cut at its top, is the first seen");
    ui.wheel(1.0f);
    AssertEq(scroll().offset, 0.0f, "a notch up: back");
    ui.wheel(5.0f);
    AssertEq(scroll().offset, 0.0f, "never below 0");
    ui.wheel(-100.0f);
    AssertEq(scroll().offset, 200.0f, "never above the content minus the node: 300 - 100");
    const auto& calls = ui.tick();

    AssertEq(calls.size(), 4, "the last item and a part of the one before, got %zu", calls.size());
    Assert(drawnTops(calls) == std::vector<float>({-20.0f, 10.0f, 40.0f, 70.0f}), "the end of the content is at the bottom of the node");
}

Test(uiscroll, the_wheel_must_be_over_it)
{
    ScrollList list;
    Widgets ui([&](kw::World& w, kw::Entity) { list = scrollList(w, 10, false); });

    ui.mouseTo({300.0f, 300.0f});
    ui.tick();
    ui.wheel(-1.0f);
    AssertEq(ui.get<kuge::UiScroll>(list.scroll).offset, 0.0f, "the mouse is elsewhere: nothing moves");
}

Test(uiscroll, short_content_stays)
{
    ScrollList list;
    Widgets ui([&](kw::World& w, kw::Entity) { list = scrollList(w, 2, false); });

    ui.mouseTo({50.0f, 50.0f});
    ui.tick();
    ui.wheel(-3.0f);
    AssertEq(ui.get<kuge::UiScroll>(list.scroll).offset, 0.0f, "nothing to scroll to");
    ui.get<kuge::UiScroll>(list.scroll).offset = 75.0f;   // (a game that sets it wrong)
    ui.tick();
    AssertEq(ui.get<kuge::UiScroll>(list.scroll).offset, 0.0f, "the layout puts it back in range");
}

Test(uiscroll, the_focus_comes_into_view)
{
    ScrollList list;
    Widgets ui([&](kw::World& w, kw::Entity) { list = scrollList(w, 10, true); });
    auto scroll = [&]() -> kuge::UiScroll& { return ui.get<kuge::UiScroll>(list.scroll); };

    ui.tick();
    ui.tap(kuge::Key::Down);
    ui.tap(kuge::Key::Down);
    AssertEq(scroll().offset, 0.0f, "the third button is inside: nothing moves");
    ui.tap(kuge::Key::Down);
    ui.tap(kuge::Key::Down);
    Assert(ui.state().focus == list.items[4], "the fifth button has the focus");
    AssertEq(scroll().offset, 50.0f, "scrolled just enough to show its bottom (150 - 100)");
    for (int i = 0; i < 5; ++i) {
        ui.tap(kuge::Key::Down);
    }
    AssertEq(scroll().offset, 200.0f, "down to the last one");
    for (int i = 0; i < 9; ++i) {
        ui.tap(kuge::Key::Up);
    }
    AssertEq(scroll().offset, 0.0f, "and back to the first");
}

Test(uiscroll, wheel_then_arrow)
{
    ScrollList list;
    Widgets ui([&](kw::World& w, kw::Entity) { list = scrollList(w, 10, true); });

    ui.mouseTo({50.0f, 50.0f});
    ui.tick();
    ui.wheel(-1.0f);
    ui.tick();
    AssertEq(ui.get<kuge::UiScroll>(list.scroll).offset, 40.0f, "the wheel moved it, and the focus did not pull it back");
}

Test(uiscroll, what_is_cut_is_not_hovered)
{
    ScrollList list;
    Widgets ui([&](kw::World& w, kw::Entity) { list = scrollList(w, 10, true); });

    ui.tick();
    ui.mouseTo({50.0f, 110.0f});   // (the fourth button goes from 90 to 120: below the node it is not seen)
    ui.tick();
    Assert(!ui.get<kuge::UiButton>(list.items[3]).hovered, "not under the mouse where it is cut");
    ui.mouseTo({50.0f, 95.0f});
    ui.tick();
    Assert(ui.get<kuge::UiButton>(list.items[3]).hovered, "under it where it shows");
}

// -- Image --------------------------------------------------------------------------------------------------------
namespace
{
    std::shared_ptr<kuge::Texture> picture(kw::World& w, int width, int height)
    {
        const std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width * height * 4), 255);

        return kuge::Texture::fromPixels(*w.getResource<kuge::Ref<kuge::IRenderer2D>>(), width, height, pixels);
    }

    std::vector<Call> textureCalls(const std::vector<Call>& calls)
    {
        std::vector<Call> found;

        std::copy_if(calls.begin(), calls.end(), std::back_inserter(found), [](const Call& c) { return c.kind == Call::Kind::Texture; });
        return found;
    }
}

Test(uiimage, drawn_in_its_node)
{
    kuge::TextureId id = 0;   // (the texture itself lives in the world: it must not outlive the renderer)
    FontFile font;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            const auto texture = picture(w, 16, 8);

            id = texture->id();
            w.add<kuge::UiImage>(uiNode(w, {.offset = {10.0f, 20.0f}, .size = {40.0f, 40.0f}}),
                kuge::UiImage{.texture = texture, .tint = {9, 8, 7, 255}});
        });
    });
    const auto& calls = fx.frame();

    AssertEq(calls.size(), 1, "one picture");
    Assert(calls[0].kind == Call::Kind::Texture && calls[0].texture.texture == id, "of the texture");
    Assert(calls[0].destination == kuge::Rect(10.0f, 20.0f, 40.0f, 40.0f), "in the rectangle of the node");
    Assert(calls[0].texture.tint == kuge::Color(9, 8, 7, 255), "with its tint");
    AssertEq(calls[0].texture.source.w, 0.0f, "all of the texture");
}

Test(uiimage, takes_the_size_it_has)
{
    FontFile font;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            const auto texture = picture(w, 16, 8);

            w.add<kuge::UiImage>(uiNode(w, {}), kuge::UiImage{.texture = texture});
            w.add<kuge::UiImage>(uiNode(w, {.offset = {100.0f, 0.0f}}), kuge::UiImage{.texture = texture, .source = {4.0f, 0.0f, 6.0f, 5.0f}});
        });
    });
    const auto calls = textureCalls(fx.frame());

    AssertEq(calls.size(), 2, "two pictures");
    Assert(calls[0].destination == kuge::Rect(0.0f, 0.0f, 16.0f, 8.0f), "the size of the texture");
    Assert(calls[1].destination == kuge::Rect(100.0f, 0.0f, 6.0f, 5.0f), "or of the part of it that is shown");
    Assert(calls[1].texture.source == kuge::Rect(4.0f, 0.0f, 6.0f, 5.0f), "which is what is drawn");
}

Test(uiimage, in_a_stack_and_over_a_panel)
{
    FontFile font;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            const auto texture = picture(w, 20, 10);

            const kw::Entity column = uiNode(w, {.anchor = kuge::Anchor::TopLeft});

            w.add<kuge::UiStack>(column, kuge::UiStack{.spacing = 0.0f, .padding = 0.0f});
            w.add<kuge::UiImage>(uiChild(w, column), kuge::UiImage{.texture = texture});
            w.add<kuge::UiImage>(uiChild(w, column), kuge::UiImage{.texture = texture});
            // A panel of a higher layer over the first picture: it covers it, so it is drawn after it
            const kw::Entity cover = uiNode(w, {.size = {20.0f, 10.0f}, .layer = 5});

            w.add<kuge::UiPanel>(cover, kuge::UiPanel{.borderWidth = 0.0f});
        });
    });
    const auto& calls = fx.frame();

    AssertEq(calls.size(), 3, "two pictures and a panel, got %zu", calls.size());
    Assert(calls[0].kind == Call::Kind::Texture && calls[1].kind == Call::Kind::Texture && calls[2].kind == Call::Kind::Fill,
        "the panel of the higher layer is drawn last: it covers the pictures");
    Assert(calls[0].destination == kuge::Rect(0.0f, 0.0f, 20.0f, 10.0f) && calls[1].destination == kuge::Rect(0.0f, 10.0f, 20.0f, 10.0f),
        "the stack puts them one under the other");
}

Test(uiimage, without_a_texture)
{
    FontFile font;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            w.add<kuge::UiImage>(uiNode(w, {.size = {40.0f, 40.0f}}), kuge::UiImage{});
            w.add<kuge::UiImage>(uiNode(w, {}), kuge::UiImage{});
        });
    });

    AssertEq(fx.frame().size(), 0, "nothing to draw, and nothing breaks");
}

// -- Scale: an interface built for one size, drawn at another ------------------------------------------------------
namespace
{
    // A button of 200 x 40 at (10, 20) and a label, built the same whatever the scale
    struct Scaled
    {
        FontFile                 font;
        kw::Entity               button = 0;
        kw::Entity               label = 0;
        std::unique_ptr<Fixture> fx;

        explicit Scaled(float scale)
        {
            fx = std::make_unique<Fixture>([&](TestScene& scene) {
                withUi(scene, font, [&](kw::World& w) {
                    w.getResource<kuge::UiScale>().value = scale;
                    button = uiNode(w, {.offset = {10.0f, 20.0f}, .size = {200.0f, 40.0f}});
                    w.add<kuge::UiButton>(button, kuge::UiButton{.text = "Play"});
                    label = uiLabel(w, "Score", {.offset = {0.0f, 100.0f}});
                });
            });
            bindUiKeys(fx->client->input());
        }

        kw::World& world(void) { return fx->scene->world(); }
        kuge::Rect rectOf(kw::Entity entity) { return world().getResource<kuge::UiLayoutResult>().rects.at(entity); }
        const std::vector<Call>& tick(void) { return fx->frame(1.0 / 60.0); }
    };
}

Test(uiscale, rectangles_follow_the_scale)
{
    Scaled one(1.0f);
    Scaled two(2.0f);

    one.tick();
    two.tick();
    Assert(sameRect(one.rectOf(one.button), {10.0f, 20.0f, 200.0f, 40.0f}), "at 1: as built");
    Assert(sameRect(two.rectOf(two.button), {20.0f, 40.0f, 400.0f, 80.0f}), "at 2: place and size doubled");
    const auto labelOne = one.rectOf(one.label);
    const auto labelTwo = two.rectOf(two.label);

    AssertEq(labelOne.w, 40.0f, "a text of 5 letters at 1");
    AssertEq(labelTwo.w, 80.0f, "twice as wide at 2: the font is made again, not stretched");
    AssertEq(labelTwo.h, 24.0f, "and twice as high");
    AssertEq(labelTwo.y, 200.0f, "at the doubled place");
}

Test(uiscale, components_keep_their_numbers)
{
    Scaled ui(2.0f);

    ui.tick();
    ui.tick();
    const auto& node = ui.world().get<kuge::UiNode>(ui.button);

    Assert(node.size == kuge::Vec2(200.0f, 40.0f) && node.offset == kuge::Vec2(10.0f, 20.0f), "the node is as it was built");
}

Test(uiscale, text_is_drawn_bigger)
{
    Scaled ui(2.0f);
    const auto texts = textureCalls(ui.tick());

    AssertEq(texts.size(), 2, "the button's text and the label");
    Assert(texts[0].destination.w == 64.0f && texts[0].destination.h == 24.0f, "\"Play\" at twice its size");
    Assert(texts[1].destination.w == 80.0f, "\"Score\" too");
}

Test(uiscale, a_click_at_the_scaled_place)
{
    Scaled ui(2.0f);

    ui.tick();
    const kuge::Vec2 inside{20.0f + 390.0f, 40.0f + 70.0f};   // in the doubled button, outside the button as built
    auto& input = *ui.fx->dummy.input;

    input.push(kuge::MouseMoveEvent{inside, {}});
    input.push(kuge::MouseButtonEvent{kuge::MouseButton::Left, true, inside});
    ui.tick();
    const auto& events = ui.world().getResource<kuge::UiEvents>().list;

    Assert(std::any_of(events.begin(), events.end(), [&](const auto& e) { return e.kind == Kind::Activated && e.entity == ui.button; }),
        "it presses the button");
}

Test(uiscale, stacks_and_themes_scale)
{
    kw::Entity first = 0;
    kw::Entity second = 0;
    kw::Entity field = 0;
    kw::Entity column = 0;
    FontFile font;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            w.getResource<kuge::UiScale>().value = 2.0f;
            column = uiNode(w, {});
            w.add<kuge::UiStack>(column, kuge::UiStack{.spacing = 4.0f, .padding = 8.0f, .align = kuge::Align::Start});
            first = uiButton(w, "OK", column);                       // no size: its text and the theme's padding
            second = uiChild(w, column, {.size = {50.0f, 20.0f}});
            field = uiChild(w, column);
            w.add<kuge::UiTextField>(field, kuge::UiTextField{.columns = 4});
        });
    });
    fx.frame(1.0 / 60.0);
    const auto rect = [&](kw::Entity e) { return fx.scene->world().getResource<kuge::UiLayoutResult>().rects.at(e); };

    Assert(sameRect(rect(first), {16.0f, 16.0f, 2.0f * 16.0f + 2.0f * 20.0f, 2.0f * 12.0f + 2.0f * 20.0f}),
        "the padding of the stack (8 -> 16), and a button: its text at 2x (32 x 24) and the theme's padding (10 -> 20)");
    Assert(sameRect(rect(second), {16.0f, 16.0f + 64.0f + 8.0f, 100.0f, 40.0f}), "the spacing (4 -> 8) and a size (50 x 20 -> 100 x 40)");
    AssertEq(rect(field).w, 4.0f * 16.0f + 40.0f, "a text field: four letters of 16 and the padding");
}

Test(uiscale, a_bad_scale_is_one)
{
    Scaled ui(1.0f);

    ui.tick();
    ui.world().getResource<kuge::UiScale>().value = 3.0f;
    ui.tick();
    AssertEq(ui.rectOf(ui.button).w, 600.0f, "a scale that changes is taken at the next frame");
    ui.world().getResource<kuge::UiScale>().value = 0.0f;
    ui.tick();
    AssertEq(ui.rectOf(ui.button).w, 200.0f, "zero is one");
    ui.world().getResource<kuge::UiScale>().value = std::nanf("");
    ui.tick();
    AssertEq(ui.rectOf(ui.button).w, 200.0f, "a number that is not one is one");
    ui.world().getResource<kuge::UiScale>().value = -2.0f;
    ui.tick();
    AssertEq(ui.rectOf(ui.button).w, 200.0f, "and so is a negative one");
}

Test(uiscale, the_scale_of_a_window)
{
    AssertEq(kuge::uiScaleFor({1280.0f, 720.0f}), 1.0f, "the size it was built for");
    AssertEq(kuge::uiScaleFor({2560.0f, 1440.0f}), 2.0f, "twice");
    AssertEq(kuge::uiScaleFor({1920.0f, 1080.0f}), 1.5f, "full HD");
    AssertEq(kuge::uiScaleFor({1280.0f, 1440.0f}), 1.0f, "a tall window: the smaller ratio, so nothing is cut");
    AssertEq(kuge::uiScaleFor({640.0f, 360.0f}, {320.0f, 180.0f}), 2.0f, "for another base");
    AssertEq(kuge::uiScaleFor({0.0f, 0.0f}), 1.0f, "a window without a size (minimised): one");
    const kuge::UiScale scale{.value = 2.0f};

    Assert(scale.toUi({100.0f, 50.0f}) == kuge::Vec2(50.0f, 25.0f) && scale.toScreen({50.0f, 25.0f}) == kuge::Vec2(100.0f, 50.0f), "to the UI and back");
}

Test(uiscale, a_font_is_made_once)
{
    kuge::DummyFont font;
    const auto big = font.scaled(2.0f);

    Assert(big && big.get() == font.scaled(2.0f).get(), "asking again gives the same one");
    Assert(big->lineHeight() == 24 && big->measure("ab").x == 32.0f, "twice as big");
    Assert(font.scaled(3.0f).get() != big.get(), "another size, another font");
    Assert(font.scaled(0.0f) == nullptr && font.scaled(-1.0f) == nullptr, "no font at no size");
}

Test(uiscale, a_scroll_in_ui_units)
{
    ScrollList list;
    Widgets ui([&](kw::World& w, kw::Entity) {
        w.getResource<kuge::UiScale>().value = 2.0f;
        list = scrollList(w, 10, false);
    });

    ui.mouseTo({100.0f, 100.0f});   // (inside the node, which is 200 x 200 on the screen)
    ui.tick();
    AssertEq(ui.get<kuge::UiScroll>(list.scroll).extent, 300.0f, "the length is in the units it was built in");
    ui.wheel(-1.0f);
    AssertEq(ui.get<kuge::UiScroll>(list.scroll).offset, 40.0f, "a notch is 40 of them");
    Assert(drawnTops(ui.tick()).front() == -20.0f, "which are 80 pixels: the second item (60 .. 120) is cut at -20");
    ui.wheel(-100.0f);
    AssertEq(ui.get<kuge::UiScroll>(list.scroll).offset, 200.0f, "the end is 300 - 100 as at scale 1");
}
