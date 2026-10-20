extern "C" {
    #include "kronklab/kronklab.h"
}
#include "ui/UiChatLog.hpp"
#include "ui_fixture.hpp"

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    struct Chat
    {
        FontFile                         font;
        std::unique_ptr<kuge::UiChatLog> log;
        Fixture                          fx;

        Chat() : fx([this](TestScene& scene) {
                withUi(scene, font, [this](kw::World& world) { log = std::make_unique<kuge::UiChatLog>(world, kuge::UiChatLog::Config{.lines = 3, .showSeconds = 2.0, .fadeSeconds = 1.0}); });
            }) {}

        kw::World& world(void) { return fx.scene->world(); }
        void tick(int count = 1) { for (int i = 0; i < count; ++i) { fx.frame(1.0 / 60.0); } }
    };
}

Test(chatlog, lines_fade_after_a_while)
{
    Chat chat;

    chat.log->push("ana: hello", {255, 0, 0, 255});
    chat.log->update(chat.world(), 0.0);
    AssertEq(chat.log->kept().size(), 1u, "a line is kept");
    AssertEq(chat.log->kept()[0].color.a, 255, "in full colour");
    chat.log->update(chat.world(), 1.9);
    chat.log->update(chat.world(), 0.6);   // 2.5 s: half way through the fade
    kuge::Color shown{};

    for (kw::Entity entity : chat.world().view<kuge::UiLabel>()) {
        if (chat.world().get<kuge::UiLabel>(entity).text == "ana: hello") {
            shown = chat.world().get<kuge::UiLabel>(entity).color;
        }
    }
    Assert(shown.a > 100 && shown.a < 155 && shown.r == 255, "half faded: alpha %d", shown.a);
    chat.log->update(chat.world(), 1.0);
    for (kw::Entity entity : chat.world().view<kuge::UiLabel>()) {
        Assert(chat.world().get<kuge::UiLabel>(entity).color.a == 0, "gone after the fade");
    }
}

Test(chatlog, only_the_last_lines)
{
    Chat chat;

    for (const char* line : {"one", "two", "three", "four"}) {
        chat.log->push(line);
    }
    AssertEq(chat.log->kept().size(), 3u, "3 lines at most");
    AssertStrEq(chat.log->kept().front().text.c_str(), "two", "the oldest went");
}

Test(chatlog, open_and_say)
{
    Chat chat;

    chat.tick();
    chat.fx.dummy.input->push(kuge::TextEvent{"t"});   // the key that opens it typed this
    chat.tick();
    chat.fx.dummy.input->push(kuge::TextEvent{"t"});
    chat.log->open(chat.world());
    Assert(chat.log->isOpen(), "open");
    chat.tick();
    AssertStrEq(chat.world().get<kuge::UiTextField>(chat.log->field()).text.c_str(), "", "what was typed and waits is dropped");
    chat.fx.dummy.input->push(kuge::TextEvent{"hi there"});
    chat.tick();
    AssertStrEq(chat.world().get<kuge::UiTextField>(chat.log->field()).text.c_str(), "hi there", "then the field has the keyboard");
    Assert(!chat.log->submitted(chat.world()), "nothing is said until Enter");
    chat.fx.dummy.input->push(kuge::KeyEvent{kuge::Key::Enter, true});
    chat.tick();
    const auto said = chat.log->submitted(chat.world());

    Assert(said && *said == "hi there", "Enter says the line");
    Assert(!chat.log->isOpen() && chat.world().get<kuge::UiTextField>(chat.log->field()).text.empty(), "and closes the field");
}

Test(chatlog, an_empty_line_is_nothing)
{
    Chat chat;

    chat.log->open(chat.world());
    chat.tick();
    chat.fx.dummy.input->push(kuge::TextEvent{"   "});
    chat.fx.dummy.input->push(kuge::KeyEvent{kuge::Key::Enter, true});
    chat.tick();
    Assert(!chat.log->submitted(chat.world()), "only spaces: no line");
    Assert(!chat.log->isOpen(), "and it closes");
}

Test(chatlog, an_open_chat_shows_everything)
{
    Chat chat;

    chat.log->push("old");
    chat.log->update(chat.world(), 10.0);
    chat.log->open(chat.world());
    chat.log->update(chat.world(), 0.0);
    int shown = 0;

    for (kw::Entity entity : chat.world().view<kuge::UiLabel>()) {
        shown += chat.world().get<kuge::UiLabel>(entity).text == "old" && chat.world().get<kuge::UiLabel>(entity).color.a > 0 ? 1 : 0;
    }
    AssertEq(shown, 1, "a line that faded is back while the field is open");
}
