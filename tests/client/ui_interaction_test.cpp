extern "C" {
    #include "kronklab/kronklab.h"
}
#include "ui_fixture.hpp"
#include <vector>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using Kind = kuge::UiEvent::Kind;

    // A menu of buttons in a column, and what is needed to play with it
    struct Menu
    {
        FontFile                 font;
        std::vector<kw::Entity>  buttons;
        kw::Entity               column = 0;
        std::unique_ptr<Fixture> fx;

        explicit Menu(std::vector<const char*> names = {"Play", "Options", "Quit"}, std::vector<bool> enabled = {})
        {
            fx = std::make_unique<Fixture>([&](TestScene& scene) {
                withUi(scene, font, [&](kw::World& w) {
                    column = uiNode(w, {.anchor = kuge::Anchor::Center});
                    w.add<kuge::UiStack>(column, kuge::UiStack{});
                    for (std::size_t i = 0; i < names.size(); ++i) {
                        buttons.push_back(uiButton(w, names[i], column, enabled.empty() || enabled[i]));
                    }
                });
            });
            bindUiKeys(fx->client->input());
        }

        kw::World& world(void) { return fx->scene->world(); }
        const kuge::UiState& state(void) { return world().getResource<kuge::UiState>(); }
        const std::vector<kuge::UiEvent>& events(void) { return world().getResource<kuge::UiEvents>().list; }
        kuge::UiButton& button(std::size_t i) { return world().get<kuge::UiButton>(buttons[i]); }

        void tick(void) { fx->frame(1.0 / 60.0); }
        void press(kuge::Key key) { fx->dummy.input->push(kuge::KeyEvent{key, true}); tick(); }
        void release(kuge::Key key) { fx->dummy.input->push(kuge::KeyEvent{key, false}); }
        void mouseTo(float x, float y) { fx->dummy.input->push(kuge::MouseMoveEvent{{x, y}, {}}); }
        void clickAt(float x, float y)
        {
            fx->dummy.input->push(kuge::MouseButtonEvent{kuge::MouseButton::Left, true, {x, y}});
        }

        // The center of a button on the screen
        kuge::Vec2 centerOf(std::size_t i)
        {
            return world().getResource<kuge::UiLayoutResult>().rects.at(buttons[i]).center();
        }

        bool focusOn(std::size_t i) { return state().hasFocus && state().focus == buttons[i]; }
    };
}

Test(uiinput, a_menu_starts_selected)
{
    Menu menu;

    menu.tick();
    Assert(menu.focusOn(0), "the first button has the focus");
    AssertEq(menu.events().size(), 1, "and says so");
    Assert(menu.events()[0].kind == Kind::Focused && menu.events()[0].entity == menu.buttons[0], "with a Focused event");
    Assert(menu.button(0).focused && !menu.button(1).focused, "the button knows");
    menu.tick();
    AssertEq(menu.events().size(), 0, "events are those of the last tick only");
}

Test(uiinput, up_and_down)
{
    Menu menu;

    menu.tick();
    menu.press(kuge::Key::Down);
    Assert(menu.focusOn(1), "down: the second");
    Assert(menu.events().size() == 1 && menu.events()[0].kind == Kind::Focused && menu.events()[0].entity == menu.buttons[1], "a Focused event");
    menu.release(kuge::Key::Down);
    menu.press(kuge::Key::Down);
    Assert(menu.focusOn(2), "the third");
    menu.release(kuge::Key::Down);
    menu.press(kuge::Key::Down);
    Assert(menu.focusOn(2) && menu.events().empty(), "at the end it stays, and says nothing");
    menu.release(kuge::Key::Down);
    menu.press(kuge::Key::Up);
    menu.release(kuge::Key::Up);
    menu.press(kuge::Key::Up);
    Assert(menu.focusOn(0), "and back up");
    menu.release(kuge::Key::Up);
    menu.press(kuge::Key::Up);
    Assert(menu.focusOn(0), "at the top it stays");
    menu.press(kuge::Key::Left);
    Assert(menu.focusOn(0), "nothing to the left in a column");
}

Test(uiinput, holding_a_key_moves_once)
{
    Menu menu;

    menu.tick();
    menu.press(kuge::Key::Down);
    menu.tick();
    menu.tick();
    menu.tick();
    Assert(menu.focusOn(1), "a held key moves the focus once, not at each tick");
}

Test(uiinput, accept_presses_the_button)
{
    Menu menu;

    menu.tick();
    menu.press(kuge::Key::Down);
    menu.release(kuge::Key::Down);
    menu.press(kuge::Key::Enter);
    AssertEq(menu.events().size(), 1, "one event");
    Assert(menu.events()[0].kind == Kind::Activated && menu.events()[0].entity == menu.buttons[1] && menu.events()[0].hasEntity,
        "the second button was pressed");
    Assert(menu.button(1).pressed, "and it looks pressed while the key is held");
    menu.tick();
    AssertEq(menu.events().size(), 0, "held: it is not pressed again");
    Assert(menu.button(1).pressed, "still down");
    menu.release(kuge::Key::Enter);
    menu.tick();
    Assert(!menu.button(1).pressed, "released");
}

