extern "C" {
    #include "kronklab/kronklab.h"
}
#include "ui_fixture.hpp"
#include <vector>

// NOTE: kronklab test names are limited to 31 characters.
// The dummy font: a letter is 8 pixels wide (a BYTE: "é" is 16), a line 12 high.

namespace
{
    using Kind = kuge::UiEvent::Kind;
    using Call = kuge::DummyRenderer::Call;

    // A form: two text fields and a button, in a column
    struct Form
    {
        FontFile                 font;
        std::vector<kw::Entity>  fields;
        kw::Entity               button = 0;
        kw::Entity               column = 0;
        std::unique_ptr<Fixture> fx;

        explicit Form(std::size_t fieldCount = 2, bool withButton = true)
        {
            fx = std::make_unique<Fixture>([&](TestScene& scene) {
                withUi(scene, font, [&](kw::World& w) {
                    column = uiNode(w, {.anchor = kuge::Anchor::Center});
                    w.add<kuge::UiStack>(column, kuge::UiStack{});
                    for (std::size_t i = 0; i < fieldCount; ++i) {
                        addField(w);
                    }
                    if (withButton) {
                        button = uiButton(w, "Create", column);
                    }
                });
            });
            bindUiKeys(fx->client->input());
        }

        kw::Entity addField(kw::World& w)
        {
            const kw::Entity field = uiChild(w, column);

            w.add<kuge::UiTextField>(field, kuge::UiTextField{});
            fields.push_back(field);
            return field;
        }

        kw::World& world(void) { return fx->scene->world(); }
        const kuge::UiState& state(void) { return world().getResource<kuge::UiState>(); }
        const std::vector<kuge::UiEvent>& events(void) { return world().getResource<kuge::UiEvents>().list; }
        kuge::UiTextField& field(std::size_t i) { return world().get<kuge::UiTextField>(fields[i]); }
        bool focusOnField(std::size_t i) { return state().hasFocus && state().focus == fields[i]; }
        bool focusOnButton(void) { return state().hasFocus && state().focus == button; }

        std::size_t count(Kind kind)
        {
            return static_cast<std::size_t>(std::count_if(events().begin(), events().end(), [kind](const auto& e) { return e.kind == kind; }));
        }

        void tick(void) { fx->frame(1.0 / 60.0); }
        void type(const std::string& text) { fx->dummy.input->push(kuge::TextEvent{text}); tick(); }
        void press(kuge::Key key, bool repeat = false) { fx->dummy.input->push(kuge::KeyEvent{key, true, repeat}); tick(); }
        void release(kuge::Key key) { fx->dummy.input->push(kuge::KeyEvent{key, false}); }
        void mouseTo(float x, float y) { fx->dummy.input->push(kuge::MouseMoveEvent{{x, y}, {}}); }
        void clickAt(float x, float y)
        {
            fx->dummy.input->push(kuge::MouseButtonEvent{kuge::MouseButton::Left, true, {x, y}});
            tick();
        }
        kuge::Rect rectOf(kw::Entity entity) { return world().getResource<kuge::UiLayoutResult>().rects.at(entity); }
    };
}

Test(uitext, a_field_has_the_focus)
{
    Form form;

    form.tick();
    Assert(form.focusOnField(0), "the first thing of a form has the focus, here a field");
    Assert(form.field(0).focused && !form.field(1).focused, "the field knows it");
    form.type("abc");
    AssertStrEq(form.field(0).text.c_str(), "abc", "what is typed goes into it");
    AssertEq(form.field(0).caret, 3, "the caret is after it");
    AssertEq(form.count(Kind::Changed), 1, "it says that it changed");
    Assert(form.events().back().entity == form.fields[0], "and which");
    AssertStrEq(form.field(1).text.c_str(), "", "the other one is left alone");
    form.tick();
    AssertEq(form.events().size(), 0, "nothing more to say once nothing is typed");
}

