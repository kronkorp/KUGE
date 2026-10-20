#include "StateMachine.hpp"
#include <charconv>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

namespace
{
    using namespace kuge;

    std::vector<std::string> words(const std::string& line)
    {
        std::istringstream in(line);
        std::vector<std::string> result;
        std::string word;

        while (in >> word) {
            result.push_back(word);
        }
        return result;
    }

    float number(const std::string& text, int line)
    {
        float value = 0.0f;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);

        if (error != std::errc() || end != text.data() + text.size() || !std::isfinite(value)) {
            throw AnimationGraphError(std::format("line {}: '{}' is not a number", line, text));
        }
        return value;
    }
}

const kuge::StateDef* kuge::StateMachineDef::find(std::string_view name) const noexcept
{
    for (const StateDef& state : states) {
        if (state.name == name) {
            return &state;
        }
    }
    return nullptr;
}

kuge::StateMachineDef kuge::parseStateMachine(std::string_view text)
{
    StateMachineDef def;
    std::istringstream in{std::string(text)};
    std::string line;
    int number_ = 0;

    while (std::getline(in, line)) {
        ++number_;
        const auto hash = line.find('#');

        if (hash != std::string::npos) {
            line.erase(hash);
        }
        const std::vector<std::string> w = words(line);

        if (w.empty()) {
            continue;
        }
        const auto fail = [&](const std::string& why) { throw AnimationGraphError(std::format("line {}: {}", number_, why)); };

        if (w[0] == "state") {
            if (w.size() < 3) {
                fail("a state is 'state <name> <clip> [loop|once] [speed <x>]'");
            }
            if (def.find(w[1])) {
                fail(std::format("the state '{}' is already there", w[1]));
            }
            StateDef state{w[1], w[2]};

            for (std::size_t i = 3; i < w.size(); ++i) {
                if (w[i] == "loop" || w[i] == "once") {
                    state.loop = w[i] == "loop";
                } else if (w[i] == "speed" && i + 1 < w.size()) {
                    state.speed = number(w[++i], number_);
                } else {
                    fail(std::format("'{}' is not loop, once or speed <x>", w[i]));
                }
            }
            def.states.push_back(state);
        } else if (w[0] == "transition") {
            if (w.size() < 4) {
                fail("a transition is 'transition <from|*> <to> <fade> [when ...]'");
            }
            TransitionDef transition;

            transition.from = w[1];
            transition.to = w[2];
            transition.fade = number(w[3], number_);
            if (w.size() > 4) {
                if (w[4] != "when") {
                    fail(std::format("'{}': expected 'when'", w[4]));
                }
                if (w.size() == 6 && w[5] == "end") {
                    transition.when = TransitionDef::When::End;
                } else if (w.size() == 8) {
                    const std::string& op = w[6];

                    transition.when = TransitionDef::When::Compare;
                    transition.parameter = w[5];
                    if (op == ">") transition.op = TransitionDef::Op::Greater;
                    else if (op == ">=") transition.op = TransitionDef::Op::GreaterEqual;
                    else if (op == "<") transition.op = TransitionDef::Op::Less;
                    else if (op == "<=") transition.op = TransitionDef::Op::LessEqual;
                    else if (op == "==") transition.op = TransitionDef::Op::Equal;
                    else if (op == "!=") transition.op = TransitionDef::Op::NotEqual;
                    else fail(std::format("'{}' is not one of > >= < <= == !=", op));
                    transition.value = number(w[7], number_);
                } else {
                    fail("a condition is 'when end' or 'when <parameter> <op> <number>'");
                }
            }
            def.transitions.push_back(transition);
        } else if (w[0] == "start") {
            if (w.size() != 2) {
                fail("'start <state>'");
            }
            def.start = w[1];
        } else {
            fail(std::format("'{}' is not state, transition or start", w[0]));
        }
    }
    if (def.states.empty()) {
        throw AnimationGraphError("no state");
    }
    if (def.start.empty()) {
        def.start = def.states.front().name;
    }
    if (!def.find(def.start)) {
        throw AnimationGraphError(std::format("the start state '{}' is not there", def.start));
    }
    for (const TransitionDef& transition : def.transitions) {
        if ((transition.from != "*" && !def.find(transition.from)) || !def.find(transition.to)) {
            throw AnimationGraphError(std::format("a transition from '{}' to '{}' names a state that is not there", transition.from, transition.to));
        }
    }
    return def;
}

