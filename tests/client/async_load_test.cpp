extern "C" {
    #include "kronklab/kronklab.h"
}
#include "client_fixture.hpp"
#include "Serializer.hpp"
#include <chrono>
#include <thread>
#include <unistd.h>

// NOTE: kronklab test names are limited to 31 characters.

Test(async_load, a_texture_arrives)
{
    Fixture fx;
    const auto path = std::filesystem::temp_directory_path() / ("kuge_test_" + std::to_string(::getpid()) + "_async.bmp");
    // A 1x1 BMP
    kuge::ByteWriter out;

    out.write<char>('B');
    out.write<char>('M');
    out.write<std::uint32_t>(58);
    out.write<std::uint32_t>(0);
    out.write<std::uint32_t>(54);
    out.write<std::uint32_t>(40);
    out.write<std::int32_t>(1);
    out.write<std::int32_t>(1);
    out.write<std::uint16_t>(1);
    out.write<std::uint16_t>(24);
    out.write<std::uint32_t>(0);
    out.write<std::uint32_t>(4);
    for (int i = 0; i < 4; ++i) { out.write<std::uint32_t>(0); }
    for (std::uint8_t byte : {0, 0, 255, 0}) { out.write<std::uint8_t>(byte); }   // one red pixel
    kuge::writeFile(path, out.bytes());

    const auto ticket = fx.client->textures().loadAsync(path);
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);

    while (!ticket->done() && std::chrono::steady_clock::now() < end) {
        fx.engine.step(0.0);      // the client finishes the load at the start of each loop
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Assert(ticket->asset() != nullptr, "the texture arrives, after a few loops");
    const auto* info = fx.dummy.renderer->texture(ticket->asset()->id());

    Assert(info && info->width == 1 && info->rgba[0] == 255 && info->rgba[1] == 0, "with the pixel of the file");
    Assert(fx.client->textures().load(path) == ticket->asset(), "and it is the one load() gives too");
    std::filesystem::remove(path);
}

Test(async_load, a_bad_picture_fails)
{
    Fixture fx;
    const auto ticket = fx.client->textures().loadAsync("/nonexistent/picture.png");
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);

    while (!ticket->done() && std::chrono::steady_clock::now() < end) {
        fx.engine.step(0.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Assert(ticket->state() == kuge::AssetManager<kuge::Texture>::Ticket::State::Failed, "failed, and the game goes on");
    Assert(!ticket->error().empty(), "with a reason");
}