Test(uitext, the_caret_and_the_keys)
{
    Form form;

    form.tick();
    form.type("hello");
    form.press(kuge::Key::Left);
    form.release(kuge::Key::Left);
    form.press(kuge::Key::Left);
    AssertEq(form.field(0).caret, 3, "two steps to the left");
    AssertEq(form.count(Kind::Changed), 0, "moving the caret changes nothing");
    form.type("X");
    AssertStrEq(form.field(0).text.c_str(), "helXlo", "typing goes in at the caret");
    AssertEq(form.field(0).caret, 4, "and the caret follows");
    form.press(kuge::Key::Backspace);
    AssertStrEq(form.field(0).text.c_str(), "hello", "Backspace erases before the caret");
    AssertEq(form.field(0).caret, 3, "");
    form.press(kuge::Key::Delete);
    AssertStrEq(form.field(0).text.c_str(), "helo", "Delete erases after it");
    AssertEq(form.count(Kind::Changed), 1, "and says so");
    form.press(kuge::Key::Home);
    AssertEq(form.field(0).caret, 0, "Home");
    form.press(kuge::Key::Backspace);
    AssertStrEq(form.field(0).text.c_str(), "helo", "nothing before the start");
    AssertEq(form.count(Kind::Changed), 0, "and nothing to say");
    form.press(kuge::Key::End);
    AssertEq(form.field(0).caret, 4, "End");
    form.press(kuge::Key::Delete);
    AssertStrEq(form.field(0).text.c_str(), "helo", "nothing after the end");
    form.press(kuge::Key::Right);
    AssertEq(form.field(0).caret, 4, "and the caret does not go past it");
}

Test(uitext, holding_backspace_erases)
{
    Form form;

    form.tick();
    form.type("abcdef");
    form.press(kuge::Key::Backspace);
    form.press(kuge::Key::Backspace, true);   // (the OS repeating the key: each one counts)
    form.press(kuge::Key::Backspace, true);
    AssertStrEq(form.field(0).text.c_str(), "abc", "each repeat erases one more");
}

Test(uitext, text_is_utf8)
{
    Form form;

    form.tick();
    form.field(0).maxLength = 3;
    form.type("\xC3\xA9\xC3\xA9\xE2\x82\xAC" "z");   // é é € z: four characters, the last one does not fit
    AssertStrEq(form.field(0).text.c_str(), "\xC3\xA9\xC3\xA9\xE2\x82\xAC", "the length is in characters, not in bytes");
    AssertEq(form.field(0).caret, 7, "the caret is a byte, after the last character");
    form.press(kuge::Key::Left);
    AssertEq(form.field(0).caret, 4, "one step is one character (the 3 bytes of the euro)");
    form.press(kuge::Key::Left);
    AssertEq(form.field(0).caret, 2, "then the 2 bytes of the accent");
    form.press(kuge::Key::Delete);
    AssertStrEq(form.field(0).text.c_str(), "\xC3\xA9\xE2\x82\xAC", "Delete erases a whole character");
    form.press(kuge::Key::End);
    form.press(kuge::Key::Backspace);
    AssertStrEq(form.field(0).text.c_str(), "\xC3\xA9", "so does Backspace, here the euro");
    form.type("\xF0\x9F\x8E\xAE");
    AssertStrEq(form.field(0).text.c_str(), "\xC3\xA9\xF0\x9F\x8E\xAE", "an emoji goes in whole");
    form.press(kuge::Key::Backspace);
    AssertStrEq(form.field(0).text.c_str(), "\xC3\xA9", "and out whole");
}

Test(uitext, the_length_is_a_limit)
{
    Form form;

    form.tick();
    form.field(0).maxLength = 4;
    form.type("abcdef");
    AssertStrEq(form.field(0).text.c_str(), "abcd", "what does not fit is left out");
    form.type("g");
    AssertStrEq(form.field(0).text.c_str(), "abcd", "and nothing more goes in");
    AssertEq(form.count(Kind::Changed), 0, "nothing changed: nothing said");
    form.press(kuge::Key::Backspace);
    form.type("z");
    AssertStrEq(form.field(0).text.c_str(), "abcz", "room that was made is used");
}

