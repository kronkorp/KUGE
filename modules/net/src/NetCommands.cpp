#include "NetCommands.hpp"
#include <charconv>
#include <chrono>
#include <cmath>
#include <format>

namespace
{
    using namespace kuge::net;

    struct Simulation
    {
        double lagMs = 0.0, jitterMs = 0.0, lossPercent = 0.0, dupPercent = 0.0;
        bool   on = false;

        LoopbackNetwork::Conditions conditions(void) const
        {
            LoopbackNetwork::Conditions c;

            c.latency = lagMs / 2000.0;       // half each way
            c.jitter = jitterMs / 1000.0;
            c.loss = lossPercent / 100.0;
            c.duplicate = dupPercent / 100.0;
            c.seed = 1;
            return c;
        }
    };

    bool number(const std::vector<std::string>& args, double& value, double maximum)
    {
        if (args.empty()) {
            return false;
        }
        const auto [end, error] = std::from_chars(args[0].data(), args[0].data() + args[0].size(), value);

        return error == std::errc() && end == args[0].data() + args[0].size() && std::isfinite(value) && value >= 0.0 && value <= maximum;
    }

    double steady(void)
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
}

void kuge::net::registerNetCommands(Commands& commands, std::function<Endpoint*(void)> endpoint, ConnectionId connection)
{
    auto state = std::make_shared<Simulation>();
    auto meter = std::make_shared<NetMeter>();
    const auto setter = [state, endpoint](const char* name, double Simulation::*field, double maximum, const char* unit) {
        return [state, endpoint, name, field, maximum, unit](const std::vector<std::string>& args) -> std::string {
            double value = 0.0;

            if (!number(args, value, maximum)) {
                return std::format("{} <{}>: a number from 0 to {}", name, unit, maximum);
            }
            Endpoint* target = endpoint();

            if (!target) {
                return "not connected";
            }
            (*state).*field = value;
            state->on = true;
            target->setSimulation(state->conditions());
            return std::format("net: lag {} ms (round trip), jitter {} ms, loss {}%, duplicates {}%", state->lagMs, state->jitterMs, state->lossPercent, state->dupPercent);
        };
    };

    commands.add("net_lag", setter("net_lag", &Simulation::lagMs, 5000.0, "ms"), "net_lag <ms>: add to the round trip");
    commands.add("net_jitter", setter("net_jitter", &Simulation::jitterMs, 2000.0, "ms"), "net_jitter <ms>: packets vary by up to this much");
    commands.add("net_loss", setter("net_loss", &Simulation::lossPercent, 100.0, "%"), "net_loss <percent>: packets lost, each way");
    commands.add("net_dup", setter("net_dup", &Simulation::dupPercent, 100.0, "%"), "net_dup <percent>: packets that arrive twice");
    commands.add("net_sim", [state, endpoint](const std::vector<std::string>& args) -> std::string {
        if (args.empty() || args[0] != "off") {
            return "net_sim off: back to the real network";
        }
        *state = Simulation{};
        if (Endpoint* target = endpoint()) {
            target->setSimulation(std::nullopt);
        }
        return "net: the real network";
    }, "net_sim off: lift the simulation");
    commands.add("net_stats", [endpoint, meter, connection](const std::vector<std::string>&) -> std::string {
        Endpoint* target = endpoint();

        if (!target) {
            return "not connected";
        }
        return kuge::describe(meter->update(*target, connection, steady()));
    }, "net_stats: round trip, bytes and packets a second, resends");
}
