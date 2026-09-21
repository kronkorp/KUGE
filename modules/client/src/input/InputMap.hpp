#pragma once

#include "ConfigFile.hpp"
#include "Math2D.hpp"
#include "input/ActionState.hpp"
#include "input/Binding.hpp"
#include "input/Event.hpp"
#include <array>
#include <bitset>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Turns keys, buttons and sticks into the actions of a game
     *
     * The game defines its actions (an enum), binds physical inputs to them,
     * and only reads actions: the player can rebind everything without the
     * game noticing.
     *
     *     enum class Action { Up, Down, Shoot };
     *
     *     input.declare(Action::Shoot, "shoot");           // its name in the keybinds file
     *     input.bind(Action::Shoot, Key::Space);
     *     input.bind(Action::Shoot, GamepadButton::A);     // several inputs, one action
     *     input.loadBindings("keybinds.cfg");              // what the player changed wins
     *
     * Events go in (handle()), and once per fixed tick sampleTick() gives the
     * ActionState of the tick.
     */
    ////////////////////////////////////////////////////////////////////////////
    class InputMap
    {
        public:
            //! Gives an action a name, to save and load its bindings
            //! @throw std::invalid_argument if the name is empty, has a space or is taken
            template<typename A>
            void declare(A action, std::string name) { declareAction(actionId(action), std::move(name)); }

            //! Binds an input to an action (an action can have several).
            //! Binding twice the same input does nothing.
            template<typename A>
            void bind(A action, Binding binding) { bindAction(actionId(action), binding); }

            //! Removes every binding of an action
            template<typename A>
            void unbind(A action) { m_bindings[actionId(action)].clear(); refresh(false); }

            //! Removes one binding of an action
            template<typename A>
            void unbind(A action, Binding binding) { unbindAction(actionId(action), binding); }

            template<typename A>
            const std::vector<Binding>& bindings(A action) const { return m_bindings[actionId(action)]; }

            //! Is the action held right now (as opposed to at the last tick)
            template<typename A>
            bool isDown(A action) const { return (m_down >> actionId(action)) & 1u; }

            //! The stick / trigger is considered pushed beyond this (default 0.5)
            void setAxisThreshold(float threshold) noexcept { m_axisThreshold = threshold; }

            //! Takes what happened outside into account
            void handle(const Event& event);

            //! The actions for the tick that starts, and forgets what was
            //! already reported (see ActionState)
            ActionState sampleTick(std::uint64_t tick);

            //! Where the mouse is, in pixels of the window
            Vec2 mousePosition(void) const noexcept { return m_mouse; }

            // -- Rebinding: "press the key you want" ---------------------------------

            //! The next key, button or stick pushed is kept instead of acting,
            //! see takeCaptured()
            void startCapture(void) noexcept;
            void cancelCapture(void) noexcept;
            bool capturing(void) const noexcept { return m_capturing; }

            //! What was pushed since startCapture(), once
            std::optional<Binding> takeCaptured(void) noexcept;

            // -- Keybinds file ------------------------------------------------------

            //! Writes the bindings of every declared action, "input.<name> = W, Up, Pad.DPadUp"
            void save(ConfigFile& config) const;

            //! Replaces the bindings of the declared actions the file talks about
            //! (the others keep theirs, so the defaults survive a file with only
            //! a few changes). What is not a binding is ignored and logged.
            void load(const ConfigFile& config);

            //! @return  false if there is no such file, or it is not a valid one
            //!          (the bindings are left as they were; the reason is logged)
            bool loadBindings(const std::filesystem::path& path);

            //! @throw ConfigError
            void saveBindings(const std::filesystem::path& path) const;

        private:
            struct PadState {
                std::bitset<static_cast<std::size_t>(GamepadButton::Count)>  buttons;
                std::array<float, static_cast<std::size_t>(GamepadAxis::Count)> axes{};
            };

            void declareAction(ActionId action, std::string name);
            void bindAction(ActionId action, Binding binding);
            void unbindAction(ActionId action, Binding binding);
            bool isActive(const Binding& binding) const;
            std::uint64_t computeDown(void) const;
            void refresh(bool latchEdges);
            std::optional<Binding> capturedBy(const Event& event) const;
            void apply(const Event& event);

            std::array<std::vector<Binding>, MAX_ACTIONS> m_bindings;
            std::array<std::string, MAX_ACTIONS>          m_names;

            // What is held, by device
            std::bitset<static_cast<std::size_t>(Key::Count)>         m_keys;
            std::bitset<static_cast<std::size_t>(MouseButton::Count)> m_mouseButtons;
            std::map<int, PadState>                                   m_pads;
            Vec2                                                      m_mouse;
            float                                                     m_axisThreshold = 0.5f;

            // The actions: held now, and what happened since the last sampleTick()
            std::uint64_t m_down     = 0;
            std::uint64_t m_pressed  = 0;
            std::uint64_t m_released = 0;

            bool                   m_capturing = false;
            std::optional<Binding> m_captured;
    };

}
