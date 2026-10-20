#pragma once

#include "PoseAnimator.hpp"
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace kuge
{

    //! A state machine file that cannot be read; the message names the line
    class AnimationGraphError : public std::runtime_error
    {
        public:
            using std::runtime_error::runtime_error;
    };

    struct StateDef
    {
        std::string name;
        std::string clip;
        bool        loop  = true;
        float       speed = 1.0f;
    };

    struct TransitionDef
    {
        enum class When : std::uint8_t { Always, End, Compare };
        enum class Op : std::uint8_t { Greater, GreaterEqual, Less, LessEqual, Equal, NotEqual };

        std::string from;           //!< A state, or "*" for any state
        std::string to;
        float       fade  = 0.2f;   //!< Seconds of crossfade
        When        when  = When::Always;
        std::string parameter;      //!< Compare only
        Op          op    = Op::Greater;
        float       value = 0.0f;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The states of a character and the ways between them, read from a text file
     *
     *     # state <name> <clip> [loop|once] [speed <x>]
     *     state idle  Idle  loop
     *     state run   Run   loop
     *     state fire  Fire  once speed 1.5
     *
     *     # transition <from|*> <to> <fade in seconds> [when <parameter> <op> <number> | when end]
     *     # (the first one that holds, in the order of the file, is taken)
     *     transition idle run  0.15 when speed > 0.5
     *     transition run  idle 0.20 when speed <= 0.5
     *     transition fire idle 0.10 when end
     *     transition *    fire 0.05 when shoot == 1
     *
     *     start idle
     *
     * A line starting with # is a comment. `end` holds when the clip of a state has played once to its end.
     */
    ////////////////////////////////////////////////////////////////////////////
    struct StateMachineDef
    {
        std::vector<StateDef>      states;
        std::vector<TransitionDef> transitions;
        std::string                start;

        const StateDef* find(std::string_view name) const noexcept;
    };

    //! @throw AnimationGraphError, with the number of the line (also when a transition names a state that is not there)
    StateMachineDef parseStateMachine(std::string_view text);

    //! @throw AnimationGraphError (also when the file cannot be read)
    StateMachineDef loadStateMachine(const std::filesystem::path& file);

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Runs a StateMachineDef: parameters in (speed, crouching, pitch...), the clip to play out
     *
     * It chooses; it does not play: give the clip to a k3::Animator (Animator3D) on the client, or to a PoseAnimator
     * (AnimationGraph) in the room. Both agree because both read the same parameters.
     */
    ////////////////////////////////////////////////////////////////////////////
    class AnimationStateMachine
    {
        public:
            //! What to play now
            struct Change
            {
                const StateDef* state;
                float           fade;   //!< Seconds of crossfade from what played
            };

            AnimationStateMachine(std::shared_ptr<const StateMachineDef> def, std::shared_ptr<const k3::AnimatedModel> model);

            //! A parameter (0 until it is set)
            void  set(std::string_view name, float value);
            float get(std::string_view name) const;

            //! Moves the time of the state on, and takes a transition if one holds. The first call gives the start state.
            //! @return  the state to play, when it changed
            std::optional<Change> update(float seconds);

            const std::string& state(void) const noexcept { return m_state; }
            float              stateTime(void) const noexcept { return m_time; }

        private:
            bool holds(const TransitionDef& transition) const;
            float duration(const StateDef& state) const;

            std::shared_ptr<const StateMachineDef>   m_def;
            std::shared_ptr<const k3::AnimatedModel> m_model;
            std::map<std::string, float, std::less<>> m_parameters;
            std::string m_state;
            float       m_time = 0.0f;
            bool        m_started = false;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A state machine and the PoseAnimator it drives: the room's animation of a character
     *
     *     AnimationGraph graph(def, model);
     *     graph.set("speed", 3.0f);
     *     graph.update(dt);                       // each tick
     *     graph.pose();                           // for bonePlaces()
     */
    ////////////////////////////////////////////////////////////////////////////
    class AnimationGraph
    {
        public:
            AnimationGraph(std::shared_ptr<const StateMachineDef> def, std::shared_ptr<const k3::AnimatedModel> model);

            void set(std::string_view name, float value) { m_machine.set(name, value); }
            void update(float seconds);
            const Pose& pose(void) { return m_animator.pose(); }
            const std::string& state(void) const noexcept { return m_machine.state(); }

        private:
            AnimationStateMachine m_machine;
            PoseAnimator          m_animator;
    };

}
