#include "Commands.hpp"
#include "Logger.hpp"
#include "Stage.hpp"
#include <algorithm>
#include <format>
#include <sstream>

kuge::Commands::Commands(Cvars& cvars) : m_cvars(cvars)
{
    add("help", [this](const std::vector<std::string>& arguments) {
        std::string text;

        if (!arguments.empty()) {
            const auto found = m_commands.find(arguments[0]);

            return found == m_commands.end() ? std::format("no command '{}' (cvars lists the variables)", arguments[0])
                : std::format("{}: {}", arguments[0], found->second.help.empty() ? "no help" : found->second.help);
        }
        for (const auto& [name, entry] : m_commands) {
            text += (text.empty() ? "" : "\n") + name + (entry.help.empty() ? std::string() : " - " + entry.help);
        }
        return text;
    }, "help [command]: the commands, or one of them");
    add("cvars", [this](const std::vector<std::string>& arguments) {
        std::string text;

        for (const std::string& line : m_cvars.list(arguments.empty() ? std::string_view() : std::string_view(arguments[0]))) {
            text += (text.empty() ? "" : "\n") + line;
        }
        return text.empty() ? std::string("no variable") : text;
    }, "cvars [filter]: the variables and their values");
    add("set", [this](const std::vector<std::string>& arguments) {
        if (arguments.size() < 2) {
            return std::string("set <name> <value>");
        }
        return m_cvars.set(arguments[0], arguments[1]).message;
    }, "set <name> <value>: changes a variable");
    add("get", [this](const std::vector<std::string>& arguments) {
        if (arguments.empty()) {
            return std::string("get <name>");
        }
        return m_cvars.has(arguments[0]) ? std::format("{} = {}", arguments[0], m_cvars.get(arguments[0])) : std::format("unknown variable '{}'", arguments[0]);
    }, "get <name>: the value of a variable");
    add("echo", [](const std::vector<std::string>& arguments) {
        std::string text;

        for (const std::string& word : arguments) {
            text += (text.empty() ? "" : " ") + word;
        }
        return text;
    }, "echo <text>");
}

void kuge::Commands::add(std::string_view name, Function function, std::string_view help)
{
    const bool valid = !name.empty() && std::all_of(name.begin(), name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '-';
    });

    if (!valid) {
        throw std::invalid_argument(std::format("'{}' is not a name for a command", name));
    }
    if (m_commands.find(name) != m_commands.end()) {
        throw std::invalid_argument(std::format("the command '{}' is already there", name));
    }
    m_commands.emplace(std::string(name), Entry{std::move(function), std::string(help)});
}

std::vector<std::string> kuge::Commands::split(std::string_view line)
{
    std::vector<std::string> words;
    std::string word;
    bool quoted = false;
    bool started = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];

        if (c == '"') {
            quoted = !quoted;
            started = true;
        } else if (c == '\\' && quoted && i + 1 < line.size() && (line[i + 1] == '"' || line[i + 1] == '\\')) {
            word += line[++i];
        } else if ((c == ' ' || c == '\t') && !quoted) {
            if (started) {
                words.push_back(word);
                word.clear();
                started = false;
            }
        } else {
            word += c;
            started = true;
        }
    }
    if (started) {
        words.push_back(word);
    }
    return words;
}

std::string kuge::Commands::run(std::string_view line)
{
    std::vector<std::string> words = split(line);

    if (words.empty()) {
        return {};
    }
    const std::string name = words.front();

    words.erase(words.begin());
    if (const auto found = m_commands.find(name); found != m_commands.end()) {
        try {
            return found->second.function(words);
        } catch (const std::exception& error) {
            return std::format("{}: {}", name, error.what());
        } catch (...) {
            return std::format("{}: failed", name);
        }
    }
    if (m_cvars.has(name)) {   // a variable on its own: read it, or set it
        if (words.empty()) {
            return std::format("{} = {}", name, m_cvars.get(name));
        }
        return m_cvars.set(name, words.front()).message;
    }
    return std::format("unknown command '{}' (help lists them)", name);
}

std::vector<std::string> kuge::Commands::complete(std::string_view prefix) const
{
    std::vector<std::string> names = m_cvars.names(prefix);

    for (const auto& [name, entry] : m_commands) {
        if (name.compare(0, prefix.size(), prefix) == 0) {
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

void kuge::CommandQueue::push(std::string line)
{
    std::lock_guard lock(m_mutex);

    m_lines.push_back(std::move(line));
}

std::vector<std::string> kuge::CommandQueue::take(void)
{
    std::lock_guard lock(m_mutex);
    std::vector<std::string> lines(m_lines.begin(), m_lines.end());

    m_lines.clear();
    return lines;
}

namespace
{
    class RunQueuedCommands final : public kw::ISystem
    {
        public:
            RunQueuedCommands(kuge::Commands& commands, std::shared_ptr<kuge::CommandQueue> queue)
                : m_commands(commands), m_queue(std::move(queue)) {}

            bool handle(kw::World&) override
            {
                for (const std::string& line : m_queue->take()) {
                    const std::string answer = m_commands.run(line);

                    if (!answer.empty()) {
                        Logger::logger().category("console").info("{}", answer);
                    }
                }
                return true;
            }

        private:
            kuge::Commands&                      m_commands;
            std::shared_ptr<kuge::CommandQueue>  m_queue;
    };
}

void kuge::installCommandQueue(SceneSetup scene, Commands& commands, std::shared_ptr<CommandQueue> queue)
{
    scene.addSystem(kw::Schedule::Fixed, stage::Input, std::make_unique<RunQueuedCommands>(commands, std::move(queue)));
}
