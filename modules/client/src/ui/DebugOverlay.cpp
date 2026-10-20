#include "ui/DebugOverlay.hpp"
#include <algorithm>
#include <format>

namespace
{
    constexpr std::size_t GAUGE_ROWS = 4;
}

kuge::DebugOverlay::DebugOverlay(kw::World& world, Config config) : m_config(config)
{
    const auto make = [&](UiNode node) {
        const kw::Entity entity = world.create();

        world.add<UiNode>(entity, node);
        return entity;
    };
    const auto label = [&](kw::Entity parent) {
        const kw::Entity entity = make({.parent = parent, .hasParent = true});

        world.add<UiLabel>(entity, UiLabel{});
        m_text.push_back(entity);
        return entity;
    };
    const auto graph = [&](kw::Entity parent, std::size_t bars, std::vector<kw::Entity>& into, Color fill) {
        const kw::Entity row = make({.parent = parent, .hasParent = true, .size = {static_cast<float>(bars) * 4.0f, 28.0f}});

        world.add<UiStack>(row, UiStack{.direction = Direction::Horizontal, .spacing = 1.0f, .padding = 0.0f, .align = Align::Stretch});
        for (std::size_t i = 0; i < bars; ++i) {
            const kw::Entity bar = make({.parent = row, .hasParent = true, .size = {3.0f, 28.0f}});

            world.add<UiProgress>(bar, UiProgress{.value = 0.0f, .max = 1.0f, .fill = fill, .back = {20, 22, 30, 200}, .border = {0, 0, 0, 0},
                .borderWidth = 0.0f, .vertical = true});
            into.push_back(bar);
        }
    };

    m_root = make({.anchor = config.anchor, .offset = config.offset, .visible = false, .layer = 90});
    world.add<UiPanel>(m_root, UiPanel{.fill = {10, 12, 20, 215}, .border = {90, 95, 120, 255}, .borderWidth = 1.0f});
    world.add<UiStack>(m_root, UiStack{.direction = Direction::Vertical, .spacing = 2.0f, .padding = 6.0f, .align = Align::Start});
    label(m_root);                                       // frames
    graph(m_root, config.bars, m_frameBars, {90, 200, 110, 255});
    label(m_root);                                       // tick and loop
    label(m_root);                                       // net
    graph(m_root, config.rtts, m_rttBars, {110, 170, 255, 255});
    for (std::size_t i = 0; i < GAUGE_ROWS + config.systems + 1; ++i) {
        label(m_root);                                   // gauges, then the slowest systems
    }
}

void kuge::DebugOverlay::setVisible(kw::World& world, bool visible)
{
    m_visible = visible;
    world.get<UiNode>(m_root).visible = visible;
}

std::vector<std::string> kuge::DebugOverlay::lines(kw::World& world) const
{
    std::vector<std::string> result;

    for (kw::Entity entity : m_text) {
        result.push_back(world.get<UiLabel>(entity).text);
    }
    return result;
}

void kuge::DebugOverlay::update(kw::World& world, const FrameStats& stats, const NetReport* net)
{
    if (!m_visible) {
        return;
    }
    const auto say = [&](std::size_t row, std::string text) { world.get<UiLabel>(m_text.at(row)).text = std::move(text); };
    // The row 0 is the frames, 1 the tick, 2 the net, then the gauges, then the systems
    const bool slow = stats.frameWorstPercent() > m_config.slowFrame;

    say(0, std::format("{:.0f} fps  {:.1f} ms  worst 1%: {:.1f} ms{}", stats.fps(), stats.frameMean() * 1000.0, stats.frameWorstPercent() * 1000.0,
        slow ? "  [SLOW]" : ""));
    say(1, std::format("tick {:.2f} ms  loop {:.2f} ms", stats.tickMean() * 1000.0, stats.workMean() * 1000.0));
    const std::vector<double> recent = stats.recent(m_frameBars.size());

    for (std::size_t i = 0; i < m_frameBars.size(); ++i) {
        // The newest on the right
        const std::size_t from = m_frameBars.size() - recent.size();
        const double seconds = i >= from ? recent[i - from] : 0.0;

        world.get<UiProgress>(m_frameBars[i]).value = static_cast<float>(std::min(seconds / m_config.slowFrame, 1.0));
        world.get<UiProgress>(m_frameBars[i]).fill = seconds > m_config.slowFrame ? Color{240, 100, 90, 255} : Color{90, 200, 110, 255};
    }
    if (net) {
        say(2, "net  " + describe(*net));
        m_rtts.push_back(std::max(net->rttSeconds, 0.0));
        if (m_rtts.size() > m_rttBars.size()) {
            m_rtts.erase(m_rtts.begin());
        }
    } else {
        say(2, "");
    }
    for (std::size_t i = 0; i < m_rttBars.size(); ++i) {
        const std::size_t from = m_rttBars.size() - m_rtts.size();
        const double rtt = i >= from ? m_rtts[i - from] : 0.0;

        world.get<UiProgress>(m_rttBars[i]).value = net ? static_cast<float>(std::min(rtt / m_config.rttFull, 1.0)) : 0.0f;
    }
    std::size_t row = 3;

    for (const auto& [name, value] : Gauges::global().all()) {
        if (row >= 3 + GAUGE_ROWS) {
            break;
        }
        say(row++, std::format("{} = {:g}", name, value));
    }
    while (row < 3 + GAUGE_ROWS) {
        say(row++, "");
    }
    if (!Profiler::enabled()) {
        say(row++, "systems: profiling is off (profile start)");
    } else {
        const auto rows = Profiler::instance().top(m_config.systems, Profiler::instance().now() > 1'000'000'000ull ? Profiler::instance().now() - 1'000'000'000ull : 0);

        say(row++, "slowest systems, last second:");
        for (std::size_t i = 0; i < m_config.systems; ++i) {
            say(row++, i < rows.size() ? std::format("{:7.3f} ms  {}", static_cast<double>(rows[i].total) / 1e6, rows[i].name) : std::string());
        }
    }
    while (row < m_text.size()) {
        say(row++, "");
    }
}