Test(uitext, control_characters_are_dropped)
{
    Form form;

    form.tick();
    form.type(std::string("a\nb\tc") + '\x7F' + "d\xC2\x85" "e");   // line break, tab, DEL and a control character of Latin-1
    AssertStrEq(form.field(0).text.c_str(), "abcde", "only what can be seen is kept");
}

Test(uitext, enter_submits)
{
    Form form;

    form.tick();
    form.type("Les copains");
    form.press(kuge::Key::Enter);
    AssertEq(form.count(Kind::Submitted), 1, "Enter says Submitted");
    Assert(form.events().back().entity == form.fields[0], "for the field");
    AssertEq(form.count(Kind::Activated), 0, "Enter is bound to the accept action of the menu: it does not press anything here");
    AssertStrEq(form.field(0).text.c_str(), "Les copains", "and the text stays");
    form.type("x");
    form.press(kuge::Key::Enter);
    AssertEq(form.count(Kind::Changed), 0, "a tick that only submits changes nothing");
}

Test(uitext, changed_then_submitted)
{
    Form form;

    form.tick();
    form.fx->dummy.input->push(kuge::TextEvent{"ok"});
    form.fx->dummy.input->push(kuge::KeyEvent{kuge::Key::Enter, true});
    form.tick();
    AssertEq(form.events().size(), 2, "typed and submitted in the same tick: two events");
    Assert(form.events()[0].kind == Kind::Changed && form.events()[1].kind == Kind::Submitted, "in this order");
}

Test(uitext, navigation_keys_type)
{
    // A game binds W to "up": in a field, W is a letter. The actions that move the focus are left alone.
    Form form;

    form.fx->client->input().bind(UiAct::Up, kuge::Key::W);
    form.fx->client->input().bind(UiAct::Down, kuge::Key::S);
    form.tick();
    Assert(form.focusOnField(0), "the focus is on the first field");
    form.fx->dummy.input->push(kuge::KeyEvent{kuge::Key::S, true});
    form.fx->dummy.input->push(kuge::TextEvent{"s"});
    form.tick();
    AssertStrEq(form.field(0).text.c_str(), "s", "S typed an s");
    Assert(form.focusOnField(0), "and did not move the focus down");
    form.release(kuge::Key::S);
    form.press(kuge::Key::Down);
    Assert(form.focusOnField(0), "neither does the arrow: Tab leaves a field");
    form.release(kuge::Key::Down);
    form.press(kuge::Key::Tab);
    Assert(form.focusOnField(1), "Tab goes to the next field");
}

Test(uitext, tab_goes_round)
{
    Form form;

    form.tick();
    form.press(kuge::Key::Tab);
    Assert(form.focusOnField(1), "Tab: the second field");
    AssertEq(form.count(Kind::Focused), 1, "with a Focused event");
    form.press(kuge::Key::Tab);
    Assert(form.focusOnButton(), "then the button");
    form.press(kuge::Key::Tab);
    Assert(form.focusOnField(0), "and round to the first again");
    // A button lets the arrows move again
    form.press(kuge::Key::Tab);
    form.press(kuge::Key::Tab);
    form.release(kuge::Key::Tab);
    Assert(form.focusOnButton(), "on the button again");
    form.press(kuge::Key::Up);
    Assert(form.focusOnField(1), "the arrows move the focus from a button, into a field");
}

Test(uitext, the_button_still_works)
{
    Form form;

    form.tick();
    form.press(kuge::Key::Tab);
    form.press(kuge::Key::Tab);
    Assert(form.focusOnButton(), "on the button");
    form.release(kuge::Key::Tab);
    form.press(kuge::Key::Enter);
    AssertEq(form.count(Kind::Activated), 1, "Enter presses it");
    form.type("abc");
    AssertStrEq(form.field(0).text.c_str(), "", "what is typed with no field focused goes nowhere");
    AssertStrEq(form.field(1).text.c_str(), "", "");
}

