#include "ui/ConsoleScene.hpp"
#include "Ref.hpp"
#include "SceneManager.hpp"
#include "Stage.hpp"
#include "backend/IWindow.hpp"
#include "input/ActionState.hpp"
#include "input/InputMap.hpp"
#include "render/Components.hpp"
#include <algorithm>
#include <charconv>
#include <format>

namespace
{
    using namespace kuge;

    Color colorOf(LoggerLevel level)
    {
        switch (level) {
            case LoggerLevel::DEBUG:   return {150, 200, 210, 255};
            case LoggerLevel::INFO:    return {225, 225, 235, 255};
            case LoggerLevel::SUCCESS: return {120, 220, 130, 255};
            case LoggerLevel::WARN:    return {240, 210, 90, 255};
            case LoggerLevel::ERROR:   return {245, 110, 100, 255};
            default:                   return {225, 225, 235, 255};
        }
    }

    // The level is written, not only coloured
    const char* nameOf(LoggerLevel level)
    {
        switch (level) {
            case LoggerLevel::DEBUG:   return "DEBUG";
            case LoggerLevel::INFO:    return "INFO ";
            case LoggerLevel::SUCCESS: return "OK   ";
            case LoggerLevel::WARN:    return "WARN ";
            case LoggerLevel::ERROR:   return "ERROR";
            default:                   return "     ";
        }
    }

    constexpr Color ANSWER{170, 220, 255, 255};
    constexpr Color ECHO{255, 255, 255, 255};

    std::string commonStart(const std::vector<std::string>& names)
    {
        std::string common = names.front();

        for (const std::string& name : names) {
            std::size_t i = 0;

            while (i < common.size() && i < name.size() && common[i] == name[i]) {
                ++i;
            }
            common.resize(i);
        }
        return common;
    }
}

// Before UiInteract in the Input stage: it reads the keys the interface is about to take (Up, Down, Tab, Escape) and
// the line that was submitted at the last tick.
class kuge::ConsoleScene::Input final : public kw::ISystem
{
    public:
        explicit Input(ConsoleScene& console) : m_console(console) {}

        bool handle(kw::World& world) override
        {
            ConsoleScene& console = m_console;
            InputMap& input = *world.getResource<Ref<InputMap>>();
            UiTextField& field = world.get<UiTextField>(console.m_field);
            const std::vector<Key> keys = input.editKeys();
            const auto pressed = [&](Key key) { return std::find(keys.begin(), keys.end(), key) != keys.end(); };

            for (const UiEvent& event : world.getResource<UiEvents>().list) {   // (of the last tick: UiInteract clears them first)
                if (event.kind == UiEvent::Kind::Submitted && event.hasEntity && event.entity == console.m_field) {
                    submit(world, field);
                }
            }
            if (pressed(Key::Escape) || (console.m_toggle >= 0 && world.getResource<ActionState>().wasPressed(console.m_toggle))) {
                input.discardTyped();
                console.ctx().scenes().pop();
                return true;
            }
            if (pressed(Key::Up) && !console.m_history.empty()) {
                console.m_walk = console.m_walk > 0 ? console.m_walk - 1 : 0;
                field.text = console.m_history[console.m_walk];
                field.caret = field.text.size();
            } else if (pressed(Key::Down) && console.m_walk < console.m_history.size()) {
                ++console.m_walk;
                field.text = console.m_walk < console.m_history.size() ? console.m_history[console.m_walk] : std::string();
                field.caret = field.text.size();
            }
            // Tab is acted on one tick later: the interface adds what was typed with it to the field after this system
            if (m_tab) {
                complete(field);
            }
            m_tab = pressed(Key::Tab);
            console.pullLog();
            console.refresh();
            auto& state = world.getResource<UiState>();

            state.focus = console.m_field;   // (Tab must not take the focus away: the field is all there is)
            state.hasFocus = true;
            return true;
        }

    private:
        void submit(kw::World&, UiTextField& field)
        {
            ConsoleScene& console = m_console;
            const std::string line = field.text;

            field.text.clear();
            field.caret = 0;
            if (line.find_first_not_of(' ') == std::string::npos) {
                return;
            }
            console.say("> " + line, ECHO);
            if (console.m_history.empty() || console.m_history.back() != line) {
                console.m_history.push_back(line);
            }
            console.m_walk = console.m_history.size();
            std::string answer = console.m_commands->run(line);

            for (std::size_t from = 0; !answer.empty() && from <= answer.size();) {   // one line each
                const std::size_t end = answer.find('\n', from);

                console.say(answer.substr(from, end == std::string::npos ? std::string::npos : end - from), ANSWER);
                if (end == std::string::npos) {
                    break;
                }
                from = end + 1;
            }
        }

