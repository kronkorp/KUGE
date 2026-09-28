#pragma once

#include "Math2D.hpp"
#include "Scene.hpp"
#include "input/ActionState.hpp"
#include "render/Color.hpp"
#include "ui/Font.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  User interface: menus, HUD, options. Made of entities in screen
     *         pixels (not the world: the camera does not move it).
     *
     * A **node** (UiNode) is a rectangle. It is placed by an **anchor** in its
     * parent (or in the screen, if it has none), or by the **stack** of its
     * parent (UiStack: a column or a row). What it shows is another component of
     * the same entity: UiPanel (a background), UiLabel (text), UiButton (a
     * button with its text).
     *
     *     kuge::installUi(setup(), {.up = kuge::actionId(Action::Up), .down = kuge::actionId(Action::Down),
     *                               .accept = kuge::actionId(Action::Confirm), .click = kuge::actionId(Action::Click)});
     *
     *     const auto menu = world.create();
     *     world.add<kuge::UiNode>(menu, kuge::UiNode{.anchor = kuge::Anchor::Center});
     *     world.add<kuge::UiPanel>(menu, kuge::UiPanel{});
     *     world.add<kuge::UiStack>(menu, kuge::UiStack{});               // a column
     *
     *     const auto play = world.create();
     *     world.add<kuge::UiNode>(play, kuge::UiNode{.parent = menu, .hasParent = true});
     *     world.add<kuge::UiButton>(play, kuge::UiButton{.text = "Play"});
     *
     * Each fixed tick the buttons react to the actions (moving the focus, pressing
     * the focused one) and to the mouse, and say what happened in UiEvents.
     */
    ////////////////////////////////////////////////////////////////////////////

    //! Which point of the parent (and of the node) they are joined by
    enum class Anchor : std::uint8_t {
        TopLeft, Top, TopRight,
        Left, Center, Right,
        BottomLeft, Bottom, BottomRight,
    };

    enum class Direction : std::uint8_t { Vertical, Horizontal };

    //! Across a stack: where the children sit (Stretch: they fill it)
    enum class Align : std::uint8_t { Start, Center, End, Stretch };

    enum class TextAlign : std::uint8_t { Left, Center, Right };

    struct UiNode
    {
        kw::Entity parent    = 0;
        bool       hasParent = false;     //!< (0 is an entity like another)
        Anchor     anchor    = Anchor::TopLeft;
        Vec2       offset{};              //!< From where the anchor puts it, in pixels
        Vec2       size{};                //!< 0: as big as what it holds (text, children)
        bool       visible   = true;      //!< With everything inside it
        int        layer     = 0;         //!< Higher is drawn over
    };

    //! A column or a row of the children of this node, in the order of their entities
    struct UiStack
    {
        Direction direction = Direction::Vertical;
        float     spacing   = 8.0f;   //!< Between two children
        float     padding   = 8.0f;   //!< Between the children and the border
        Align     align     = Align::Stretch;
    };

    struct UiPanel
    {
        Color fill{30, 32, 44, 235};
        Color border{170, 175, 200, 255};
        float borderWidth = 2.0f;
    };

    struct UiLabel
    {
        std::string                text;
        std::shared_ptr<const IFont> font;    //!< The theme's if not given
        Color                      color{240, 240, 245, 255};
        int                        wrapWidth = 0;    //!< 0: no limit
        TextAlign                  align = TextAlign::Left;
    };

    struct UiButton
    {
        std::string text;
        bool        enabled = true;

        // What the interaction found, at the last tick
        bool        hovered = false;    //!< The mouse is over it
        bool        focused = false;    //!< The keyboard or the gamepad is on it
        bool        pressed = false;    //!< An action or a click holds it down
    };

    //! What the UI looks like (a resource: change it for your game)
    struct UiTheme
    {
        std::shared_ptr<const IFont> font;    //!< Without one, text is not drawn
        Color buttonFill{58, 62, 92, 255};
        Color buttonHover{82, 88, 130, 255};
        Color buttonPressed{40, 43, 68, 255};
        Color buttonDisabled{45, 46, 58, 255};
        Color buttonText{240, 240, 245, 255};
        Color disabledText{120, 122, 135, 255};
        Color focusRing{255, 214, 90, 255};
        float focusWidth = 3.0f;
        float padding = 10.0f;     //!< Between the text of a button and its border
    };

    //! Which actions of the game the interface uses. -1: none. They are the
    //! numbers of the game's own actions (its enum), see InputMap.
    struct UiActions
    {
        int up     = -1;
        int down   = -1;
        int left   = -1;
        int right  = -1;
        int accept = -1;   //!< Presses the focused button
        int cancel = -1;   //!< Says "back" (see UiEvent)
        int click  = -1;   //!< Presses the button under the mouse
    };

    struct UiEvent
    {
        enum class Kind : std::uint8_t {
            Activated,   //!< A button was pressed
            Focused,     //!< The focus moved to a button
            Cancelled,   //!< "back" was asked (entity: what had the focus, if anything)
        };

        Kind       kind;
        kw::Entity entity;
        bool       hasEntity;
    };

    //! What happened during the last tick (a resource)
    struct UiEvents
    {
        std::vector<UiEvent> list;
    };

    //! What the keyboard or gamepad is on (a resource)
    struct UiState
    {
        kw::Entity focus    = 0;
        bool       hasFocus = false;
        Vec2       lastMouse{-1.0f, -1.0f};
    };

    //! Where each node is, as of the last frame (a resource)
    struct UiLayoutResult
    {
        std::map<kw::Entity, Rect> rects;     //!< Only the nodes that show
        std::vector<kw::Entity>    drawOrder; //!< Back to front
    };

    //! Adds the interface to a scene: its resources, and the systems that lay it
    //! out (Frame, Late), make it react (Fixed, Input) and draw it (Frame,
    //! Render, after the sprites). Call it after installClientSystems().
    void installUi(SceneSetup scene, UiActions actions = {});

    //! Where a node of a size is put in a rectangle by an anchor
    Vec2 anchoredPosition(Anchor anchor, const Rect& area, Vec2 size);

}
