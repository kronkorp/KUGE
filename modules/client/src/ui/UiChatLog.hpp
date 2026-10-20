#pragma once

#include "Math2D.hpp"
#include "kronkworld/Kronkworld.hpp"
#include "render/Color.hpp"
#include "ui/Ui.hpp"
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The last lines of a text chat in a corner of the window, each one fading after a while, and a field to say one
     *
     * It builds its nodes in the World of a scene (after installUi()); the game keeps it, calls update() each tick and reads
     * the line that was said. The colour of a line (who wrote it) and who may say what are the game's.
     *
     *     UiChatLog chat(world(), {.anchor = Anchor::BottomLeft});
     *     ...
     *     if (actions.wasPressed(Action::Chat) && !chat.isOpen()) chat.open(world());          // the opening key's character is dropped
     *     if (const auto line = chat.submitted(world())) endpoint.send(room, ChatSend{*line}); // Enter: the field closes
     *     if (chat.isOpen() && cancelled) chat.close(world());
     *     chat.push("bob: hello", {255, 255, 255, 255});                                       // a ChatLine arrived
     *     chat.update(world(), dt);                                                            // the lines fade
     *
     * While it is open every line stays shown and the game should not move (the game asks isOpen(), as the console is an overlay
     * scene that stops the game: do the same with a scene if the game's input must be frozen).
     */
    ////////////////////////////////////////////////////////////////////////////
    class UiChatLog
    {
        public:
            struct Config
            {
                Anchor      anchor = Anchor::BottomLeft;
                Vec2        offset{16.0f, -16.0f};
                Vec2        size{480.0f, 0.0f};
                std::size_t lines = 6;               //!< Shown at once
                double      showSeconds = 8.0;       //!< A line stays this long...
                double      fadeSeconds = 2.0;       //!< ...then fades out over this
                std::size_t maxLength = 200;         //!< Of the field, in characters
            };

            explicit UiChatLog(kw::World& world) : UiChatLog(world, Config()) {}
            UiChatLog(kw::World& world, Config config);

            //! Adds a line at the bottom. The oldest goes when there are more than `lines`.
            void push(std::string text, Color color = {240, 240, 245, 255});

            //! Opens the field and gives it the keyboard. What was typed and waits (the key that opened it) is dropped.
            void open(kw::World& world);
            void close(kw::World& world);
            bool isOpen(void) const noexcept { return m_open; }

            //! The line that Enter sent since the last call, trimmed; the field is emptied and closed. An empty line is no line.
            std::optional<std::string> submitted(kw::World& world);

            //! Ages the lines and shows them (alpha 0 when faded, a line in a field that is open is never faded)
            void update(kw::World& world, double seconds);

            //! The lines kept, with how long they have been there (for tests)
            struct Kept
            {
                std::string text;
                Color       color;
                double      age;
            };
            const std::deque<Kept>& kept(void) const noexcept { return m_kept; }

            kw::Entity field(void) const noexcept { return m_field; }

        private:
            Config                  m_config;
            std::deque<Kept>        m_kept;
            std::vector<kw::Entity> m_labels;
            kw::Entity              m_field = 0;
            bool                    m_open = false;
    };

}
