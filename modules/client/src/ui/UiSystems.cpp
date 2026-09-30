#include "ui/UiSystems.hpp"
#include "Ref.hpp"
#include "Stage.hpp"
#include "Time.hpp"
#include "backend/IRenderer2D.hpp"
#include "input/ActionState.hpp"
#include "input/InputMap.hpp"
#include "ui/TextRenderer.hpp"
#include "ui/Ui.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <string>

namespace
{
    using namespace kuge;

    Rect rectAt(Vec2 position, Vec2 size) { return {position.x, position.y, size.x, size.y}; }

    Vec2 measureText(const IFont* font, const std::string& text, int wrap)
    {
        return font ? font->measure(text, wrap) : Vec2{};
    }

    const IFont* fontOf(const UiLabel& label, const UiTheme& theme)
    {
        return label.font ? label.font.get() : theme.font.get();
    }

    const IFont* fieldFont(const UiTextField& field, const UiTheme& theme)
    {
        return field.font ? field.font.get() : theme.font.get();
    }

    // -- Layout -----------------------------------------------------------------------------------
    class Layouter
    {
        public:
            Layouter(kw::World& world, const UiTheme& theme, UiLayoutResult& result) : m_world(world), m_theme(theme), m_result(result) {}

            void build(const std::vector<kw::Entity>& nodes)
            {
                std::map<kw::Entity, bool> exists;

                for (kw::Entity entity : nodes) {
                    exists[entity] = true;
                }
                for (kw::Entity entity : nodes) {
                    const UiNode& node = m_world.get<UiNode>(entity);

                    if (node.hasParent && node.parent != entity && exists.count(node.parent)) {
                        m_children[node.parent].push_back(entity);
                    } else {
                        m_roots.push_back(entity);
                    }
                }
            }

            void run(const Rect& screen)
            {
                for (kw::Entity root : m_roots) {
                    const UiNode& node = m_world.get<UiNode>(root);

                    if (!node.visible) {
                        continue;
                    }
                    const Vec2 size = fit(root);

                    place(root, rectAt(anchoredPosition(node.anchor, screen, size) + node.offset, size), node.layer, 0);
                }
                std::stable_sort(m_order.begin(), m_order.end(), [](const Drawn& a, const Drawn& b) {
                    if (a.layer != b.layer) { return a.layer < b.layer; }
                    if (a.depth != b.depth) { return a.depth < b.depth; }
                    return a.entity < b.entity;
                });
                for (const Drawn& drawn : m_order) {
                    m_result.drawOrder.push_back(drawn.entity);
                }
            }

        private:
            struct Drawn
            {
                kw::Entity entity;
                int        layer;
                int        depth;
            };

            const std::vector<kw::Entity>& childrenOf(kw::Entity entity) const
            {
                static const std::vector<kw::Entity> none;
                const auto found = m_children.find(entity);

                return found == m_children.end() ? none : found->second;
            }

            // What a node holds, without its own size
            Vec2 content(kw::Entity entity)
            {
                if (m_world.has<UiStack>(entity)) {
                    const UiStack& stack = m_world.get<UiStack>(entity);
                    const bool column = stack.direction == Direction::Vertical;
                    float along = 0.0f;
                    float across = 0.0f;
                    int shown = 0;

                    for (kw::Entity child : childrenOf(entity)) {
                        if (!m_world.get<UiNode>(child).visible) {
                            continue;
                        }
                        const Vec2 size = fit(child);

                        along += column ? size.y : size.x;
                        across = std::max(across, column ? size.x : size.y);
                        ++shown;
                    }
                    along += stack.spacing * static_cast<float>(std::max(shown - 1, 0)) + 2.0f * stack.padding;
                    across += 2.0f * stack.padding;
                    return column ? Vec2{across, along} : Vec2{along, across};
                }
                if (m_world.has<UiButton>(entity)) {
                    const UiButton& button = m_world.get<UiButton>(entity);
                    const Vec2 text = measureText(m_theme.font.get(), button.text, 0);

                    return {text.x + 2.0f * m_theme.padding, text.y + 2.0f * m_theme.padding};
                }
                if (m_world.has<UiTextField>(entity)) {
                    const UiTextField& field = m_world.get<UiTextField>(entity);
                    const Vec2 letter = measureText(fieldFont(field, m_theme), "n", 0);

                    return {letter.x * static_cast<float>(std::max(field.columns, 1)) + 2.0f * m_theme.padding, letter.y + 2.0f * m_theme.padding};
                }
                if (m_world.has<UiLabel>(entity)) {
                    const UiLabel& label = m_world.get<UiLabel>(entity);

                    return measureText(fontOf(label, m_theme), label.text, label.wrapWidth);
                }
                return {};
            }

