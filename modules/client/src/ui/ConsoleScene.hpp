#pragma once

#include "ClientScene.hpp"
#include "Commands.hpp"
#include "Logger.hpp"
#include "ui/Font.hpp"
#include "ui/Ui.hpp"
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The developer console: a panel at the top of the window where commands are typed and the log is read
     *
     * It is a scene pushed over the game, Transparent (see Scene::overlay()): the game under it is still drawn and keeps its
     * network, but its Input and Simulation stages stand still, so it does not move while the console has the keyboard.
     *
     *     // in the game, when the console action is pressed:
     *     ctx().scenes().push<kuge::ConsoleScene>(commands, logRing, font, actionId(Action::Console));
     *
     * Enter runs the line (`commands->run`) and shows the answer; Up and Down walk the history; Tab completes the name of a command
     * or a variable (with several candidates it completes what they share and lists them); Escape, or the console action again,
     * closes it. The log of the process is shown too, in the colour of its level and always with the level written
     * ("WARN [net] bad packet"), from `log` (a RingLogHandler the game added to the Logger; none: no log). Two variables, added
     * to the commands' Cvars when they are not there, filter it: `console_log_level` (0 debug ... 4 error, default 1) and
     * `console_log_category` (a category, empty for all).
     *
     * Commands that change the world should be queued (CommandQueue) rather than run from here, if the game keeps its state
     * in a scene that must not be touched from the console's: the console runs them in its own tick, on the thread of the scenes.
     * The key that opened the console also typed its character: the console throws it away.
     */
    ////////////////////////////////////////////////////////////////////////////
    class ConsoleScene : public ClientScene
    {
        public:
            //! @param toggleAction  An action of the game (its number) that also closes the console; -1: only Escape does
            ConsoleScene(std::shared_ptr<Commands> commands, std::shared_ptr<RingLogHandler> log, std::shared_ptr<const IFont> font,
                int toggleAction = -1);

            void onEnter(void) override;
            void onExit(void) override;
            OverlayMode overlay(void) const override { return OverlayMode::Transparent; }

            //! What the console shows: the answers and the log, oldest first (for tests)
            struct Line
            {
                std::string text;
                Color       color;
            };
            const std::deque<Line>& lines(void) const noexcept { return m_lines; }

            //! The text in the field, as it is typed now (for tests)
            std::string typed(void);

        private:
            class Input;
            friend class Input;

            static constexpr std::size_t SHOWN = 16;      //!< Lines on screen
            static constexpr std::size_t KEPT = 300;      //!< Lines kept

            void say(std::string text, Color color);
            void pullLog(void);
            void refresh(void);

            std::shared_ptr<Commands>        m_commands;
            std::shared_ptr<RingLogHandler>  m_log;
            std::shared_ptr<const IFont>     m_font;
            int                              m_toggle;
            std::deque<Line>                 m_lines;
            std::vector<std::string>         m_history;
            std::size_t                      m_walk = 0;        //!< Where Up and Down are in the history (size: not walking)
            std::uint64_t                    m_seen = 0;        //!< Last id of the log that was read
            kw::Entity                       m_field = 0;
            std::vector<kw::Entity>          m_labels;
    };

}