Test(uitext, a_click_focuses_a_field)
{
    Form form;

    form.tick();
    form.type("abc");   // into the first field, which has the focus
    const kuge::Rect second = form.rectOf(form.fields[1]);

    form.clickAt(second.x + 12.0f, second.y + 5.0f);
    Assert(form.focusOnField(1), "a click on the other field takes the focus");
    AssertEq(form.count(Kind::Activated), 0, "it focuses: it does not press anything");
    form.fx->dummy.input->push(kuge::MouseButtonEvent{kuge::MouseButton::Left, false, {0.0f, 0.0f}});
    form.tick();
    form.type("z");
    AssertStrEq(form.field(1).text.c_str(), "z", "what is typed goes there now");
    AssertStrEq(form.field(0).text.c_str(), "abc", "and the first one keeps what it had");
    // A click on a button takes the focus from a field, and presses it
    const kuge::Rect button = form.rectOf(form.button);

    form.clickAt(button.center().x, button.center().y);
    Assert(form.focusOnButton(), "a click on the button leaves the field");
    AssertEq(form.count(Kind::Activated), 1, "and presses it");
}

Test(uitext, a_click_places_the_caret)
{
    Form form;

    form.tick();
    form.type("abcdef");
    const kuge::Rect rect = form.rectOf(form.fields[0]);

    form.clickAt(rect.x + 10.0f + 3.0f * 8.0f + 1.0f, rect.y + 5.0f);   // just after the 3rd letter
    AssertEq(form.field(0).caret, 3, "between the 3rd and the 4th letter");
    form.fx->dummy.input->push(kuge::MouseButtonEvent{kuge::MouseButton::Left, false, {0.0f, 0.0f}});
    form.tick();
    form.clickAt(rect.x + 10.0f + 5.0f * 8.0f + 6.0f, rect.y + 5.0f);   // nearer to the 6th boundary than to the 5th
    AssertEq(form.field(0).caret, 6, "the nearest boundary");
    form.fx->dummy.input->push(kuge::MouseButtonEvent{kuge::MouseButton::Left, false, {0.0f, 0.0f}});
    form.tick();
    form.clickAt(rect.right() - 2.0f, rect.y + 5.0f);
    AssertEq(form.field(0).caret, 6, "past the text: the end");
    form.type("!");
    AssertStrEq(form.field(0).text.c_str(), "abcdef!", "and typing goes in there");
}

Test(uitext, the_mouse_does_not_steal)
{
    Form form;

    form.tick();
    form.type("ab");
    const kuge::Rect under = form.rectOf(form.button);

    form.mouseTo(under.center().x, under.center().y);
    form.tick();
    Assert(form.focusOnField(0), "moving the mouse over a button does not take a field out of what is typed");
    form.type("c");
    AssertStrEq(form.field(0).text.c_str(), "abc", "it goes on typing");
    form.press(kuge::Key::Tab);
    form.press(kuge::Key::Tab);
    Assert(form.focusOnButton(), "(on the button, by Tab)");
    form.mouseTo(form.rectOf(form.fields[0]).center().x, form.rectOf(form.fields[0]).center().y);
    form.tick();
    Assert(form.focusOnField(0), "but from a button, the mouse moves the focus as it did");
}

Test(uitext, a_disabled_field)
{
    Form form;

    form.field(0).enabled = false;
    form.tick();
    Assert(form.focusOnField(1), "the focus goes to the first field that can be used");
    form.type("x");
    AssertStrEq(form.field(0).text.c_str(), "", "a disabled field gets nothing");
    AssertStrEq(form.field(1).text.c_str(), "x", "");
    form.press(kuge::Key::Tab);
    Assert(form.focusOnButton(), "and Tab skips it");
}

