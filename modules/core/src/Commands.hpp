#pragma once

#include "Cvars.hpp"
#include "Scene.hpp"
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Commands typed in a console: a name, arguments, and an answer
     *
     *     kuge::Cvars cvars;
     *     kuge::Commands commands(cvars);
     *     commands.add("spawn", [&](const std::vector<std::string>& args) { return spawn(args.at(0)); }, "spawn <enemy>");
     *     commands.run("spawn boss_blob");                    // -> the answer, for the console to show
     *     commands.run("brainrot_per_kill 5");                // a variable's name alone reads it; with a value it sets it
     *
     * Built in: `help`, `cvars [filter]`, `set <name> <value>`, `get <name>`, `echo <text>`. A line is cut at spaces; a word
     * can be quoted with "..." to hold spaces (`echo "a b"`). An unknown name is an answer, not an exception, and so is a
     * command that throws. A command runs on the thread that calls run(): one that changes the world must be run in the tick
     * (see CommandQueue).
     */
    ////////////////////////////////////////////////////////////////////////////
    class Commands
    {
        public:
            using Function = std::function<std::string(const std::vector<std::string>& arguments)>;

            explicit Commands(Cvars& cvars);

            //! @throw std::invalid_argument if the name is not valid or is taken
            void add(std::string_view name, Function function, std::string_view help = {});

            //! Runs a line and gives the answer ("" for nothing to say)
            std::string run(std::string_view line);

            //! The words of a line: spaces separate them, "..." holds spaces
            static std::vector<std::string> split(std::string_view line);

            //! The commands and variables whose name starts with `prefix`, by name
            std::vector<std::string> complete(std::string_view prefix) const;

            Cvars& cvars(void) noexcept { return m_cvars; }

        private:
            struct Entry
            {
                Function    function;
                std::string help;
            };

            Cvars&                                  m_cvars;
            std::map<std::string, Entry, std::less<>> m_commands;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Lines of commands that wait for the next tick: they come from a thread that reads the keyboard, a socket, an admin
     *
     * push() from any thread; a system (installCommandQueue()) runs them in the tick, where it is safe to change the World, and
     * logs the answers under the category "console".
     */
    ////////////////////////////////////////////////////////////////////////////
    class CommandQueue
    {
        public:
            //! Any thread
            void push(std::string line);

            //! The lines waiting, oldest first, once
            std::vector<std::string> take(void);

        private:
            std::mutex              m_mutex;
            std::deque<std::string> m_lines;
    };

    //! Gives a scene the system that runs the lines of a queue in the tick (Fixed, stage Input) and logs each answer under the
    //! category "console". The commands and the queue must outlive the scene.
    void installCommandQueue(SceneSetup scene, Commands& commands, std::shared_ptr<CommandQueue> queue);

}