            // Its size: the one it asks for, or what it holds
            Vec2 fit(kw::Entity entity)
            {
                const auto known = m_sizes.find(entity);

                if (known != m_sizes.end()) {
                    return known->second;
                }
                const UiNode& node = m_world.get<UiNode>(entity);
                Vec2 size = node.size;

                if (size.x <= 0.0f || size.y <= 0.0f) {
                    const Vec2 inside = content(entity);

                    size = {size.x > 0.0f ? size.x : inside.x, size.y > 0.0f ? size.y : inside.y};
                }
                m_sizes[entity] = size;
                return size;
            }

            void place(kw::Entity entity, const Rect& rect, int layer, int depth)
            {
                m_result.rects[entity] = rect;
                m_order.push_back({entity, layer, depth});
                if (m_world.has<UiStack>(entity)) {
                    placeStack(entity, rect, layer, depth);
                    return;
                }
                for (kw::Entity child : childrenOf(entity)) {
                    const UiNode& node = m_world.get<UiNode>(child);

                    if (node.visible) {
                        const Vec2 size = fit(child);

                        place(child, rectAt(anchoredPosition(node.anchor, rect, size) + node.offset, size), std::max(node.layer, layer), depth + 1);
                    }
                }
            }

            void placeStack(kw::Entity entity, const Rect& rect, int layer, int depth)
            {
                const UiStack& stack = m_world.get<UiStack>(entity);
                const bool column = stack.direction == Direction::Vertical;
                const float room = (column ? rect.w : rect.h) - 2.0f * stack.padding;
                float cursor = (column ? rect.y : rect.x) + stack.padding;

                for (kw::Entity child : childrenOf(entity)) {
                    const UiNode& node = m_world.get<UiNode>(child);

                    if (!node.visible) {
                        continue;
                    }
                    Vec2 size = fit(child);
                    float& across = column ? size.x : size.y;
                    const bool sized = (column ? node.size.x : node.size.y) > 0.0f;

                    if (stack.align == Align::Stretch && !sized) {
                        across = std::max(room, 0.0f);
                    }
                    float sideways = (column ? rect.x : rect.y) + stack.padding;

                    if (stack.align == Align::Center) {
                        sideways += (room - across) / 2.0f;
                    } else if (stack.align == Align::End) {
                        sideways += room - across;
                    }
                    place(child, column ? Rect{sideways, cursor, size.x, size.y} : Rect{cursor, sideways, size.x, size.y},
                        std::max(node.layer, layer), depth + 1);
                    cursor += (column ? size.y : size.x) + stack.spacing;
                }
            }

            kw::World&                                        m_world;
            const UiTheme&                                    m_theme;
            UiLayoutResult&                                   m_result;
            std::map<kw::Entity, std::vector<kw::Entity>>     m_children;
            std::vector<kw::Entity>                           m_roots;
            std::map<kw::Entity, Vec2>                        m_sizes;
            std::vector<Drawn>                                m_order;
    };

    // -- Text fields: the text is UTF-8, and the caret goes from one character to the next -------------------
    constexpr float CARET_WIDTH = 2.0f;

    bool startsCharacter(char byte)
    {
        return (static_cast<unsigned char>(byte) & 0xC0) != 0x80;
    }

    std::size_t previousCharacter(const std::string& text, std::size_t at)
    {
        if (at == 0) {
            return 0;
        }
        do {
            --at;
        } while (at > 0 && !startsCharacter(text[at]));
        return at;
    }

    std::size_t nextCharacter(const std::string& text, std::size_t at)
    {
        if (at >= text.size()) {
            return text.size();
        }
        do {
            ++at;
        } while (at < text.size() && !startsCharacter(text[at]));
        return at;
    }

    std::size_t charactersIn(const std::string& text)
    {
        return static_cast<std::size_t>(std::count_if(text.begin(), text.end(), startsCharacter));
    }

    // A caret that is past the text, or in the middle of a character, goes to the start of one
    void settleCaret(UiTextField& field)
    {
        field.caret = std::min(field.caret, field.text.size());
        while (field.caret > 0 && field.caret < field.text.size() && !startsCharacter(field.text[field.caret])) {
            --field.caret;
        }
    }

