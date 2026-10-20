#include "ui/UiChatLog.hpp"
#include "Ref.hpp"
#include "Utf8.hpp"
#include "input/InputMap.hpp"
#include <algorithm>

kuge::UiChatLog::UiChatLog(kw::World& world, Config config) : m_config(config)
{
    const auto make = [&](UiNode node) {
        const kw::Entity entity = world.create();

        world.add<UiNode>(entity, node);
        return entity;
    };
    const kw::Entity root = make({.anchor = config.anchor, .offset = config.offset, .size = config.size, .layer = 50});

    world.add<UiStack>(root, UiStack{.direction = Direction::Vertical, .spacing = 2.0f, .padding = 0.0f, .align = Align::Stretch});
    for (std::size_t i = 0; i < config.lines; ++i) {
        const kw::Entity label = make({.parent = root, .hasParent = true});

        UiLabel blank;

        blank.color = {0, 0, 0, 0};
        world.add<UiLabel>(label, blank);
        m_labels.push_back(label);
    }
    m_field = make({.parent = root, .hasParent = true, .visible = false});
    world.add<UiTextField>(m_field, UiTextField{.placeholder = "say something", .maxLength = config.maxLength, .columns = 40});
}

void kuge::UiChatLog::push(std::string text, Color color)
{
    m_kept.push_back({std::move(text), color, 0.0});
    while (m_kept.size() > m_config.lines) {
        m_kept.pop_front();
    }
}

void kuge::UiChatLog::open(kw::World& world)
{
    m_open = true;
    world.get<UiNode>(m_field).visible = true;
    world.get<UiTextField>(m_field).text.clear();
    world.get<UiTextField>(m_field).caret = 0;
    world.getResource<Ref<InputMap>>()->discardTyped();   // the key that opened it also typed its character
    auto& state = world.getResource<UiState>();

    state.focus = m_field;
    state.hasFocus = true;
}

void kuge::UiChatLog::close(kw::World& world)
{
    m_open = false;
    world.get<UiNode>(m_field).visible = false;
    world.get<UiTextField>(m_field).text.clear();
    world.get<UiTextField>(m_field).caret = 0;
    auto& state = world.getResource<UiState>();

    if (state.hasFocus && state.focus == m_field) {
        state.hasFocus = false;
    }
}

std::optional<std::string> kuge::UiChatLog::submitted(kw::World& world)
{
    if (!m_open) {
        return std::nullopt;
    }
    for (const UiEvent& event : world.getResource<UiEvents>().list) {
        if (event.kind == UiEvent::Kind::Submitted && event.hasEntity && event.entity == m_field) {
            const std::string line(trimSpaces(world.get<UiTextField>(m_field).text));

            close(world);
            if (line.empty()) {
                return std::nullopt;
            }
            return line;
        }
    }
    return std::nullopt;
}

void kuge::UiChatLog::update(kw::World& world, double seconds)
{
    for (Kept& line : m_kept) {
        line.age += seconds;
    }
    // The newest lines are at the bottom of the labels
    const std::size_t blank = m_labels.size() - std::min(m_labels.size(), m_kept.size());

    for (std::size_t i = 0; i < m_labels.size(); ++i) {
        UiLabel& label = world.get<UiLabel>(m_labels[i]);

        if (i < blank) {
            label.text.clear();
            label.color.a = 0;
            continue;
        }
        const Kept& line = m_kept[i - blank];
        double alpha = 1.0;

        if (!m_open && line.age > m_config.showSeconds) {
            alpha = m_config.fadeSeconds > 0.0 ? std::max(0.0, 1.0 - (line.age - m_config.showSeconds) / m_config.fadeSeconds) : 0.0;
        }
        label.text = line.text;
        label.color = line.color;
        label.color.a = static_cast<std::uint8_t>(alpha * line.color.a);
    }
}
