#include "EngineCommands.hpp"
#include "Logger.hpp"
#include "Profiler.hpp"
#include <charconv>
#include <format>

namespace
{
    bool parseOn(const std::string& word, bool& on)
    {
        if (word == "on" || word == "1" || word == "true") {
            on = true;
            return true;
        }
        if (word == "off" || word == "0" || word == "false") {
            on = false;
            return true;
        }
        return false;
    }
}

void kuge::registerEngineCommands(Engine& engine, Commands& commands, std::function<std::string(void)> refuse)
{
    Cvars& cvars = commands.cvars();
    const auto allowed = [&cvars, refuse = std::move(refuse)]() -> std::string {
        if (cvars.side() == Cvars::Side::Server && !cvars.cheats()) {
            return "on a server, time commands need cheats";
        }
        return refuse ? refuse() : std::string();
    };

    commands.add("pause", [&engine, allowed](const std::vector<std::string>& args) {
        if (const std::string why = allowed(); !why.empty()) {
            return why;
        }
        bool on = !engine.paused();

        if (!args.empty() && !parseOn(args[0], on)) {
            return std::string("pause [on|off]");
        }
        engine.setPaused(on);
        return std::string(on ? "paused (step runs a tick)" : "running");
    }, "pause [on|off]: stop the fixed ticks");
    commands.add("step", [&engine, allowed](const std::vector<std::string>& args) {
        if (const std::string why = allowed(); !why.empty()) {
            return why;
        }
        std::uint32_t count = 1;

        if (!args.empty()) {
            const auto [end, error] = std::from_chars(args[0].data(), args[0].data() + args[0].size(), count);

            if (error != std::errc() || end != args[0].data() + args[0].size() || count == 0 || count > 100000) {
                return std::string("step [n]: n is a number from 1 to 100000");
            }
        }
        engine.setPaused(true);
        engine.advance(count);
        return std::format("{} tick(s), then paused", count);
    }, "step [n]: run n fixed ticks and stay paused");
    commands.add("timescale", [&engine, allowed](const std::vector<std::string>& args) {
        if (const std::string why = allowed(); !why.empty()) {
            return why;
        }
        double scale = 0.0;

        if (args.empty()) {
            return std::format("timescale = {}", engine.timeScale());
        }
        const auto [end, error] = std::from_chars(args[0].data(), args[0].data() + args[0].size(), scale);

        if (error != std::errc() || end != args[0].data() + args[0].size() || !(scale >= 0.0) || scale > 100.0) {
            return std::string("timescale <x>: x is a number from 0 to 100 (1 is real time)");
        }
        engine.setTimeScale(scale);
        return std::format("timescale = {}", engine.timeScale());
    }, "timescale <x>: slower (0.25) or faster (2) time");
    commands.add("profile", [](const std::vector<std::string>& args) {
        Profiler& profiler = Profiler::instance();
        const std::string what = args.empty() ? "" : args[0];

        if (what == "start") {
            profiler.enable(true);
            return std::string("profiling");
        }
        if (what == "stop") {
            profiler.enable(false);
            return std::string("stopped (save writes the trace)");
        }
        if (what == "clear") {
            profiler.clear();
            return std::string("cleared");
        }
        if (what == "save" && args.size() >= 2) {
            profiler.saveTrace(args[1]);   // (a command that throws is an answer)
            return std::format("{} events written to {}", profiler.events().size(), args[1]);
        }
        if (what == "top") {
            std::size_t count = 10;

            if (args.size() >= 2) {
                count = static_cast<std::size_t>(std::max(1, std::atoi(args[1].c_str())));
            }
            std::string text;

            for (const auto& row : profiler.top(count)) {
                text += std::format("{}{:8.3f} ms  {:6} calls  {}  [{}]", text.empty() ? "" : "\n", static_cast<double>(row.total) / 1e6, row.calls, row.name, row.category);
            }
            return text.empty() ? std::string(Profiler::enabled() ? "nothing recorded yet" : "profiling is off (profile start)") : text;
        }
        return std::string("profile start | stop | clear | top [n] | save <file>");
    }, "profile start|stop|clear|top [n]|save <file>: the profiler");
    commands.add("fps", [&engine](const std::vector<std::string>&) {
        const FrameStats& stats = engine.stats();

        return std::format("{:.1f} fps, frame {:.2f} ms (worst 1%: {:.2f} ms), tick {:.3f} ms, loop {:.2f} ms{}", stats.fps(), stats.frameMean() * 1000.0,
            stats.frameWorstPercent() * 1000.0, stats.tickMean() * 1000.0, stats.workMean() * 1000.0, engine.paused() ? " [paused]" : "");
    }, "frames per second and what they cost");
}