Test(uiinput, disabled_buttons_are_skipped)
{
    Menu menu({"A", "B", "C", "D"}, {true, false, false, true});

    menu.tick();
    menu.press(kuge::Key::Down);
    Assert(menu.focusOn(3), "from the first to the last: the disabled ones are passed");
    menu.release(kuge::Key::Down);
    menu.press(kuge::Key::Up);
    Assert(menu.focusOn(0), "and back");
    menu.world().get<kuge::UiButton>(menu.buttons[1]).enabled = true;
    menu.release(kuge::Key::Up);
    menu.press(kuge::Key::Down);
    Assert(menu.focusOn(1), "one that gets enabled can be reached");
}

Test(uiinput, a_grid)
{
    FontFile font;
    std::vector<kw::Entity> cell;        // 2 x 2, by row
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            // Four buttons of 40 x 20, at the corners of a square
            const kuge::Vec2 places[4] = {{100.0f, 100.0f}, {300.0f, 100.0f}, {100.0f, 250.0f}, {300.0f, 250.0f}};

            for (const auto& at : places) {
                const kw::Entity b = uiNode(w, {.offset = at, .size = {40.0f, 20.0f}});

                w.add<kuge::UiButton>(b, kuge::UiButton{.text = "x"});
                cell.push_back(b);
            }
        });
    });
    const auto& state = fx.scene->world().getResource<kuge::UiState>();
    auto press = [&](kuge::Key key) { fx.dummy.input->push(kuge::KeyEvent{key, true}); fx.frame(1.0 / 60.0); fx.dummy.input->push(kuge::KeyEvent{key, false}); };
    auto on = [&](int i) { return state.hasFocus && state.focus == cell[static_cast<std::size_t>(i)]; };

    bindUiKeys(fx.client->input());
    fx.frame(1.0 / 60.0);
    Assert(on(0), "the top left one first");
    press(kuge::Key::Right);
    Assert(on(1), "right: the one beside it");
    press(kuge::Key::Down);
    Assert(on(3), "down: the one below");
    press(kuge::Key::Left);
    Assert(on(2), "left");
    press(kuge::Key::Up);
    Assert(on(0), "up");
    press(kuge::Key::Left);
    Assert(on(0), "nothing further left: it stays");
}

Test(uiinput, neighbours_by_place)
{
    FontFile font;
    std::vector<kw::Entity> b;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            // One at the top left, a near one to its right and a far one to its right, but lower
            const kuge::Vec2 places[3] = {{100.0f, 100.0f}, {200.0f, 100.0f}, {200.0f, 400.0f}};

            for (const auto& at : places) {
                const kw::Entity e = uiNode(w, {.offset = at, .size = {40.0f, 20.0f}});

                w.add<kuge::UiButton>(e, kuge::UiButton{.text = "x"});
                b.push_back(e);
            }
        });
    });
    const auto& state = fx.scene->world().getResource<kuge::UiState>();

    bindUiKeys(fx.client->input());
    fx.frame(1.0 / 60.0);
    fx.dummy.input->push(kuge::KeyEvent{kuge::Key::Right, true});
    fx.frame(1.0 / 60.0);
    Assert(state.focus == b[1], "right goes to the one that is in line, not the one far below");
}

Test(uiinput, mouse_hovers_and_takes_focus)
{
    Menu menu;

    menu.tick();
    Assert(menu.focusOn(0), "the first is focused");
    const auto target = menu.centerOf(2);
    menu.mouseTo(target.x, target.y);
    menu.tick();
    Assert(menu.focusOn(2), "the mouse moved onto the third: it takes the focus");
    Assert(menu.button(2).hovered && menu.button(2).focused, "hovered and focused");
    Assert(!menu.button(0).hovered, "the first is not");
    menu.press(kuge::Key::Up);
    Assert(menu.focusOn(1), "the keyboard goes on from there");
    menu.release(kuge::Key::Up);
    menu.tick();
    menu.tick();
    Assert(menu.focusOn(1), "a mouse that does not move does not take the focus back");
    menu.mouseTo(1.0f, 1.0f);
    menu.tick();
    Assert(menu.focusOn(1) && !menu.button(1).hovered, "and moving it off the buttons changes nothing");
}

