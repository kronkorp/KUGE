#include "ui/UiSystems.hpp"
#include "Ref.hpp"
#include "Stage.hpp"
#include "Time.hpp"
#include "backend/IRenderer2D.hpp"
#include "backend/IWindow.hpp"
#include "input/ActionState.hpp"
#include "input/InputMap.hpp"
#include "ui/TextRenderer.hpp"
#include "ui/Ui.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <optional>
#include <string>

namespace
{
    using namespace kuge;

    constexpr float CHECK_GAP = 8.0f;      // between the box of a check box and its text
    constexpr float HANDLE_WIDTH = 12.0f;  // of a slider

    Rect intersection(const Rect& a, const Rect& b)
    {
        const float left = std::max(a.x, b.x);
        const float top = std::max(a.y, b.y);

        return {left, top, std::max(std::min(a.right(), b.right()) - left, 0.0f), std::max(std::min(a.bottom(), b.bottom()) - top, 0.0f)};
    }

    Rect rectAt(Vec2 position, Vec2 size) { return {position.x, position.y, size.x, size.y}; }

    Vec2 measureText(const IFont* font, const std::string& text, int wrap)
    {
        return font ? font->measure(text, wrap) : Vec2{};
    }

    // The font of a text at the scale of the interface (the theme's is already: see scaledTheme())
    const IFont* scaledFont(const std::shared_ptr<const IFont>& font, float scale)
    {
        if (!font || scale == 1.0f) {
            return font.get();
        }
        const auto bigger = font->scaled(scale);   // (kept by the font: the pointer lives as long as it does)

        return bigger ? bigger.get() : font.get();
    }

    const IFont* fontOf(const UiLabel& label, const UiTheme& theme, float scale)
    {
        return label.font ? scaledFont(label.font, scale) : theme.font.get();
    }

    const IFont* fieldFont(const UiTextField& field, const UiTheme& theme, float scale)
    {
        return field.font ? scaledFont(field.font, scale) : theme.font.get();
    }

    float scaleOf(kw::World& world)
    {
        const float value = world.getResource<UiScale>().value;

        return value > 0.0f && std::isfinite(value) ? value : 1.0f;
    }

    // The theme with its pixel numbers and its font multiplied by the scale
    UiTheme scaledTheme(const UiTheme& theme, float scale)
    {
        UiTheme scaled = theme;

        if (scale != 1.0f) {
            scaled.padding *= scale;
            scaled.focusWidth *= scale;
            scaled.progressWidth *= scale;
            scaled.progressHeight *= scale;
            scaled.font = std::shared_ptr<const IFont>(theme.font, scaledFont(theme.font, scale));   // (aliasing: the original keeps it alive)
        }
        return scaled;
    }

    // -- Layout -----------------------------------------------------------------------------------
    class Layouter
    {
        public:
            Layouter(kw::World& world, const UiTheme& theme, UiLayoutResult& result, float scale)
                : m_world(world), m_theme(theme), m_result(result), m_scale(scale) {}

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

