extern "C" {
    #include "kronklab/kronklab.h"
}
#include "ui/ConsoleScene.hpp"
#include "ui_fixture.hpp"

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    enum class Act : std::uint8_t { Console = 20 };

    struct Counted : kw::ISystem
    {
        explicit Counted(int& count) : m_count(count) {}
        bool handle(kw::World&) override { ++m_count; return true; }
        int& m_count;
    };

    struct Desk
    {
        FontFile                         font;
        std::shared_ptr<kuge::Cvars>     cvars = std::make_shared<kuge::Cvars>();
        std::shared_ptr<kuge::Commands>  commands = std::make_shared<kuge::Commands>(*cvars);
        std::shared_ptr<RingLogHandler>  ring = std::make_shared<RingLogHandler>(100);
        int                              gameTicks = 0;
        Fixture                          fx;
        std::shared_ptr<const kuge::IFont> face;

        Desk() : fx([this](TestScene& scene) {
                scene.addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Counted>(gameTicks));
            })
        {
            face = fx.client->loadFont(font.path, 16);
            fx.client->input().bind(Act::Console, kuge::Key::Backslash);
        }

        void open(void)
        {
            fx.engine.scenes().push<kuge::ConsoleScene>(commands, ring, face, kuge::actionId(Act::Console));
            fx.frame(1.0 / 60.0);
        }

        kuge::ConsoleScene& console(void) { return *static_cast<kuge::ConsoleScene*>(fx.engine.scenes().top()); }
        void type(const char* text) { fx.dummy.input->push(kuge::TextEvent{text}); }
        void press(kuge::Key key)
        {
            fx.dummy.input->push(kuge::KeyEvent{key, true});
            fx.dummy.input->push(kuge::KeyEvent{key, false});
        }
        void tick(int count = 1) { for (int i = 0; i < count; ++i) { fx.frame(1.0 / 60.0); } }

        bool said(const std::string& text)
        {
            const auto& lines = console().lines();

            return std::any_of(lines.begin(), lines.end(), [&](const auto& line) { return line.text == text; });
        }
    };
}

Test(console, runs_a_typed_line)
{
    Desk desk;

    desk.open();
    desk.type("echo hello world");
    desk.tick();
    AssertStrEq(desk.console().typed().c_str(), "echo hello world", "what is typed goes in the field");
    desk.press(kuge::Key::Enter);
    desk.tick(3);
    Assert(desk.said("> echo hello world") && desk.said("hello world"), "the line and its answer are shown");
    AssertStrEq(desk.console().typed().c_str(), "", "the field is empty again");
    desk.type("nothing at all");
    desk.press(kuge::Key::Enter);
    desk.tick(3);
    Assert(desk.said("unknown command 'nothing' (help lists them)"), "an unknown command is an answer");
}

Test(console, the_game_stands_still)
{
    Desk desk;

    desk.tick(2);
    const int before = desk.gameTicks;

    Assert(before >= 2, "the game ticks without the console: %d", before);
    desk.open();
    desk.tick(5);
    AssertEq(desk.gameTicks, before, "its simulation does not run while the console has the keyboard");
    desk.press(kuge::Key::Escape);
    desk.tick(2);
    AssertEq(desk.fx.engine.scenes().size(), 1u, "Escape closes it");
    desk.tick(3);
    Assert(desk.gameTicks > before, "and the game goes on");
}

Test(console, the_action_closes_it)
{
    Desk desk;

    desk.open();
    desk.fx.dummy.input->push(kuge::KeyEvent{kuge::Key::Backslash, true});
    desk.tick(2);
    AssertEq(desk.fx.engine.scenes().size(), 1u, "the key that opens it closes it");
}

Test(console, the_opening_key_is_not_typed)
{
    Desk desk;

    desk.type("`");   // what the key that opens the console typed
    desk.open();
    desk.tick();
    AssertStrEq(desk.console().typed().c_str(), "", "its character is thrown away");
    desk.type("ab");
    desk.tick();
    AssertStrEq(desk.console().typed().c_str(), "ab", "what follows is kept");
}

Test(console, history_and_completion)
{
    Desk desk;

    desk.commands->cvars().add("sv_speed", 3);
    desk.open();
    desk.type("echo one");
    desk.press(kuge::Key::Enter);
    desk.tick(3);
    desk.type("echo two");
    desk.press(kuge::Key::Enter);
    desk.tick(3);
    desk.press(kuge::Key::Up);
    desk.tick();
    AssertStrEq(desk.console().typed().c_str(), "echo two", "Up: the last line");
    desk.press(kuge::Key::Up);
    desk.tick();
    AssertStrEq(desk.console().typed().c_str(), "echo one", "Up again: the one before");
    desk.press(kuge::Key::Down);
    desk.tick();
    AssertStrEq(desk.console().typed().c_str(), "echo two", "Down: forward");
    desk.press(kuge::Key::Down);
    desk.tick();
    AssertStrEq(desk.console().typed().c_str(), "", "and out of the history: empty");
    desk.type("ec");
    desk.press(kuge::Key::Tab);
    desk.tick(2);
    AssertStrEq(desk.console().typed().c_str(), "echo ", "Tab completes a command");
    for (int i = 0; i < 5; ++i) {
        desk.press(kuge::Key::Backspace);
    }
    desk.tick();
    desk.type("s");
    desk.tick();
    desk.press(kuge::Key::Tab);
    desk.tick(3);
    Assert(desk.said("set  sv_speed"), "several candidates: listed");
    AssertStrEq(desk.console().typed().c_str(), "s", "and completed as far as they agree");
    desk.press(kuge::Key::Backspace);
    desk.tick();
    desk.type("sv");
    desk.tick();
    desk.press(kuge::Key::Tab);
    desk.tick(2);
    AssertStrEq(desk.console().typed().c_str(), "sv_speed ", "a variable too");
    AssertEq(desk.fx.engine.scenes().size(), 2u, "(Tab did not close it)");
}

Test(console, shows_the_log)
{
    Desk desk;

    desk.ring->log(LoggerLevel::INFO, "before");
    desk.open();
    desk.ring->handle({LoggerLevel::INFO, "t", "net", "bad packet"});
    desk.ring->handle({LoggerLevel::DEBUG, "t", "net", "chatter"});
    desk.ring->handle({LoggerLevel::ERROR, "t", "audio", "no device"});
    desk.tick(2);
    Assert(desk.said("INFO  [net] bad packet") && desk.said("ERROR [audio] no device"), "the lines come with their level written");
    Assert(!desk.said("INFO  before") && !desk.said("DEBUG [net] chatter"), "not what was before it opened, nor below the level");
    desk.type("console_log_level 0");
    desk.press(kuge::Key::Enter);
    desk.tick(3);
    desk.ring->handle({LoggerLevel::DEBUG, "t", "net", "chatter 2"});
    desk.tick(2);
    Assert(desk.said("DEBUG [net] chatter 2"), "the level is a variable: lowered, debug shows");
    desk.type("console_log_category audio");
    desk.press(kuge::Key::Enter);
    desk.tick(3);
    desk.ring->handle({LoggerLevel::ERROR, "t", "net", "net again"});
    desk.ring->handle({LoggerLevel::ERROR, "t", "audio", "audio again"});
    desk.tick(2);
    Assert(desk.said("ERROR [audio] audio again") && !desk.said("ERROR [net] net again"), "and so is the category");
}
