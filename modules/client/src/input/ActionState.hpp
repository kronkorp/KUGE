#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace kuge
{

    //! Number of the action in the InputMap, from 0 to 63 (an enum of the game,
    //! most of the time)
    using ActionId = std::uint8_t;

    constexpr std::size_t MAX_ACTIONS = 64;

    //! An enum (or a number) as an ActionId
    //! @throw std::out_of_range if it is not below MAX_ACTIONS
    template<typename A>
    constexpr ActionId actionId(A action)
    {
        static_assert(std::is_enum_v<A> || std::is_integral_v<A>, "an action is an enum or a number");
        const auto value = static_cast<long long>(action);

        if (value < 0 || value >= static_cast<long long>(MAX_ACTIONS)) {
            throw std::out_of_range("an action must be between 0 and 63");
        }
        return static_cast<ActionId>(value);
    }

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  What the player asked for during one fixed tick, as actions
     *
     * Three sets of actions, one bit each: down (held), pressed (went down
     * since the last tick) and released (went up since the last tick). A key
     * pressed and released between two ticks is still seen as pressed. It is
     * what a client sends to the server, and what the simulation reads to move
     * things: the game never sees the keys.
     */
    ////////////////////////////////////////////////////////////////////////////
    struct ActionState
    {
        std::uint64_t tick     = 0;
        std::uint64_t down     = 0;
        std::uint64_t pressed  = 0;
        std::uint64_t released = 0;

        template<typename A> constexpr bool isDown(A action) const { return has(down, action); }
        template<typename A> constexpr bool wasPressed(A action) const { return has(pressed, action); }
        template<typename A> constexpr bool wasReleased(A action) const { return has(released, action); }

        constexpr bool operator==(const ActionState&) const = default;

        private:
            template<typename A>
            static constexpr bool has(std::uint64_t mask, A action)
            {
                return (mask >> actionId(action)) & 1u;
            }
    };

}