                    place(root, rectAt(anchoredPosition(node.anchor, screen, size) + node.offset * m_scale, size), node.layer, 0, std::nullopt);
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
                    along += stack.spacing * m_scale * static_cast<float>(std::max(shown - 1, 0)) + 2.0f * stack.padding * m_scale;
                    across += 2.0f * stack.padding * m_scale;
                    return column ? Vec2{across, along} : Vec2{along, across};
                }
                if (m_world.has<UiButton>(entity)) {
                    const UiButton& button = m_world.get<UiButton>(entity);
                    const Vec2 text = measureText(m_theme.font.get(), button.text, 0);

                    return {text.x + 2.0f * m_theme.padding, text.y + 2.0f * m_theme.padding};
                }
                if (m_world.has<UiTextField>(entity)) {
                    const UiTextField& field = m_world.get<UiTextField>(entity);
                    const Vec2 letter = measureText(fieldFont(field, m_theme, m_scale), "n", 0);

                    return {letter.x * static_cast<float>(std::max(field.columns, 1)) + 2.0f * m_theme.padding, letter.y + 2.0f * m_theme.padding};
                }
                if (m_world.has<UiImage>(entity)) {
                    const UiImage& image = m_world.get<UiImage>(entity);

                    if (!image.texture) {
                        return {};
                    }
                    return (image.source.w > 0.0f && image.source.h > 0.0f ? Vec2{image.source.w, image.source.h} : image.texture->size()) * m_scale;
                }
                if (m_world.has<UiSlider>(entity)) {
                    const Vec2 letter = measureText(m_theme.font.get(), "n", 0);

                    return {letter.x * static_cast<float>(std::max(m_world.get<UiSlider>(entity).columns, 1)) + 2.0f * m_theme.padding,
                        letter.y + 2.0f * m_theme.padding};
                }
                if (m_world.has<UiCheckbox>(entity)) {
                    const Vec2 text = measureText(m_theme.font.get(), m_world.get<UiCheckbox>(entity).text, 0);
                    const float line = m_theme.font ? static_cast<float>(m_theme.font->lineHeight()) : 0.0f;

                    return {line + CHECK_GAP * m_scale + text.x, line};   // (the box is a square of the height of a line)
                }
                if (m_world.has<UiProgress>(entity)) {
                    const Vec2 bar{m_theme.progressWidth, m_theme.progressHeight};

                    return m_world.get<UiProgress>(entity).vertical ? Vec2{bar.y, bar.x} : bar;
                }
                if (m_world.has<UiLabel>(entity)) {
                    const UiLabel& label = m_world.get<UiLabel>(entity);

                    return measureText(fontOf(label, m_theme, m_scale), label.text, static_cast<int>(static_cast<float>(label.wrapWidth) * m_scale));
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
                Vec2 size = node.size * m_scale;

                if (size.x <= 0.0f || size.y <= 0.0f) {
                    const Vec2 inside = content(entity);

                    size = {size.x > 0.0f ? size.x : inside.x, size.y > 0.0f ? size.y : inside.y};
                }
                m_sizes[entity] = size;
                return size;
            }

            // `clip`: the part of the screen where it can be seen, if a UiScroll is above it. What is outside is still
            // placed (the focus can go to it, and bring it into view) but not drawn.
            void place(kw::Entity entity, const Rect& rect, int layer, int depth, const std::optional<Rect>& clip)
            {
                m_result.rects[entity] = rect;
                if (clip) {
                    m_result.clips[entity] = *clip;
                }
                if (!clip || rect.intersects(*clip)) {
                    m_order.push_back({entity, layer, depth});
                }
                std::optional<Rect> inner = clip;

                if (m_world.has<UiScroll>(entity)) {
                    // What scrolls stays inside the border of the panel of the node, not over it
                    const float edge = m_world.has<UiPanel>(entity) ? std::min(m_world.get<UiPanel>(entity).borderWidth * m_scale, std::min(rect.w, rect.h) / 2.0f) : 0.0f;
                    const Rect view{rect.x + edge, rect.y + edge, rect.w - 2.0f * edge, rect.h - 2.0f * edge};

                    inner = clip ? intersection(*clip, view) : view;
                }

                if (m_world.has<UiStack>(entity)) {
                    placeStack(entity, rect, layer, depth, inner);
                    return;
                }
                for (kw::Entity child : childrenOf(entity)) {
                    const UiNode& node = m_world.get<UiNode>(child);

                    if (node.visible) {
                        const Vec2 size = fit(child);

                        place(child, rectAt(anchoredPosition(node.anchor, rect, size) + node.offset * m_scale, size), std::max(node.layer, layer), depth + 1, inner);
                    }
                }
            }

