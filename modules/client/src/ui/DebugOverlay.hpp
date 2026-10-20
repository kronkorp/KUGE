#pragma once

#include "Profiler.hpp"
#include "Stats.hpp"
#include "kronkworld/Kronkworld.hpp"
#include "ui/Ui.hpp"
#include <optional>
#include <string>
#include <vector>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The numbers of the engine in a corner of the window: frames per second, a graph of the last frames, the slowest systems,
     *         other gauges, and the network (the "net graph")
     *
     * Built from UiLabel, UiPanel and UiProgress in the World of a scene (after installUi()); the game keeps it, shows it on a key
     * and calls update() each frame. Nothing is computed while it is hidden.
     *
     *     DebugOverlay overlay(world());                                                 // hidden at first
     *     if (actions.wasPressed(Action::Stats)) overlay.setVisible(world(), !overlay.visible());
     *     overlay.update(world(), engine.stats(), &netReport);                           // netReport: from a net::NetMeter, or nothing
     *
     * The slowest systems come from the Profiler: when it is off the overlay says how to turn it on (`profile start` in the console).
     * The network numbers are the lines of describe(NetReport), plus a graph of the round trip of the last 5 seconds.
     */
    ////////////////////////////////////////////////////////////////////////////
    class DebugOverlay
    {
        public:
            struct Config
            {
                Anchor      anchor = Anchor::TopRight;
                Vec2        offset{-12.0f, 12.0f};
                std::size_t bars = 60;           //!< Frames in the graph
                double      slowFrame = 1.0 / 20.0;   //!< A bar is full at this frame time
                std::size_t systems = 8;         //!< Rows of the slowest systems
                std::size_t rtts = 50;           //!< Points of the round trip graph
                double      rttFull = 0.25;      //!< A bar of the round trip is full at this
            };

            explicit DebugOverlay(kw::World& world) : DebugOverlay(world, Config()) {}
            DebugOverlay(kw::World& world, Config config);

            void setVisible(kw::World& world, bool visible);
            bool visible(void) const noexcept { return m_visible; }

            //! @param net  The network numbers, or none for a game with no network
            void update(kw::World& world, const FrameStats& stats, const NetReport* net = nullptr);

            //! The texts on the lines, from the top (for tests)
            std::vector<std::string> lines(kw::World& world) const;

        private:
            Config                  m_config;
            kw::Entity              m_root = 0;
            std::vector<kw::Entity> m_text;       //!< Labels: frames, tick, net, gauges, the systems
            std::vector<kw::Entity> m_frameBars;
            std::vector<kw::Entity> m_rttBars;
            std::vector<double>     m_rtts;
            bool                    m_visible = false;
    };

}