Test(uiinput, a_click_presses)
{
    Menu menu;
    const auto second = menu.centerOf(1);

    menu.tick();
    menu.mouseTo(second.x, second.y);
    menu.clickAt(second.x, second.y);
    menu.tick();
    Assert(menu.focusOn(1), "the button under the mouse");
    bool activated = false;
    for (const auto& event : menu.events()) {
        activated = activated || (event.kind == Kind::Activated && event.entity == menu.buttons[1]);
    }
    Assert(activated, "was pressed by the click");
    Assert(menu.button(1).pressed, "and looks pressed while the button is down");
}

Test(uiinput, a_click_elsewhere_does_nothing)
{
    Menu menu;

    menu.tick();
    menu.mouseTo(5.0f, 5.0f);
    menu.clickAt(5.0f, 5.0f);
    menu.tick();
    for (const auto& event : menu.events()) {
        Assert(event.kind != Kind::Activated, "nothing was pressed");
    }
    Assert(menu.focusOn(0), "and the focus is where it was");
}

Test(uiinput, the_top_button_gets_the_click)
{
    FontFile font;
    kw::Entity low = 0, high = 0;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            low = uiNode(w, {.offset = {100.0f, 100.0f}, .size = {80.0f, 40.0f}, .layer = 0});
            high = uiNode(w, {.offset = {120.0f, 110.0f}, .size = {80.0f, 40.0f}, .layer = 1});
            w.add<kuge::UiButton>(low, kuge::UiButton{.text = "low"});
            w.add<kuge::UiButton>(high, kuge::UiButton{.text = "high"});
        });
    });

    bindUiKeys(fx.client->input());
    fx.frame(1.0 / 60.0);
    fx.dummy.input->push(kuge::MouseMoveEvent{{150.0f, 125.0f}, {}});      // where they overlap
    fx.dummy.input->push(kuge::MouseButtonEvent{kuge::MouseButton::Left, true, {150.0f, 125.0f}});
    fx.frame(1.0 / 60.0);
    const auto& events = fx.scene->world().getResource<kuge::UiEvents>().list;
    bool highPressed = false, lowPressed = false;

    for (const auto& event : events) {
        highPressed = highPressed || (event.kind == Kind::Activated && event.entity == high);
        lowPressed = lowPressed || (event.kind == Kind::Activated && event.entity == low);
    }
    Assert(highPressed && !lowPressed, "the one that is over the other");
}

Test(uiinput, cancel_says_back)
{
    Menu menu;

    menu.tick();
    menu.press(kuge::Key::Escape);
    AssertEq(menu.events().size(), 1, "one event");
    Assert(menu.events()[0].kind == Kind::Cancelled && menu.events()[0].hasEntity && menu.events()[0].entity == menu.buttons[0],
        "Cancelled, with the button that had the focus");
    Menu empty({});
    empty.press(kuge::Key::Escape);
    Assert(empty.events().size() == 1 && empty.events()[0].kind == Kind::Cancelled && !empty.events()[0].hasEntity, "even with no buttons: back is back");
}

Test(uiinput, the_focus_follows_changes)
{
    Menu menu;

    menu.tick();
    menu.press(kuge::Key::Down);
    Assert(menu.focusOn(1), "on the second");
    menu.world().get<kuge::UiButton>(menu.buttons[1]).enabled = false;
    menu.tick();
    Assert(menu.focusOn(0), "it is disabled: the focus goes to the first that can be pressed");
    menu.world().remove(menu.buttons[0]);
    menu.tick();
    Assert(menu.focusOn(2), "the one that had it is destroyed: it goes to what is left");
    menu.world().get<kuge::UiNode>(menu.buttons[2]).visible = false;
    menu.fx->frame(0.0);          // the layout is redone at each frame, and the buttons react to the last one
    menu.tick();
    Assert(!menu.state().hasFocus, "nothing left to be on: no focus, and no crash");
    menu.press(kuge::Key::Enter);
    Assert(menu.events().empty(), "nothing can be pressed");
}

Test(uiinput, no_action_no_reaction)
{
    FontFile font;
    kw::Entity b = 0;
    Fixture fx([&](TestScene& scene) {
        kuge::installUi(scene.setup());          // no actions given: all -1
        scene.world().getResource<kuge::UiTheme>().font = scene.ctx().engine().module<kuge::ClientModule>()->loadFont(font.path, 16);
        b = uiNode(scene.world(), {.size = {50.0f, 20.0f}});
        scene.world().add<kuge::UiButton>(b, kuge::UiButton{.text = "x"});
    });

    fx.dummy.input->push(kuge::KeyEvent{kuge::Key::Enter, true});
    fx.frame(1.0 / 60.0);
    fx.frame(1.0 / 60.0);
    Assert(fx.scene->world().getResource<kuge::UiState>().hasFocus, "the button is selected anyway");
    for (const auto& event : fx.scene->world().getResource<kuge::UiEvents>().list) {
        Assert(event.kind != Kind::Activated, "but no key does anything: the interface was given no action");
    }
}