            void placeStack(kw::Entity entity, const Rect& rect, int layer, int depth, const std::optional<Rect>& clip)
            {
                const UiStack& stack = m_world.get<UiStack>(entity);
                const bool column = stack.direction == Direction::Vertical;
                const float padding = stack.padding * m_scale;
                const float spacing = stack.spacing * m_scale;
                const float room = (column ? rect.w : rect.h) - 2.0f * padding;
                float cursor = (column ? rect.y : rect.x) + padding;

                if (m_world.has<UiScroll>(entity)) {
                    UiScroll& scroll = m_world.get<UiScroll>(entity);
                    const Vec2 whole = content(entity);

                    // (in the units of the components, not of the screen: they keep their numbers when the scale changes)
                    scroll.extent = (column ? whole.y : whole.x) / m_scale;
                    scroll.offset = std::isfinite(scroll.offset) ? std::clamp(scroll.offset, 0.0f, std::max(scroll.extent - (column ? rect.h : rect.w) / m_scale, 0.0f)) : 0.0f;
                    cursor -= scroll.offset * m_scale;
                }

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
                    float sideways = (column ? rect.x : rect.y) + padding;

                    if (stack.align == Align::Center) {
                        sideways += (room - across) / 2.0f;
                    } else if (stack.align == Align::End) {
                        sideways += room - across;
                    }
                    place(child, column ? Rect{sideways, cursor, size.x, size.y} : Rect{cursor, sideways, size.x, size.y},
                        std::max(node.layer, layer), depth + 1, clip);
                    cursor += (column ? size.y : size.x) + spacing;
                }
            }

            kw::World&                                        m_world;
            const UiTheme&                                    m_theme;
            UiLayoutResult&                                   m_result;
            std::map<kw::Entity, std::vector<kw::Entity>>     m_children;
            std::vector<kw::Entity>                           m_roots;
            std::map<kw::Entity, Vec2>                        m_sizes;
            std::vector<Drawn>                                m_order;
            float                                             m_scale;
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

    // Ctrl+V pastes the first line of the clipboard at the caret, Ctrl+C copies the whole text (there is no
    // selection yet), Ctrl+X copies it and empties the field. @return  whether the text changed
    bool clipboardWith(UiTextField& field, Key key, IWindow& window)
    {
        if (key == Key::C || key == Key::X) {
            window.setClipboardText(field.text);
            if (key == Key::X && !field.text.empty()) {
                field.text.clear();
                field.caret = 0;
                return true;
            }
            return false;
        }
        const std::string pasted = window.clipboardText();

        return insertTyped(field, pasted.substr(0, pasted.find_first_of("\r\n")));
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

    // -- Sliders: where the value is in the rectangle, from 0 (left) to 1 (right) ----------------------------
    float slidRatio(const UiSlider& slider)
    {
        const float range = slider.max - slider.min;

        return range > 0.0f ? std::clamp((slider.value - slider.min) / range, 0.0f, 1.0f) : 0.0f;
    }

    // -- Interaction ------------------------------------------------------------------------------
    // The scroll node under the mouse (the one drawn last, so the innermost) takes the wheel
    void scrollBy(kw::World& world, const UiLayoutResult& layout, Vec2 mouse, float notches, float scale)
    {
        for (auto it = layout.drawOrder.rbegin(); it != layout.drawOrder.rend(); ++it) {
            if (!world.has<UiScroll>(*it) || !layout.rects.at(*it).contains(mouse)) {
                continue;
            }
            UiScroll& scroll = world.get<UiScroll>(*it);
            const Rect& rect = layout.rects.at(*it);
            const bool column = !world.has<UiStack>(*it) || world.get<UiStack>(*it).direction == Direction::Vertical;

            scroll.offset = std::clamp(scroll.offset - notches * scroll.notch, 0.0f, std::max(scroll.extent - (column ? rect.h : rect.w) / scale, 0.0f));
            return;
        }
    }

    // The focus went to `target`: the scroll nodes around it move so that it is seen
    void reveal(kw::World& world, const UiLayoutResult& layout, kw::Entity target, float scale)
    {
        const auto found = layout.rects.find(target);

        if (found == layout.rects.end()) {
            return;
        }
        const Rect inside = found->second;

        for (kw::Entity at = target; world.has<UiNode>(at);) {
            const UiNode& node = world.get<UiNode>(at);

            if (!node.hasParent || node.parent == at) {
                return;
            }
            at = node.parent;
            if (!world.has<UiScroll>(at) || !layout.rects.count(at)) {
                continue;
            }
            const Rect& view = layout.rects.at(at);
            const bool column = !world.has<UiStack>(at) || world.get<UiStack>(at).direction == Direction::Vertical;
            const float before = column ? inside.y - view.y : inside.x - view.x;
            const float after = column ? inside.bottom() - view.bottom() : inside.right() - view.right();

            world.get<UiScroll>(at).offset += (before < 0.0f ? before : std::max(after, 0.0f)) / scale;
        }
    }

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
    result.clips.clear();
    result.drawOrder.clear();
    m_nodes.clear();
    auto view = world.view<UiNode>();
    for (kw::Entity entity : view) {
        m_nodes.push_back(entity);
    }
    std::sort(m_nodes.begin(), m_nodes.end());

    const float scale = scaleOf(world);
    const UiTheme theme = scaledTheme(world.getResource<UiTheme>(), scale);
    Layouter layouter(world, theme, result, scale);

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
    const float scale = scaleOf(world);
    const UiTheme theme = scaledTheme(world.getResource<UiTheme>(), scale);
    InputMap& inputMap = *world.getResource<Ref<InputMap>>();
    const Vec2 mouse = inputMap.mousePosition();
    std::vector<kw::Entity> all;
    std::vector<kw::Entity> usable;   // what can take the focus: buttons, check boxes, sliders and text fields, shown and enabled

    events.clear();
    // What was typed since the last tick. Taken even if nothing reads it, so that it does not wait for a field
    // that comes later and type into it.
    const std::string typed = inputMap.takeTyped();
    const std::vector<Key> keys = inputMap.takeEditKeys();
    if (const float wheel = inputMap.takeWheel(); wheel != 0.0f) {
        scrollBy(world, layout, mouse, wheel, scale);
    }
    auto buttons = world.view<UiNode, UiButton>();
    for (kw::Entity entity : buttons) {
        all.push_back(entity);
    }
    auto fields = world.view<UiNode, UiTextField>();
    for (kw::Entity entity : fields) {
        all.push_back(entity);
    }
    auto boxes = world.view<UiNode, UiCheckbox>();
    for (kw::Entity entity : boxes) {
        all.push_back(entity);
    }
    auto sliders = world.view<UiNode, UiSlider>();
    for (kw::Entity entity : sliders) {
        all.push_back(entity);
    }
    std::sort(all.begin(), all.end());   // (the order of the entities: the order of Tab)
    for (kw::Entity entity : all) {
        bool enabled = false;

        if (world.has<UiButton>(entity)) {
            UiButton& button = world.get<UiButton>(entity);

            button.hovered = button.focused = button.pressed = false;
            enabled = button.enabled;
        } else if (world.has<UiCheckbox>(entity)) {
            UiCheckbox& box = world.get<UiCheckbox>(entity);

            box.hovered = box.focused = false;
            enabled = box.enabled;
        } else if (world.has<UiSlider>(entity)) {
            UiSlider& slider = world.get<UiSlider>(entity);

            slider.hovered = slider.focused = false;
            slider.dragging = slider.dragging && slider.enabled && held(input, actions.click);   // (let go, or no longer usable)
            enabled = slider.enabled;
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
    // The slider that left / right move: they step its value instead of moving the focus
    const bool sliding = state.hasFocus && world.has<UiSlider>(state.focus);
    const kw::Entity slid = state.focus;
    const bool dragging = std::any_of(all.begin(), all.end(), [&](kw::Entity e) { return world.has<UiSlider>(e) && world.get<UiSlider>(e).dragging; });
    // The button under the mouse: the one drawn last (on top) among those under it
    std::optional<kw::Entity> hovered;
    for (auto it = layout.drawOrder.rbegin(); it != layout.drawOrder.rend() && !hovered; ++it) {
        const auto clip = layout.clips.find(*it);   // (what a scroll cuts cannot be pointed at)

        if (std::find(usable.begin(), usable.end(), *it) != usable.end() && layout.rects.at(*it).contains(mouse)
            && (clip == layout.clips.end() || clip->second.contains(mouse))) {
            hovered = *it;
        }
    }
    const bool moved = mouse.x != state.lastMouse.x || mouse.y != state.lastMouse.y;

    state.lastMouse = mouse;
    if (moved && hovered && !typing && !dragging) {
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
        if (!onField() && !(sliding && move.direction.x != 0.0f) && pressed(input, move.action)) {
            if (const auto next = neighbour(usable, layout, state.focus, move.direction)) {
                focusOn(*next);
            }
        }
    }
    // A slider takes a new value: one Changed per tick at most, and none if it did not move
    const auto slideTo = [&](kw::Entity entity, float value) {
        UiSlider& slider = world.get<UiSlider>(entity);

        value = std::clamp(value, slider.min, std::max(slider.min, slider.max));
        if (value == slider.value) {
            return;
        }
        slider.value = value;
        const auto same = [&](const UiEvent& e) { return e.kind == UiEvent::Kind::Changed && e.entity == entity; };

        if (std::none_of(events.begin(), events.end(), same)) {
            events.push_back({UiEvent::Kind::Changed, entity, true});
        }
    };
    const auto slideToMouse = [&](kw::Entity entity) {
        const UiSlider& slider = world.get<UiSlider>(entity);
        const Rect& rect = layout.rects.at(entity);
        const float ratio = rect.w > 0.0f ? std::clamp((mouse.x - rect.x) / rect.w, 0.0f, 1.0f) : 0.0f;

        slideTo(entity, slider.min + ratio * (slider.max - slider.min));
    };

    if (sliding && slid == state.focus) {
        const UiSlider& slider = world.get<UiSlider>(slid);

        if (pressed(input, actions.left)) {
            slideTo(slid, slider.value - slider.step);
        }
        if (pressed(input, actions.right)) {
            slideTo(slid, slider.value + slider.step);
        }
    }
    // A button was pressed; a check box flips instead, and a slider has nothing to press
    const auto activate = [&](kw::Entity entity) {
        if (world.has<UiSlider>(entity)) {
            return;
        }
        if (world.has<UiCheckbox>(entity)) {
            UiCheckbox& box = world.get<UiCheckbox>(entity);

            box.checked = !box.checked;
            events.push_back({UiEvent::Kind::Changed, entity, true});
        } else {
            events.push_back({UiEvent::Kind::Activated, entity, true});
        }
    };

    if (pressed(input, actions.click) && hovered) {
        focusOn(*hovered);
        if (world.has<UiTextField>(*hovered)) {
            // Only focused, and the caret goes where the click is
            UiTextField& field = world.get<UiTextField>(*hovered);
            const Rect& rect = layout.rects.at(*hovered);
            const float room = rect.w - 2.0f * theme.padding - CARET_WIDTH * scale;

            settleCaret(field);
            field.caret = caretAt(fieldFont(field, theme, scale), field, room, mouse.x - (rect.x + theme.padding));
        } else if (world.has<UiSlider>(*hovered)) {
            world.get<UiSlider>(*hovered).dragging = true;   // the handle goes to the mouse, and follows it while it is held
        } else {
            activate(*hovered);
        }
    }
    for (kw::Entity entity : usable) {
        if (world.has<UiSlider>(entity) && world.get<UiSlider>(entity).dragging) {
            slideToMouse(entity);
        }
    }
    if (pressed(input, actions.accept) && !onField()) {
        activate(state.focus);
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
            } else if (key == Key::V || key == Key::C || key == Key::X) {
                changed = clipboardWith(field, key, *world.getResource<Ref<IWindow>>()) || changed;
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
    // The focus came to a child of a scroll: bring it into view (not when the mouse did it: it is under the mouse)
    if (state.hasFocus && !(moved && hovered && *hovered == state.focus)
        && std::any_of(events.begin(), events.end(), [](const UiEvent& e) { return e.kind == UiEvent::Kind::Focused; })) {
        reveal(world, layout, state.focus, scale);
    }
    for (kw::Entity entity : usable) {
        const bool focused = state.hasFocus && entity == state.focus;
        const bool over = hovered && *hovered == entity;

        if (world.has<UiButton>(entity)) {
            UiButton& button = world.get<UiButton>(entity);

            button.focused = focused;
            button.hovered = over;
            button.pressed = (button.focused && held(input, actions.accept)) || (button.hovered && held(input, actions.click));
        } else if (world.has<UiCheckbox>(entity)) {
            UiCheckbox& box = world.get<UiCheckbox>(entity);

            box.focused = focused;
            box.hovered = over;
        } else if (world.has<UiSlider>(entity)) {
            UiSlider& slider = world.get<UiSlider>(entity);

            slider.focused = focused;
            slider.hovered = over;
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
    const float scale = scaleOf(world);
    const UiTheme theme = scaledTheme(world.getResource<UiTheme>(), scale);
    const auto& layout = world.getResource<UiLayoutResult>();

    std::optional<Rect> clip;   // what the renderer is cutting to now

    for (kw::Entity entity : layout.drawOrder) {
        const Rect rect = layout.rects.at(entity);
        const auto cut = layout.clips.find(entity);
        const std::optional<Rect> wanted = cut == layout.clips.end() ? std::nullopt : std::optional<Rect>(cut->second);

        if (wanted != clip) {
            renderer.setClip(wanted);
            clip = wanted;
        }

        if (world.has<UiPanel>(entity)) {
            const UiPanel& panel = world.get<UiPanel>(entity);

            renderer.fillRect(rect, panel.fill);
            border(renderer, rect, panel.border, panel.borderWidth * scale);
        }
        if (world.has<UiProgress>(entity)) {
            const UiProgress& bar = world.get<UiProgress>(entity);
            float ratio = bar.max > 0.0f ? bar.value / bar.max : 0.0f;

            ratio = ratio > 0.0f ? std::min(ratio, 1.0f) : 0.0f;   // (a NaN is not above 0: empty)
            renderer.fillRect(rect, bar.back);
            if (ratio > 0.0f) {
                renderer.fillRect(bar.vertical ? Rect{rect.x, rect.bottom() - rect.h * ratio, rect.w, rect.h * ratio}
                                               : Rect{rect.x, rect.y, rect.w * ratio, rect.h}, bar.fill);
            }
            border(renderer, rect, bar.border, bar.borderWidth * scale);
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
        if (world.has<UiImage>(entity)) {
            const UiImage& image = world.get<UiImage>(entity);

            if (image.texture) {
                renderer.drawTexture({.texture = image.texture->id(), .source = image.source, .destination = rect, .tint = image.tint});
            }
        }
        if (world.has<UiSlider>(entity)) {
            const UiSlider& slider = world.get<UiSlider>(entity);
            const float centre = rect.x + slidRatio(slider) * rect.w;
            const float grabWidth = HANDLE_WIDTH * scale;
            const Rect handle{std::clamp(centre - grabWidth / 2.0f, rect.x, std::max(rect.x, rect.right() - grabWidth)), rect.y, grabWidth, rect.h};
            const Rect track{rect.x, std::round(rect.y + rect.h / 4.0f), rect.w, std::round(rect.h / 2.0f)};
            Color grip = theme.buttonFill;

            if (!slider.enabled) {
                grip = theme.buttonDisabled;
            } else if (slider.dragging) {
                grip = theme.buttonPressed;
            } else if (slider.hovered || slider.focused) {
                grip = theme.buttonHover;
            }
            renderer.fillRect(track, slider.enabled ? theme.fieldFill : theme.buttonDisabled);
            if (centre > track.x) {
                renderer.fillRect({track.x, track.y, std::min(centre - track.x, track.w), track.h}, slider.enabled ? theme.sliderFill : theme.disabledText);
            }
            border(renderer, track, theme.fieldBorder, scale);
            renderer.fillRect(handle, grip);
            border(renderer, handle, slider.enabled ? theme.fieldBorder : theme.disabledText, scale);
            if (slider.focused) {
                border(renderer, rect, theme.focusRing, theme.focusWidth);
            }
        }
        if (world.has<UiCheckbox>(entity)) {
            const UiCheckbox& box = world.get<UiCheckbox>(entity);
            const Rect square{rect.x, rect.y, rect.h, rect.h};
            const float inset = std::round(rect.h / 4.0f);

            if (box.enabled && (box.hovered || box.focused)) {
                renderer.fillRect(rect, theme.buttonHover);
            }
            renderer.fillRect(square, box.enabled ? theme.fieldFill : theme.buttonDisabled);
            border(renderer, square, theme.fieldBorder, 2.0f * scale);
            if (box.checked) {
                renderer.fillRect({square.x + inset, square.y + inset, square.w - 2.0f * inset, square.h - 2.0f * inset},
                    box.enabled ? theme.checkMark : theme.disabledText);
            }
            if (box.focused) {
                border(renderer, rect, theme.focusRing, theme.focusWidth);
            }
            if (theme.font) {
                const Vec2 size = theme.font->measure(box.text, 0);

                text.draw(*theme.font, box.text, {std::round(rect.x + rect.h + CHECK_GAP * scale), std::round(rect.y + (rect.h - size.y) / 2.0f)},
                    box.enabled ? theme.buttonText : theme.disabledText);
            }
        }
        if (world.has<UiTextField>(entity)) {
            const UiTextField& field = world.get<UiTextField>(entity);
            const IFont* font = fieldFont(field, theme, scale);

            renderer.fillRect(rect, field.enabled ? theme.fieldFill : theme.buttonDisabled);
            border(renderer, rect, field.focused ? theme.focusRing : theme.fieldBorder, field.focused ? theme.focusWidth : 2.0f * scale);
            if (font) {
                const float room = rect.w - 2.0f * theme.padding - CARET_WIDTH * scale;
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

                    renderer.fillRect({std::round(left + before), top, CARET_WIDTH * scale, lineHeight}, theme.fieldCaret);
                }
            }
        }
        if (world.has<UiLabel>(entity)) {
            const UiLabel& label = world.get<UiLabel>(entity);
            const IFont* font = fontOf(label, theme, scale);
            const int wrap = static_cast<int>(static_cast<float>(label.wrapWidth) * scale);

            if (font && !label.text.empty()) {
                const Vec2 size = font->measure(label.text, wrap);
                float x = rect.x;

                if (label.align == TextAlign::Center) {
                    x += (rect.w - size.x) / 2.0f;
                } else if (label.align == TextAlign::Right) {
                    x += rect.w - size.x;
                }
                text.draw(*font, label.text, {std::round(x), std::round(rect.y)}, label.color, wrap);
            }
        }
    }
    if (clip) {
        renderer.setClip(std::nullopt);   // (the games that draw after the UI are not cut)
    }
    return true;
}

float kuge::uiScaleFor(Vec2 window, Vec2 base)
{
    if (base.x <= 0.0f || base.y <= 0.0f || window.x <= 0.0f || window.y <= 0.0f) {
        return 1.0f;
    }
    return std::min(window.x / base.x, window.y / base.y);
}

void kuge::installUi(SceneSetup scene, UiActions actions)
{
    auto& world = scene.world();

    world.addResource<UiTheme>();
    world.addResource<UiScale>();
    world.addResource<UiState>();
    world.addResource<UiLayoutResult>();
    world.addResource<UiEvents>();
    world.addResource<UiActions>(actions);
    scene.addSystem(kw::Schedule::Fixed, stage::Input, std::make_unique<UiInteract>());
    scene.addSystem(kw::Schedule::Frame, stage::Late, std::make_unique<UiLayout>());
    scene.addSystem(kw::Schedule::Frame, stage::Render, std::make_unique<UiRender>());
}