        // The first word is completed; several candidates complete what they share and are listed
        void complete(UiTextField& field)
        {
            if (field.text.find(' ') != std::string::npos) {
                return;
            }
            const std::vector<std::string> names = m_console.m_commands->complete(field.text);

            if (names.empty()) {
                return;
            }
            if (names.size() == 1) {
                field.text = names.front() + " ";
            } else {
                field.text = commonStart(names);
                std::string list;

                for (const std::string& name : names) {
                    list += (list.empty() ? "" : "  ") + name;
                }
                m_console.say(list, ANSWER);
            }
            field.caret = field.text.size();
        }

        ConsoleScene& m_console;
        bool          m_tab = false;
};

kuge::ConsoleScene::ConsoleScene(std::shared_ptr<Commands> commands, std::shared_ptr<RingLogHandler> log, std::shared_ptr<const IFont> font,
    int toggleAction)
    : m_commands(std::move(commands)), m_log(std::move(log)), m_font(std::move(font)), m_toggle(toggleAction)
{
}

std::string kuge::ConsoleScene::typed(void)
{
    return world().get<UiTextField>(m_field).text;
}

void kuge::ConsoleScene::say(std::string text, Color color)
{
    m_lines.push_back({std::move(text), color});
    while (m_lines.size() > KEPT) {
        m_lines.pop_front();
    }
}

void kuge::ConsoleScene::pullLog(void)
{
    if (!m_log) {
        return;
    }
    Cvars& cvars = m_commands->cvars();
    int minimum = 1;
    const std::string level = cvars.get("console_log_level");
    const std::string category = cvars.get("console_log_category");

    std::from_chars(level.data(), level.data() + level.size(), minimum);
    for (const auto& line : m_log->snapshot(m_seen)) {
        m_seen = line.id;
        const LogRecord& record = line.record;

        if (static_cast<int>(record.level) >= minimum && (category.empty() || record.category == category)) {
            say(std::format("{} {}", nameOf(record.level), lineText(record)), colorOf(record.level));
        }
    }
}

void kuge::ConsoleScene::refresh(void)
{
    kw::World& next = world();
    const std::size_t first = m_lines.size() > SHOWN ? m_lines.size() - SHOWN : 0;

    for (std::size_t i = 0; i < m_labels.size(); ++i) {
        UiLabel& label = next.get<UiLabel>(m_labels[i]);
        const std::size_t at = first + i;

        label.text = at < m_lines.size() ? m_lines[at].text : std::string();
        label.color = at < m_lines.size() ? m_lines[at].color : Color{};
    }
}

void kuge::ConsoleScene::onEnter(void)
{
    Cvars& cvars = m_commands->cvars();

    installClientSystems();
    if (!cvars.has("console_log_level")) {
        cvars.add("console_log_level", 1, CvarFlag::None, "Lowest level of the log the console shows: 0 debug, 1 info, 2 ok, 3 warn, 4 error");
    }
    if (!cvars.has("console_log_category")) {
        cvars.add<std::string>("console_log_category", "", CvarFlag::None, "Only this category of the log is shown in the console (empty: all)");
    }
    addSystem(kw::Schedule::Fixed, stage::Input, std::make_unique<Input>(*this));
    installUi(setup());
    world().getResource<UiTheme>().font = m_font;
    if (m_log) {
        m_seen = m_log->lastId();   // the log from now on: the console does not replay what was before it opened
    }
    // The panel: lines on top of a field
    kw::World& next = world();
    const auto node = [&](UiNode made) {
        const kw::Entity entity = next.create();

        next.add<UiNode>(entity, made);
        return entity;
    };
    const kw::Entity panel = node({.anchor = Anchor::Top, .size = {1280.0f, 0.0f}, .layer = 100});

    next.add<UiPanel>(panel, UiPanel{.fill = {14, 16, 24, 235}});
    next.add<UiStack>(panel, UiStack{.direction = Direction::Vertical, .spacing = 2.0f, .padding = 6.0f, .align = Align::Stretch});
    for (std::size_t i = 0; i < SHOWN; ++i) {
        const kw::Entity label = node({.parent = panel, .hasParent = true});

        next.add<UiLabel>(label, UiLabel{});
        m_labels.push_back(label);
    }
    m_field = node({.parent = panel, .hasParent = true});
    next.add<UiTextField>(m_field, UiTextField{.placeholder = "type a command (help)", .maxLength = 200, .columns = 100});
    auto& state = next.getResource<UiState>();

    state.focus = m_field;
    state.hasFocus = true;
    next.getResource<Ref<InputMap>>()->discardTyped();   // the key that opened the console typed its character
    say("console: help lists the commands, Tab completes, Escape closes", ANSWER);
}

void kuge::ConsoleScene::onExit(void)
{
    world().getResource<Ref<InputMap>>()->discardTyped();
}