Test(uitext, typing_waits_for_no_field)
{
    // What was typed while no field could take it is lost: it does not go into the field that comes later
    Form form(0, true);

    form.tick();
    form.type("zzz");
    form.addField(form.world());
    form.tick();
    form.tick();
    AssertStrEq(form.field(0).text.c_str(), "", "nothing typed before it was there");
    form.press(kuge::Key::Tab);
    form.release(kuge::Key::Tab);
    form.type("ok");
    AssertStrEq(form.field(0).text.c_str(), "ok", "and what is typed now goes in");
}

Test(uitext, a_caret_that_is_wrong)
{
    Form form;

    form.tick();
    form.field(0).text = "a\xC3\xA9";
    form.field(0).caret = 2;   // in the middle of the accent
    form.type("x");
    AssertStrEq(form.field(0).text.c_str(), "ax\xC3\xA9", "the caret went to the start of a character: the text is still UTF-8");
    form.field(0).caret = 99;
    form.type("y");
    AssertStrEq(form.field(0).text.c_str(), "ax\xC3\xA9y", "a caret past the end is put at the end");
}

// -- The size of a field ---------------------------------------------------------------------------------
Test(uitext, the_size_of_a_field)
{
    FontFile font;
    kw::Entity natural = 0, given = 0, wide = 0;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            natural = uiNode(w, {});
            w.add<kuge::UiTextField>(natural, kuge::UiTextField{.columns = 10});
            given = uiNode(w, {.offset = {0.0f, 100.0f}, .size = {150.0f, 40.0f}});
            w.add<kuge::UiTextField>(given, kuge::UiTextField{});
            wide = uiNode(w, {.offset = {0.0f, 200.0f}});
            w.add<kuge::UiTextField>(wide, kuge::UiTextField{});
        });
    });

    fx.frame();
    const auto& rects = fx.scene->world().getResource<kuge::UiLayoutResult>().rects;

    AssertEq(rects.at(natural).w, 100.0f, "10 letters of 8 pixels and the padding twice (2 x 10)");
    AssertEq(rects.at(natural).h, 32.0f, "a line of 12 and the padding twice");
    AssertEq(rects.at(given).w, 150.0f, "the size of the node, if it has one (width)");
    AssertEq(rects.at(given).h, 40.0f, "(height)");
    AssertEq(rects.at(wide).w, 8.0f * 16 + 20.0f, "16 letters by default");
}

// -- What it looks like ------------------------------------------------------------------------------------
namespace
{
    // One field, 100 x 30 (so 78 pixels for the text and the caret: 9 letters), and what the theme says of it
    struct Drawn
    {
        FontFile                 font;
        kw::Entity               field = 0;
        std::unique_ptr<Fixture> fx;

        Drawn(const std::string& text, std::size_t caret, bool blink = false)
        {
            fx = std::make_unique<Fixture>([&](TestScene& scene) {
                withUi(scene, font, [&](kw::World& w) {
                    field = uiNode(w, {.size = {100.0f, 30.0f}});
                    w.add<kuge::UiTextField>(field, kuge::UiTextField{.text = text, .placeholder = "Name", .caret = caret});
                    w.getResource<kuge::UiTheme>().caretBlinkTicks = blink ? 1 : 0;
                });
            });
        }

        const std::vector<Call>& frame(void) { return fx->frame(1.0 / 60.0); }
    };

    const Call* textOf(const std::vector<Call>& calls)
    {
        for (const Call& call : calls) {
            if (call.kind == Call::Kind::Texture) {
                return &call;
            }
        }
        return nullptr;
    }

    // The caret: a fill 2 wide and a line high, in its color
    const Call* caretOf(const std::vector<Call>& calls)
    {
        const kuge::UiTheme theme;

        for (const Call& call : calls) {
            if (call.kind == Call::Kind::Fill && call.color == theme.fieldCaret && call.destination.w == 2.0f) {
                return &call;
            }
        }
        return nullptr;
    }
}

