#pragma once

#include "Reflect.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace kuge
{

    //! A script that cannot be loaded; the message names the file and the line ("mods/wand/ai.lua:12: unexpected symbol near 'end'")
    class ScriptError : public std::runtime_error
    {
        public:
            using std::runtime_error::runtime_error;
    };

    struct ScriptOptions
    {
        std::uint64_t  seed = 1;                //!< Of `math.random` (see ScriptEngine::seed())
        std::uint64_t  budget = 1'000'000;      //!< Instructions of the virtual machine that one load() or call() may run
        std::size_t    memory = 64u << 20;      //!< Bytes the scripts may hold
        kw::Entity     maxEntity = 1u << 20;    //!< A script that names an entity above this is stopped (the world grows to hold any number it is given)
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A Lua state for the scripts of a mod (or of a game), that reads and writes the world by the names of the ComponentRegistry
     *
     *     kuge::ComponentRegistry registry;  kuge::describeCore(registry);  registry.describe<Health>(...);
     *     kuge::ScriptEngine scripts(registry);
     *     scripts.load("mods/wand/ai.lua");                             // @throw ScriptError: names the file and the line
     *     auto result = scripts.call(world, "update", {dt});            // {ok, message}: a script that fails is stopped, not the game
     *
     * In a script, `world` is the only door to the game:
     *
     *     function update(dt)
     *         for _, e in ipairs(world:view{"Health", "Enemy"}) do          -- a list (taken now, so removing in the loop is safe), by ascending number
     *             local h = world:get(e, "Health")                         -- {points = 80, regen = 0.5}, or nil if e has none
     *             if h.points <= 0 then world:remove(e)                    -- the entity is gone
     *             else world:set(e, "Health", "points", h.points - 1) end  -- or world:set(e, "Health", {points = 1, regen = 0})
     *         end
     *         local boss = world:spawn{Health = {points = 500}, Transform2D = {position = {0, 0}}}
     *         world:add(boss, "Enemy"); world:drop(boss, "Enemy"); world:has(boss, "Enemy"); world:components(boss)
     *     end
     *
     * A value is what the registry writes in JSON: a number, a boolean, a string, an enum by its name, a Vec2/Vec3/Vec4 as `{x, y}`, `{x, y, z}`,
     * `{x, y, z, w}`, an entity as its number. A wrong name, type or limit stops the script with the message of the registry (and changes nothing).
     *
     * **Scopes: how several mods live together.** A script is loaded in a *scope* (a mod gives its name). The files of a scope share their
     * globals; each scope has **its own**: a `function update` or a variable of one mod can never replace another's, and
     * `call(world, "update")` runs the `update` of **every** scope, in load order. A scope reads the standard library and `world` through
     * its globals, and `_G` is its own table. Scopes cannot call one another (they meet in the components of the game). The sandbox is the same
     * for all, and they do share the standard tables (`string`, `math`...): a mod that changes `math.floor` changes it for all.
     *
     * **Sandbox.** Only the base (without `dofile`, `loadfile`, `load`, `collectgarbage`), `string`, `table`, `math`, `utf8` and `coroutine` libraries
     * exist: no `io`, `os`, `package`, `debug`. A chunk is text (no bytecode). `math.random` is KUGE's generator (PCG32, seeded with seed()) and
     * there is no clock, so a script gives the same results for the same world and the same seed. A load or a call that runs over `budget` instructions
     * or `memory` bytes is stopped. **What the sandbox does not stop:** a long call of the standard library in one instruction (`string.rep` is
     * limited by `memory`; a pathological `string.find` pattern is not), and the order of `pairs` over a table of strings (it does not change from
     * a run to the next, but do not rely on it between versions of Lua: sort the keys).
     *
     * A ScriptEngine is not thread-safe; two engines are independent.
     */
    ////////////////////////////////////////////////////////////////////////////
    class ScriptEngine
    {
        public:
            struct Result
            {
                bool         ok = true;
                std::string  message;   //!< Why not, with the file and the line
            };

            //! What reloadChanged() did
            struct Reload
            {
                bool         reloaded = false;   //!< The scripts ran again in a new state
                std::string  error;              //!< Else, if not empty: why a changed script was refused (the old ones keep running)
            };

            explicit ScriptEngine(const ComponentRegistry& registry, ScriptOptions options = {});
            ~ScriptEngine();
            ScriptEngine(const ScriptEngine&) = delete;
            ScriptEngine& operator=(const ScriptEngine&) = delete;

            //! Reads and runs a file (its top level: it defines functions, `world` cannot be used yet), in the scope `scope` (see the class
            //! description: the files of a scope share their globals, two scopes never do). @throw ScriptError
            void load(const std::filesystem::path& file, const std::string& scope = {});
            //! Same for a text; `name` is what the messages call it. @throw ScriptError
            void loadString(std::string_view code, const std::string& name, const std::string& scope = {});

            //! Hot reload: if a file given to load() changed (its date or its size), everything that was loaded runs again, in the same
            //! order, in a **new** state (what the scripts kept in globals is lost). If any script fails, nothing changes: the old state keeps
            //! running, the error is returned once, and the files are looked at again when one changes. A text given to loadString() is run
            //! again as it was. Cheap when nothing changed (a stat per file).
            Reload reloadChanged(void);

            //! The scopes that define a function of that name, in load order (a scope that was only given a text with no `scope` is "")
            std::vector<std::string> scopes(const std::string& function) const;

            //! Does any scope define a function of that name?
            bool has(const std::string& function) const;

            //! Calls `function(args...)` of **every** scope that defines it, in load order, with `world` bound to this world. A scope that fails
            //! does not stop the others: `ok` is false if any did, and `message` has one line for each. It never throws for a script's
            //! mistake: it says why not. Nothing defines it: `ok` is false too.
            Result call(kw::World& world, const std::string& function, std::span<const double> args = {});

            //! Calls the `function` of one scope only (ScriptSystem uses it to stop the scope that fails and keep the others)
            Result call(kw::World& world, const std::string& scope, const std::string& function, std::span<const double> args);

            //! The next numbers of `math.random` come from this seed on (the same seed, the same numbers)
            void seed(std::uint64_t seed);

            //! Where `print` goes (nowhere by default: the logger of the game is the right place)
            void onPrint(std::function<void(const std::string&)> sink);

            struct Impl;   //!< Not for the users: the state of Lua and what the scripts' functions need

        private:
            std::unique_ptr<Impl> m_impl;
    };

}
