#include "input/InputMap.hpp"
#include "Logger.hpp"
#include <algorithm>
#include <format>

namespace
{
    constexpr std::string_view PREFIX = "input.";

    template<typename Table>
    bool inRange(std::size_t index, const Table& table)
    {
        return index < table.size();
    }

    std::vector<std::string_view> split(std::string_view text)
    {
        std::vector<std::string_view> parts;

        while (true) {
            const auto comma = text.find(',');

            parts.push_back(text.substr(0, comma));
            if (comma == std::string_view::npos) {
                return parts;
            }
            text = text.substr(comma + 1);
        }
    }
}

void kuge::InputMap::declareAction(ActionId action, std::string name)
{
    if (name.empty() || name.find_first_of(" \t=,#;[") != std::string::npos) {
        throw std::invalid_argument(std::format("invalid action name '{}'", name));
    }
    for (std::size_t other = 0; other < MAX_ACTIONS; ++other) {
        if (other != action && m_names[other] == name) {
            throw std::invalid_argument(std::format("the action name '{}' is already taken", name));
        }
    }
    m_names[action] = std::move(name);
}

void kuge::InputMap::bindAction(ActionId action, Binding binding)
{
    auto& list = m_bindings[action];

    if (std::find(list.begin(), list.end(), binding) == list.end()) {
        list.push_back(binding);
    }
    refresh(false);
}

void kuge::InputMap::unbindAction(ActionId action, Binding binding)
{
    auto& list = m_bindings[action];

    list.erase(std::remove(list.begin(), list.end(), binding), list.end());
    refresh(false);
}

bool kuge::InputMap::isActive(const Binding& binding) const
{
    switch (binding.kind) {
        case Binding::Kind::Key:
            return inRange(binding.code, m_keys) && m_keys[binding.code];
        case Binding::Kind::MouseButton:
            return inRange(binding.code, m_mouseButtons) && m_mouseButtons[binding.code];
        case Binding::Kind::GamepadButton:
            return std::any_of(m_pads.begin(), m_pads.end(), [&](const auto& pad) {
                return inRange(binding.code, pad.second.buttons) && pad.second.buttons[binding.code];
            });
        case Binding::Kind::GamepadAxis:
            return std::any_of(m_pads.begin(), m_pads.end(), [&](const auto& pad) {
                return inRange(binding.code, pad.second.axes)
                    && pad.second.axes[binding.code] * binding.direction > m_axisThreshold;
            });
    }
    return false;
}

std::uint64_t kuge::InputMap::computeDown(void) const
{
    std::uint64_t down = 0;

    for (std::size_t action = 0; action < MAX_ACTIONS; ++action) {
        const auto& list = m_bindings[action];

        if (std::any_of(list.begin(), list.end(), [this](const Binding& b) { return isActive(b); })) {
            down |= std::uint64_t{1} << action;
        }
    }
    return down;
}

// latchEdges: remember the actions that went down or up since the last
// sampleTick(), even if they come back before it
void kuge::InputMap::refresh(bool latchEdges)
{
    const std::uint64_t now = computeDown();

    if (latchEdges) {
        m_pressed |= now & ~m_down;
        m_released |= ~now & m_down;
    }
    m_down = now;
}

void kuge::InputMap::apply(const Event& event)
{
    if (const auto* key = std::get_if<KeyEvent>(&event)) {
        const auto code = static_cast<std::size_t>(key->key);

        if (!key->repeat && inRange(code, m_keys) && key->key != Key::Unknown) {
            m_keys[code] = key->down;
        }
    } else if (const auto* button = std::get_if<MouseButtonEvent>(&event)) {
        const auto code = static_cast<std::size_t>(button->button);

        m_mouse = button->position;
        if (inRange(code, m_mouseButtons)) {
            m_mouseButtons[code] = button->down;
        }
    } else if (const auto* move = std::get_if<MouseMoveEvent>(&event)) {
        m_mouse = move->position;
    } else if (const auto* pad = std::get_if<GamepadButtonEvent>(&event)) {
        const auto code = static_cast<std::size_t>(pad->button);

        if (inRange(code, m_pads[pad->pad].buttons)) {
            m_pads[pad->pad].buttons[code] = pad->down;
        }
    } else if (const auto* axis = std::get_if<GamepadAxisEvent>(&event)) {
        const auto code = static_cast<std::size_t>(axis->axis);

        if (inRange(code, m_pads[axis->pad].axes)) {
            m_pads[axis->pad].axes[code] = axis->value;
        }
    } else if (const auto* connection = std::get_if<GamepadConnectionEvent>(&event)) {
        if (connection->connected) {
            m_pads[connection->pad];
        } else {
            m_pads.erase(connection->pad);
        }
    }
}