    // The control characters of ASCII, and the ones of Latin-1 (U+0080 to U+009F)
    bool isControl(std::string_view character)
    {
        const auto first = static_cast<unsigned char>(character[0]);

        return first < 0x20 || first == 0x7F || (first == 0xC2 && character.size() > 1 && static_cast<unsigned char>(character[1]) < 0xA0);
    }

    // What was typed goes in at the caret, one character at a time. Control characters are dropped, and nothing
    // goes over maxLength. @return  whether the text changed
    bool insertTyped(UiTextField& field, const std::string& typed)
    {
        std::size_t count = charactersIn(field.text);
        bool changed = false;

        for (std::size_t at = 0; at < typed.size();) {
            const std::size_t end = nextCharacter(typed, at);
            const std::string_view character(typed.data() + at, end - at);

            at = end;
            if (isControl(character)) {
                continue;
            }
            if (count >= field.maxLength) {
                break;
            }
            field.text.insert(field.caret, character);
            field.caret += character.size();
            ++count;
            changed = true;
        }
        return changed;
    }

    // Backspace, Delete and the keys that move the caret. @return  whether the text changed
    bool editWith(UiTextField& field, Key key)
    {
        switch (key) {
            case Key::Backspace: {
                const std::size_t from = previousCharacter(field.text, field.caret);

                if (from == field.caret) {
                    return false;
                }
                field.text.erase(from, field.caret - from);
                field.caret = from;
                return true;
            }
            case Key::Delete: {
                const std::size_t to = nextCharacter(field.text, field.caret);

                if (to == field.caret) {
                    return false;
                }
                field.text.erase(field.caret, to - field.caret);
                return true;
            }
            case Key::Left:  field.caret = previousCharacter(field.text, field.caret); return false;
            case Key::Right: field.caret = nextCharacter(field.text, field.caret); return false;
            case Key::Home:  field.caret = 0; return false;
            case Key::End:   field.caret = field.text.size(); return false;
            default:         return false;
        }
    }

    // The part of the text that shows in `room` pixels: it starts late enough for the caret to be in it, and it
    // stops where the rest no longer fits. (There is no clipping in the renderer: only what fits is drawn.)
    struct Shown
    {
        std::size_t first;
        std::size_t last;
    };

    Shown shownPart(const IFont* font, const UiTextField& field, float room)
    {
        Shown shown{0, field.text.size()};
        const std::string_view text = field.text;

        if (!font) {
            return shown;
        }
        while (shown.first < field.caret && font->measure(text.substr(shown.first, field.caret - shown.first)).x > room) {
            shown.first = nextCharacter(field.text, shown.first);
        }
        while (shown.last > field.caret && font->measure(text.substr(shown.first, shown.last - shown.first)).x > room) {
            shown.last = previousCharacter(field.text, shown.last);
        }
        return shown;
    }

    // Where a click puts the caret: between the two characters that the click is the nearest to
    std::size_t caretAt(const IFont* font, const UiTextField& field, float room, float x)
    {
        const Shown shown = shownPart(font, field, room);
        const std::string_view text = field.text;
        std::size_t best = shown.first;
        float bestGap = -1.0f;

        for (std::size_t at = shown.first;; at = nextCharacter(field.text, at)) {
            const float here = font ? font->measure(text.substr(shown.first, at - shown.first)).x : 0.0f;
            const float gap = std::fabs(here - x);

            if (bestGap < 0.0f || gap < bestGap) {
                best = at;
                bestGap = gap;
            }
            if (at >= shown.last) {
                break;
            }
        }
        return best;
    }

    // -- Interaction ------------------------------------------------------------------------------
    bool pressed(const ActionState& state, int action)
    {
        return action >= 0 && state.wasPressed(action);
    }

    bool held(const ActionState& state, int action)
    {
        return action >= 0 && state.isDown(action);
    }

    Vec2 centerOf(const Rect& rect) { return rect.center(); }

    // The button that is next in a direction: the nearest one ahead, and a bit
    // less keen on those that are far to the side
    std::optional<kw::Entity> neighbour(const std::vector<kw::Entity>& buttons, const UiLayoutResult& layout,
        kw::Entity from, Vec2 direction)
    {
        const Vec2 here = centerOf(layout.rects.at(from));
        std::optional<kw::Entity> best;
        float bestScore = 0.0f;

        for (kw::Entity candidate : buttons) {
            if (candidate == from) {
                continue;
            }
            const Vec2 apart = centerOf(layout.rects.at(candidate)) - here;
            const float along = apart.dot(direction);
            const float across = std::fabs(apart.x * direction.y - apart.y * direction.x);

            if (along <= 0.5f) {
                continue;   // not ahead
            }
            const float score = along + 2.0f * across;

            if (!best || score < bestScore) {
                best = candidate;
                bestScore = score;
            }
        }
        return best;
    }