Test(uitext, a_field_is_drawn)
{
    Drawn drawn("abc", 3);
    const auto& calls = drawn.frame();
    const kuge::UiTheme theme;

    Assert(calls[0].kind == Call::Kind::Fill && calls[0].destination == kuge::Rect(0.0f, 0.0f, 100.0f, 30.0f) && calls[0].color == theme.fieldFill, "its background");
    Assert(calls[1].color == theme.focusRing, "a ring of the focus (it is the only thing that can have it)");
    const Call* text = textOf(calls);

    Assert(text != nullptr, "its text");
    Assert(text->destination == kuge::Rect(10.0f, 9.0f, 24.0f, 12.0f), "at the padding, in the middle of its height");
    Assert(text->texture.tint == theme.fieldText, "in the color of fields");
    const Call* caret = caretOf(calls);

    Assert(caret != nullptr, "and its caret");
    Assert(caret->destination == kuge::Rect(34.0f, 9.0f, 2.0f, 12.0f), "after the third letter");
}

Test(uitext, the_caret_is_where_it_is)
{
    Drawn drawn("abcd", 1);
    const Call* caret = caretOf(drawn.frame());

    Assert(caret != nullptr, "there is a caret");
    AssertEq(caret->destination.x, 18.0f, "after the first letter: 10 + 8");
}

Test(uitext, a_placeholder)
{
    Drawn drawn("", 0);
    const auto& calls = drawn.frame();
    const kuge::UiTheme theme;
    const Call* text = textOf(calls);

    Assert(text != nullptr && text->texture.tint == theme.fieldPlaceholder, "an empty field shows its placeholder, dimmed");
    AssertEq(text->destination.w, 32.0f, "\"Name\"");
    const Call* caret = caretOf(calls);

    Assert(caret != nullptr && caret->destination.x == 10.0f, "and its caret is at the start");
}

Test(uitext, long_text_shows_its_end)
{
    // 12 letters do not fit in 78 pixels (9 letters): the end is shown, where the caret is
    Drawn drawn("abcdefghijkl", 12);
    const auto& calls = drawn.frame();
    const Call* text = textOf(calls);

    Assert(text != nullptr, "the text");
    AssertEq(text->destination.w, 72.0f, "9 letters of it");
    const Call* caret = caretOf(calls);

    Assert(caret != nullptr, "the caret");
    AssertEq(caret->destination.x, 82.0f, "after the last one (10 + 72)");
    drawn.fx->scene->world().get<kuge::UiTextField>(drawn.field).caret = 0;
    const auto& home = drawn.frame();

    AssertEq(textOf(home)->destination.w, 72.0f, "with the caret at the start, it is the start that is shown: 9 letters");
    AssertEq(caretOf(home)->destination.x, 10.0f, "and the caret is there");
}

Test(uitext, the_caret_blinks)
{
    Drawn drawn("ab", 2, true);   // shown one tick, hidden the next
    int shown = 0;

    for (int frame = 0; frame < 6; ++frame) {
        shown += caretOf(drawn.frame()) != nullptr ? 1 : 0;
    }
    AssertEq(shown, 3, "it is there every other tick");
}

Test(uitext, only_the_focused_has_a_caret)
{
    FontFile font;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& w) {
            const kw::Entity column = uiNode(w, {.anchor = kuge::Anchor::Center});

            w.add<kuge::UiStack>(column, kuge::UiStack{});
            for (int i = 0; i < 2; ++i) {
                const kw::Entity field = uiChild(w, column);

                w.add<kuge::UiTextField>(field, kuge::UiTextField{.text = "x", .caret = 1});
            }
            w.getResource<kuge::UiTheme>().caretBlinkTicks = 0;
        });
    });
    const auto& calls = fx.frame(1.0 / 60.0);
    const kuge::UiTheme theme;
    const auto carets = std::count_if(calls.begin(), calls.end(), [&](const Call& c) { return c.kind == Call::Kind::Fill && c.color == theme.fieldCaret && c.destination.w == 2.0f; });

    AssertEq(carets, 1, "two fields, one caret: the one that has the focus");
}