// What the event would be as a binding, if it is a push (not a release)
std::optional<kuge::Binding> kuge::InputMap::capturedBy(const Event& event) const
{
    if (const auto* key = std::get_if<KeyEvent>(&event)) {
        if (key->down && !key->repeat && key->key != Key::Unknown) {
            return Binding(key->key);
        }
    } else if (const auto* button = std::get_if<MouseButtonEvent>(&event)) {
        if (button->down) {
            return Binding(button->button);
        }
    } else if (const auto* pad = std::get_if<GamepadButtonEvent>(&event)) {
        if (pad->down) {
            return Binding(pad->button);
        }
    } else if (const auto* axis = std::get_if<GamepadAxisEvent>(&event)) {
        if (axis->value > m_axisThreshold) {
            return Binding::axis(axis->axis, 1);
        }
        if (axis->value < -m_axisThreshold) {
            return Binding::axis(axis->axis, -1);
        }
    }
    return std::nullopt;
}

void kuge::InputMap::handle(const Event& event)
{
    if (m_capturing) {
        // Consumed: no action reacts to what the player is choosing
        if (const auto binding = capturedBy(event)) {
            m_captured = binding;
            m_capturing = false;
        }
        apply(event);
        refresh(false);
        return;
    }
    apply(event);
    refresh(true);
}

kuge::ActionState kuge::InputMap::sampleTick(std::uint64_t tick)
{
    ActionState state;

    state.tick = tick;
    state.down = m_down;
    state.pressed = m_pressed;
    state.released = m_released;
    m_pressed = 0;
    m_released = 0;
    return state;
}

void kuge::InputMap::startCapture(void) noexcept
{
    m_capturing = true;
    m_captured.reset();
}

void kuge::InputMap::cancelCapture(void) noexcept
{
    m_capturing = false;
    m_captured.reset();
}

std::optional<kuge::Binding> kuge::InputMap::takeCaptured(void) noexcept
{
    std::optional<Binding> taken = m_captured;

    m_captured.reset();
    return taken;
}

void kuge::InputMap::save(ConfigFile& config) const
{
    for (std::size_t action = 0; action < MAX_ACTIONS; ++action) {
        std::string value;

        if (m_names[action].empty()) {
            continue;
        }
        for (const Binding& binding : m_bindings[action]) {
            value += (value.empty() ? "" : ", ") + binding.toString();
        }
        config.set(std::string(PREFIX) + m_names[action], value);
    }
}

void kuge::InputMap::load(const ConfigFile& config)
{
    for (std::size_t action = 0; action < MAX_ACTIONS; ++action) {
        const std::string key = std::string(PREFIX) + m_names[action];
        std::vector<Binding> loaded;

        if (m_names[action].empty() || !config.has(key)) {
            continue;
        }
        // Kept in a variable: the parts are views on it
        const std::string value = config.getString(key);

        for (std::string_view part : split(value)) {
            if (part.find_first_not_of(" \t") == std::string_view::npos) {
                continue;   // "input.shoot =" means: nothing is bound to it
            }
            if (const auto binding = Binding::parse(part)) {
                if (std::find(loaded.begin(), loaded.end(), *binding) == loaded.end()) {
                    loaded.push_back(*binding);
                }
            } else {
                Logger::logger().warn("keybinds: '{}' is not an input (for '{}')", part, m_names[action]);
            }
        }
        m_bindings[action] = std::move(loaded);
    }
    refresh(false);
}

bool kuge::InputMap::loadBindings(const std::filesystem::path& path)
{
    ConfigFile config;

    try {
        if (!config.load(path)) {
            return false;
        }
    } catch (const ConfigError& error) {
        Logger::logger().error("keybinds: cannot use '{}': {}", path.string(), error.what());
        return false;
    }
    load(config);
    return true;
}

void kuge::InputMap::saveBindings(const std::filesystem::path& path) const
{
    ConfigFile config;

    save(config);
    config.save(path);
}