    void border(IRenderer2D& renderer, const Rect& rect, Color color, float width)
    {
        if (width <= 0.0f) {
            return;
        }
        width = std::min(width, std::min(rect.w, rect.h) / 2.0f);
        renderer.fillRect({rect.x, rect.y, rect.w, width}, color);
        renderer.fillRect({rect.x, rect.bottom() - width, rect.w, width}, color);
        renderer.fillRect({rect.x, rect.y + width, width, rect.h - 2.0f * width}, color);
        renderer.fillRect({rect.right() - width, rect.y + width, width, rect.h - 2.0f * width}, color);
    }
}

Vec2 kuge::anchoredPosition(Anchor anchor, const Rect& area, Vec2 size)
{
    const int index = static_cast<int>(anchor);
    const float across = static_cast<float>(index % 3) / 2.0f;    // 0: left, 1: right
    const float down = static_cast<float>(index / 3) / 2.0f;      // 0: top, 1: bottom

    return {area.x + (area.w - size.x) * across, area.y + (area.h - size.y) * down};
}

bool kuge::UiLayout::handle(kw::World& world)
{
    auto& result = world.getResource<UiLayoutResult>();
    const Vec2 screen = world.getResource<Ref<IRenderer2D>>()->outputSize();

    result.rects.clear();
    result.drawOrder.clear();
    m_nodes.clear();
    auto view = world.view<UiNode>();
    for (kw::Entity entity : view) {
        m_nodes.push_back(entity);
    }
    std::sort(m_nodes.begin(), m_nodes.end());

    Layouter layouter(world, world.getResource<UiTheme>(), result);

    layouter.build(m_nodes);
    layouter.run({0.0f, 0.0f, screen.x, screen.y});
    return true;
}

