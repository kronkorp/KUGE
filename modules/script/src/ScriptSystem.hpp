#pragma once

#include "ScriptEngine.hpp"
#include "kronkworld/Kronkworld.hpp"
#include <cstdint>
#include <memory>
#include <string>

namespace kuge
{

    struct ScriptSystemOptions
    {
        std::string    function = "update";   //!< The global function called at each tick: `update(dt)`
        std::uint32_t  seedSalt = 0;          //!< Mixed into the seed of each tick, so two systems of a game do not roll the same numbers
        std::uint32_t  reloadEvery = 0;       //!< In `--dev`: look for changed scripts every this many ticks (0: never, the default)
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A system that calls `update(dt)` of a ScriptEngine at each tick, in the stage the game chooses
     *
     *     auto scripts = std::make_shared<kuge::ScriptEngine>(registry);
     *     for (const auto& file : mods.scripts()) { scripts->load(file); }              // @throw ScriptError
     *     scene.addSystem(kuge::stage::Simulation, std::make_unique<kuge::ScriptSystem>(scripts, kuge::ScriptSystemOptions{.reloadEvery = dev ? 30 : 0}));
     *
     * - `dt` is the fixed tick of `kuge::Time` (a World without one gets 1/60).
     * - Before each call the generator of `math.random` is seeded from the tick (`seedFor(0, tick, seedSalt)`): a system run again
     *   (prediction, a replay) rolls the same numbers.
     * - `print` goes to the logger, category "script".
     * - **A script that fails is stopped, not the game.** The error (file, line, or "over its budget") is logged once, and the system
     *   calls nothing more until the scripts are reloaded successfully (`reloadEvery`, or `restart()`): a script that fails at each tick
     *   would fill the log 60 times a second and leave the world half changed 60 times a second.
     *
     * **Which side runs which script is the game's choice**, made with the registry it gives the engine. The room (the authority) runs the
     * scripts that change the rules, with a registry of every component. A client runs its own engine for the interface and the effects
     * with a registry of the components it owns (the ones that are not replicated): a script cannot name what it was not given, and
     * a replicated component written on a client is overwritten by the next snapshot anyway. The inputs are the only thing a client sends.
     */
    ////////////////////////////////////////////////////////////////////////////
    class ScriptSystem final : public kw::ISystem
    {
        public:
            explicit ScriptSystem(std::shared_ptr<ScriptEngine> engine, ScriptSystemOptions options = {});

            bool handle(kw::World& world) override;

            //! Has a failure stopped it?
            bool stopped(void) const noexcept { return m_stopped; }
            //! Lets it call the scripts again (after the game fixed what failed)
            void restart(void) noexcept { m_stopped = false; }

        private:
            std::shared_ptr<ScriptEngine>  m_engine;
            ScriptSystemOptions            m_options;
            bool                           m_stopped = false;
    };

}