kuge::StateMachineDef kuge::loadStateMachine(const std::filesystem::path& file)
{
    std::ifstream in(file);

    if (!in) {
        throw AnimationGraphError(std::format("cannot read '{}'", file.string()));
    }
    std::ostringstream text;

    text << in.rdbuf();
    try {
        return parseStateMachine(text.str());
    } catch (const AnimationGraphError& error) {
        throw AnimationGraphError(std::format("{}: {}", file.string(), error.what()));
    }
}

kuge::AnimationStateMachine::AnimationStateMachine(std::shared_ptr<const StateMachineDef> def, std::shared_ptr<const k3::AnimatedModel> model)
    : m_def(std::move(def)), m_model(std::move(model)), m_state(m_def->start)
{
}

void kuge::AnimationStateMachine::set(std::string_view name, float value)
{
    const auto found = m_parameters.find(name);

    if (found == m_parameters.end()) {
        m_parameters.emplace(std::string(name), value);
    } else {
        found->second = value;
    }
}

float kuge::AnimationStateMachine::get(std::string_view name) const
{
    const auto found = m_parameters.find(name);

    return found == m_parameters.end() ? 0.0f : found->second;
}

float kuge::AnimationStateMachine::duration(const StateDef& state) const
{
    const int clip = m_model->findAnimation(state.clip);

    return clip < 0 ? 0.0f : m_model->animations[static_cast<std::size_t>(clip)].duration / std::max(std::fabs(state.speed), 0.001f);
}

bool kuge::AnimationStateMachine::holds(const TransitionDef& transition) const
{
    switch (transition.when) {
        case TransitionDef::When::Always:
            return true;
        case TransitionDef::When::End:
            return m_time >= duration(*m_def->find(m_state));
        case TransitionDef::When::Compare: {
            const float value = get(transition.parameter);

            switch (transition.op) {
                case TransitionDef::Op::Greater:      return value > transition.value;
                case TransitionDef::Op::GreaterEqual: return value >= transition.value;
                case TransitionDef::Op::Less:         return value < transition.value;
                case TransitionDef::Op::LessEqual:    return value <= transition.value;
                case TransitionDef::Op::Equal:        return value == transition.value;
                case TransitionDef::Op::NotEqual:     return value != transition.value;
            }
        }
    }
    return false;
}

std::optional<kuge::AnimationStateMachine::Change> kuge::AnimationStateMachine::update(float seconds)
{
    if (!m_started) {
        m_started = true;
        m_time = 0.0f;
        return Change{m_def->find(m_state), 0.0f};
    }
    m_time += seconds;
    for (const TransitionDef& transition : m_def->transitions) {
        if ((transition.from == "*" || transition.from == m_state) && transition.to != m_state && holds(transition)) {
            m_state = transition.to;
            m_time = 0.0f;
            return Change{m_def->find(m_state), transition.fade};
        }
    }
    return std::nullopt;
}

kuge::AnimationGraph::AnimationGraph(std::shared_ptr<const StateMachineDef> def, std::shared_ptr<const k3::AnimatedModel> model)
    : m_machine(std::move(def), model), m_animator(std::move(model))
{
}

void kuge::AnimationGraph::update(float seconds)
{
    if (const auto change = m_machine.update(seconds)) {
        m_animator.play(change->state->clip, {change->state->loop, change->fade, change->state->speed});
    }
    m_animator.update(seconds);
}