bool kuge::UiInteract::handle(kw::World& world)
{
    auto& events = world.getResource<UiEvents>().list;
    auto& state = world.getResource<UiState>();
    const auto& layout = world.getResource<UiLayoutResult>();
    const auto& actions = world.getResource<UiActions>();
    const auto& input = world.getResource<ActionState>();
    const auto& theme = world.getResource<UiTheme>();
    InputMap& inputMap = *world.getResource<Ref<InputMap>>();
    const Vec2 mouse = inputMap.mousePosition();
    std::vector<kw::Entity> all;
    std::vector<kw::Entity> usable;   // what can take the focus: buttons and text fields, shown and enabled

    events.clear();
    // What was typed since the last tick. Taken even if nothing reads it, so that it does not wait for a field
    // that comes later and type into it.
    const std::string typed = inputMap.takeTyped();
    const std::vector<Key> keys = inputMap.takeEditKeys();
    auto buttons = world.view<UiNode, UiButton>();
    for (kw::Entity entity : buttons) {
        all.push_back(entity);
    }
    auto fields = world.view<UiNode, UiTextField>();
    for (kw::Entity entity : fields) {
        all.push_back(entity);
    }
    std::sort(all.begin(), all.end());   // (the order of the entities: the order of Tab)
    for (kw::Entity entity : all) {
        bool enabled = false;

        if (world.has<UiButton>(entity)) {
            UiButton& button = world.get<UiButton>(entity);

            button.hovered = button.focused = button.pressed = false;
            enabled = button.enabled;
        } else {
            UiTextField& field = world.get<UiTextField>(entity);

            field.hovered = field.focused = false;
            enabled = field.enabled;
        }
        if (enabled && layout.rects.count(entity)) {
            usable.push_back(entity);
        }
    }
    const auto onField = [&] { return state.hasFocus && world.has<UiTextField>(state.focus); };
    const bool cancelled = pressed(input, actions.cancel);
    auto focusOn = [&](kw::Entity entity) {
        if (!state.hasFocus || state.focus != entity) {
            state.focus = entity;
            state.hasFocus = true;
            events.push_back({UiEvent::Kind::Focused, entity, true});
        }
    };

    if (usable.empty()) {
        state.hasFocus = false;
        if (cancelled) {
            events.push_back({UiEvent::Kind::Cancelled, 0, false});
        }
        return true;
    }
    if (state.hasFocus && std::find(usable.begin(), usable.end(), state.focus) == usable.end()) {
        state.hasFocus = false;   // it went, or cannot be pressed any more
    }
    const bool typing = onField();   // (before the mouse and the actions move the focus)
    // The button under the mouse: the one drawn last (on top) among those under it
    std::optional<kw::Entity> hovered;
    for (auto it = layout.drawOrder.rbegin(); it != layout.drawOrder.rend() && !hovered; ++it) {
        if (std::find(usable.begin(), usable.end(), *it) != usable.end() && layout.rects.at(*it).contains(mouse)) {
            hovered = *it;
        }
    }
    const bool moved = mouse.x != state.lastMouse.x || mouse.y != state.lastMouse.y;

    state.lastMouse = mouse;
    if (moved && hovered && !typing) {
        focusOn(*hovered);         // the mouse takes the focus only when it moves: the keyboard keeps it otherwise
    }                              // (and not from a text field: a click is what takes the focus away from what is typed in)
    if (!state.hasFocus) {
        focusOn(usable.front());   // a menu always has something selected
    }
    const struct { int action; Vec2 direction; } moves[] = {
        {actions.up, {0.0f, -1.0f}}, {actions.down, {0.0f, 1.0f}}, {actions.left, {-1.0f, 0.0f}}, {actions.right, {1.0f, 0.0f}},
    };
    for (const auto& move : moves) {
        // In a text field the keys that a game binds to moving the focus are letters too: they type
        if (!onField() && pressed(input, move.action)) {
            if (const auto next = neighbour(usable, layout, state.focus, move.direction)) {
                focusOn(*next);
            }
        }
    }
    if (pressed(input, actions.click) && hovered) {
        focusOn(*hovered);
        if (world.has<UiTextField>(*hovered)) {
            // Only focused, and the caret goes where the click is
            UiTextField& field = world.get<UiTextField>(*hovered);
            const Rect& rect = layout.rects.at(*hovered);
            const float room = rect.w - 2.0f * theme.padding - CARET_WIDTH;

            settleCaret(field);
            field.caret = caretAt(fieldFont(field, theme), field, room, mouse.x - (rect.x + theme.padding));
        } else {
            events.push_back({UiEvent::Kind::Activated, *hovered, true});
        }
    }
    if (pressed(input, actions.accept) && !onField()) {
        events.push_back({UiEvent::Kind::Activated, state.focus, true});
    }
    if (cancelled) {
        events.push_back({UiEvent::Kind::Cancelled, state.focus, state.hasFocus});
    }
    // What is typed and the editing keys: into the field that has the focus. Tab goes on to the next one.
    bool tab = false;

    if (onField()) {
        UiTextField& field = world.get<UiTextField>(state.focus);
        bool changed = false;
        bool submitted = false;

        settleCaret(field);
        changed = insertTyped(field, typed);
        for (Key key : keys) {
            if (key == Key::Enter || key == Key::KpEnter) {
                submitted = true;
            } else if (key == Key::Tab) {
                tab = true;
            } else {
                changed = editWith(field, key) || changed;
            }
        }
        if (changed) {
            events.push_back({UiEvent::Kind::Changed, state.focus, true});
        }
        if (submitted) {
            events.push_back({UiEvent::Kind::Submitted, state.focus, true});
        }
    } else {
        tab = std::find(keys.begin(), keys.end(), Key::Tab) != keys.end();
    }
    if (tab && state.hasFocus) {
        const auto here = std::find(usable.begin(), usable.end(), state.focus);

        focusOn(here == usable.end() || std::next(here) == usable.end() ? usable.front() : *std::next(here));
    }
    for (kw::Entity entity : usable) {
        const bool focused = state.hasFocus && entity == state.focus;
        const bool over = hovered && *hovered == entity;

        if (world.has<UiButton>(entity)) {
            UiButton& button = world.get<UiButton>(entity);

            button.focused = focused;
            button.hovered = over;
            button.pressed = (button.focused && held(input, actions.accept)) || (button.hovered && held(input, actions.click));
        } else {
            UiTextField& field = world.get<UiTextField>(entity);

            field.focused = focused;
            field.hovered = over;
        }
    }
    return true;
}

