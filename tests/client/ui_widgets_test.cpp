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
