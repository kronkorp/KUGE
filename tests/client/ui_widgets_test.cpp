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
