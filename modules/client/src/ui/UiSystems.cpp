#include "ui/UiSystems.hpp"
#include "Ref.hpp"
#include "Stage.hpp"
#include "backend/IRenderer2D.hpp"
#include "input/ActionState.hpp"
#include "input/InputMap.hpp"
#include "ui/TextRenderer.hpp"
#include "ui/Ui.hpp"
#include <algorithm>
#include <cmath>
#include <functional>

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
    const Vec2 mouse = world.getResource<Ref<InputMap>>()->mousePosition();
    std::vector<kw::Entity> all;
    std::vector<kw::Entity> usable;   // buttons that can be pressed: shown and enabled

    events.clear();
    auto view = world.view<UiNode, UiButton>();
    for (kw::Entity entity : view) {
        all.push_back(entity);
    }
    std::sort(all.begin(), all.end());
    for (kw::Entity entity : all) {
        UiButton& button = world.get<UiButton>(entity);

        button.hovered = button.focused = button.pressed = false;
        if (button.enabled && layout.rects.count(entity)) {
            usable.push_back(entity);
        }
    }
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
    // The button under the mouse: the one drawn last (on top) among those under it
    std::optional<kw::Entity> hovered;
    for (auto it = layout.drawOrder.rbegin(); it != layout.drawOrder.rend() && !hovered; ++it) {
        if (std::find(usable.begin(), usable.end(), *it) != usable.end() && layout.rects.at(*it).contains(mouse)) {
            hovered = *it;
        }
    }
    const bool moved = mouse.x != state.lastMouse.x || mouse.y != state.lastMouse.y;

    state.lastMouse = mouse;
    if (moved && hovered) {
        focusOn(*hovered);         // the mouse takes the focus only when it moves: the keyboard keeps it otherwise
    }
    if (!state.hasFocus) {
        focusOn(usable.front());   // a menu always has something selected
    }
    const struct { int action; Vec2 direction; } moves[] = {
        {actions.up, {0.0f, -1.0f}}, {actions.down, {0.0f, 1.0f}}, {actions.left, {-1.0f, 0.0f}}, {actions.right, {1.0f, 0.0f}},
    };
    for (const auto& move : moves) {
        if (pressed(input, move.action)) {
            if (const auto next = neighbour(usable, layout, state.focus, move.direction)) {
                focusOn(*next);
            }
        }
    }
    if (pressed(input, actions.click) && hovered) {
        focusOn(*hovered);
        events.push_back({UiEvent::Kind::Activated, *hovered, true});
    }
    if (pressed(input, actions.accept)) {
        events.push_back({UiEvent::Kind::Activated, state.focus, true});
    }
    if (cancelled) {
        events.push_back({UiEvent::Kind::Cancelled, state.focus, state.hasFocus});
    }
    for (kw::Entity entity : usable) {
        UiButton& button = world.get<UiButton>(entity);

        button.focused = state.hasFocus && entity == state.focus;
        button.hovered = hovered && *hovered == entity;
        button.pressed = (button.focused && held(input, actions.accept)) || (button.hovered && held(input, actions.click));
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