bool kuge::UiRender::handle(kw::World& world)
{
    IRenderer2D& renderer = *world.getResource<Ref<IRenderer2D>>();
    TextRenderer& text = *world.getResource<Ref<TextRenderer>>();
    const auto& theme = world.getResource<UiTheme>();
    const auto& layout = world.getResource<UiLayoutResult>();

    for (kw::Entity entity : layout.drawOrder) {
        const Rect rect = layout.rects.at(entity);

        if (world.has<UiPanel>(entity)) {
            const UiPanel& panel = world.get<UiPanel>(entity);

            renderer.fillRect(rect, panel.fill);
            border(renderer, rect, panel.border, panel.borderWidth);
        }
        if (world.has<UiButton>(entity)) {
            const UiButton& button = world.get<UiButton>(entity);
            Color fill = theme.buttonFill;

            if (!button.enabled) {
                fill = theme.buttonDisabled;
            } else if (button.pressed) {
                fill = theme.buttonPressed;
            } else if (button.hovered || button.focused) {
                fill = theme.buttonHover;
            }
            renderer.fillRect(rect, fill);
            if (button.focused) {
                border(renderer, rect, theme.focusRing, theme.focusWidth);
            }
            if (theme.font) {
                const Vec2 size = theme.font->measure(button.text, 0);

                text.draw(*theme.font, button.text, {std::round(rect.x + (rect.w - size.x) / 2.0f), std::round(rect.y + (rect.h - size.y) / 2.0f)},
                    button.enabled ? theme.buttonText : theme.disabledText);
            }
        }
        if (world.has<UiTextField>(entity)) {
            const UiTextField& field = world.get<UiTextField>(entity);
            const IFont* font = fieldFont(field, theme);

            renderer.fillRect(rect, field.enabled ? theme.fieldFill : theme.buttonDisabled);
            border(renderer, rect, field.focused ? theme.focusRing : theme.fieldBorder, field.focused ? theme.focusWidth : 2.0f);
            if (font) {
                const float room = rect.w - 2.0f * theme.padding - CARET_WIDTH;
                const float lineHeight = static_cast<float>(font->lineHeight());
                const float top = std::round(rect.y + (rect.h - lineHeight) / 2.0f);
                const float left = rect.x + theme.padding;

                if (field.text.empty()) {
                    if (!field.placeholder.empty()) {
                        text.draw(*font, field.placeholder, {std::round(left), top}, theme.fieldPlaceholder);
                    }
                } else {
                    const Shown shown = shownPart(font, field, room);

                    text.draw(*font, std::string_view(field.text).substr(shown.first, shown.last - shown.first), {std::round(left), top},
                        field.enabled ? theme.fieldText : theme.disabledText);
                }
                const std::uint32_t blink = theme.caretBlinkTicks;
                const bool lit = blink == 0 || (world.getResource<Time>().tick / blink) % 2 == 0;

                if (field.focused && field.enabled && lit) {
                    const Shown shown = shownPart(font, field, room);
                    const std::size_t caret = std::min(field.caret, field.text.size());
                    const float before = caret > shown.first ? font->measure(std::string_view(field.text).substr(shown.first, caret - shown.first)).x : 0.0f;

                    renderer.fillRect({std::round(left + before), top, CARET_WIDTH, lineHeight}, theme.fieldCaret);
                }
            }
        }
        if (world.has<UiLabel>(entity)) {
            const UiLabel& label = world.get<UiLabel>(entity);
            const IFont* font = fontOf(label, theme);

            if (font && !label.text.empty()) {
                const Vec2 size = font->measure(label.text, label.wrapWidth);
                float x = rect.x;

                if (label.align == TextAlign::Center) {
                    x += (rect.w - size.x) / 2.0f;
                } else if (label.align == TextAlign::Right) {
                    x += rect.w - size.x;
                }
                text.draw(*font, label.text, {std::round(x), std::round(rect.y)}, label.color, label.wrapWidth);
            }
        }
    }
    return true;
}

void kuge::installUi(SceneSetup scene, UiActions actions)
{
    auto& world = scene.world();

    world.addResource<UiTheme>();
    world.addResource<UiState>();
    world.addResource<UiLayoutResult>();
    world.addResource<UiEvents>();
    world.addResource<UiActions>(actions);
    scene.addSystem(kw::Schedule::Fixed, stage::Input, std::make_unique<UiInteract>());
    scene.addSystem(kw::Schedule::Frame, stage::Late, std::make_unique<UiLayout>());
    scene.addSystem(kw::Schedule::Frame, stage::Render, std::make_unique<UiRender>());
}
