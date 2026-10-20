#include "ClientCommands.hpp"
#include <format>

void kuge::registerClientCommands(ClientModule& client, Commands& commands)
{
    commands.add("screenshot", [&client, count = 0](const std::vector<std::string>& args) mutable {
        const std::string file = args.empty() ? std::format("screenshot_{}.ppm", ++count) : args[0];

        client.saveScreenshot(file);
        return std::format("screenshot of the next frame: {}", file);
    }, "screenshot [file]: save the next frame as a PPM picture");
}
