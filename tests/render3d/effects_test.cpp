extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Audio3D.hpp"
#include "Particles3D.hpp"
#include "audio/DummyAudio.hpp"
#include "fixture.hpp"
#include <cmath>
#include <fstream>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using Vec3 = k3::Math::Vector3f;

    // What the 3D image shows at a pixel, in the last frame
    const std::uint8_t* seen(render3d::Fixture& fx, int x, int y)
    {
        for (const auto& call : fx.dummy.renderer->lastFrame()) {
            if (call.kind == kuge::DummyRenderer::Call::Kind::Texture && call.destination.w == 64.0f) {
                return fx.dummy.renderer->texture(call.texture.texture)->rgba.data() + (y * 64 + x) * 4;
            }
        }
        return nullptr;
    }

    void lookFromFive(render3d::Fixture& fx)
    {
        auto& camera = fx.world().getResource<k3::Camera>();

        camera.position = {0.0f, 0.0f, 5.0f};
        camera.lookAt({0.0f, 0.0f, 0.0f});
        fx.world().getResource<k3::Environment>().clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
    }
}

Test(particles, a_burst_lives_and_goes)
{
    kuge::Particles3D particles;

    particles.burst({.count = 25, .life = 0.4f});
    AssertEq(particles.count(), 25u, "twenty-five");
    particles.step(0.1f);
    AssertEq(particles.count(), 25u, "still there after a tenth of a second (each lives at least 40% of it)");
    for (int i = 0; i < 5; ++i) {
        particles.step(0.1f);
    }
    AssertEq(particles.count(), 0u, "gone when they are old");
    particles.setLimit(10);
    particles.burst({.count = 30});
    AssertEq(particles.count(), 10u, "no more than the limit: the oldest go");
}

Test(particles, they_fall_and_fly_where_told)
{
    // Alive, but not where the camera looks any more: down with a great gravity, or up fast in a direction
    for (int up = 0; up < 2; ++up) {
        render3d::Fixture fx;

        lookFromFive(fx);
        if (up) {
            fx.world().getResource<kuge::Particles3D>().burst({.count = 20, .speed = 100.0f, .life = 2.0f, .size = 1.0f, .gravity = 0.0f,
                .color = {1.0f, 0.0f, 0.0f, 1.0f}, .direction = {0.0f, 1.0f, 0.0f}});
        } else {
            fx.world().getResource<kuge::Particles3D>().burst({.count = 20, .speed = 0.0f, .life = 2.0f, .size = 1.0f, .gravity = 400.0f,
                .color = {1.0f, 0.0f, 0.0f, 1.0f}});
        }
        for (int i = 0; i < 3; ++i) {
            fx.engine.step(0.1);
        }
        AssertEq(fx.world().getResource<kuge::Particles3D>().count(), 20u, "still alive");
        const std::uint8_t* middle = seen(fx, 32, 24);

        Assert(middle && middle[0] < 10, "%s: and out of the view: %d", up ? "thrown up" : "falling", middle ? middle[0] : -1);
    }
}

Test(particles, they_are_drawn_and_fade)
{
    render3d::Fixture fx;

    lookFromFive(fx);
    fx.world().getResource<kuge::Particles3D>().burst({.count = 30, .speed = 0.0f, .life = 0.5f, .size = 3.0f, .gravity = 0.0f,
        .color = {1.0f, 0.0f, 0.0f, 1.0f}});
    fx.engine.step(0.01);
    const std::uint8_t* middle = seen(fx, 32, 24);

    Assert(middle && middle[0] > 60 && middle[1] < 30 && middle[2] < 30, "a red cloud in front of the camera: %d %d %d", middle[0], middle[1], middle[2]);
    for (int i = 0; i < 8; ++i) {
        fx.engine.step(0.1);
    }
    AssertEq(fx.world().getResource<kuge::Particles3D>().count(), 0u, "gone");
    middle = seen(fx, 32, 24);
    Assert(middle && middle[0] < 10, "and the screen is black again: %d", middle[0]);
}

Test(audio3d, near_far_left_right)
{
    const render3d::Folder folder("audio3d");
    const auto file = folder.write("beep.wav", "not a sound: the dummy only looks that it is there");
    kuge::DummyAudio backend;
    kuge::Audio audio(backend);
    const kuge::Sound sound(backend, backend.loadSound(file));
    k3::Camera camera;

    camera.position = {10.0f, 2.0f, 10.0f};
    camera.lookAt({10.0f, 2.0f, 0.0f});                 // toward -z: its right is +x
    const auto ears = kuge::listenerOf(camera, 40.0f, 10.0f);

    const auto here = kuge::spatialize({10.0f, 2.0f, 10.0f}, ears);

    Assert(here.volume == 1.0f && here.pan == 0.0f, "on the spot: full, in the middle");
    const auto right = kuge::spatialize({15.0f, 2.0f, 10.0f}, ears);

    Assert(right.pan > 0.4f && right.pan < 0.6f && right.volume > 0.8f, "5 m to the right: on the right side: %f %f", right.pan, right.volume);
    Assert(kuge::spatialize({0.0f, 2.0f, 10.0f}, ears).pan == -1.0f, "10 m or more to the left: all on the left");
    const auto ahead = kuge::spatialize({10.0f, 2.0f, 0.0f}, ears);
    const auto behind = kuge::spatialize({10.0f, 2.0f, 20.0f}, ears);

    Assert(ahead.pan == 0.0f && behind.pan == 0.0f && ahead.volume == behind.volume, "ahead and behind sound the same: it is the side that pans");
    Assert(kuge::spatialize({10.0f, 2.0f, -40.0f}, ears).volume == 0.0f, "beyond the range, nothing");
    Assert(kuge::spatialize({10.0f, 2.0f, -10.0f}, ears).volume < kuge::spatialize({10.0f, 2.0f, 5.0f}, ears).volume, "farther is quieter");

    // The ears follow the camera: turned a quarter turn to the right, what was ahead is on the left
    camera.yaw = 3.14159265f / 2.0f;
    const auto turned = kuge::listenerOf(camera);

    Assert(kuge::spatialize({10.0f, 2.0f, 0.0f}, turned).pan < -0.3f, "a quarter turn: what was ahead is on the left");

    const auto voice = kuge::playAt(audio, sound, {15.0f, 2.0f, 10.0f}, ears, {.volume = 0.5f});

    Assert(voice && backend.voice(voice.id)->pan == right.pan && std::fabs(backend.voice(voice.id)->volume - 0.5f * right.volume) < 1e-5f,
        "played with the volume of the sound times the distance's, and the pan");
    AssertEq(backend.activeVoices(), 1u, "one voice");
    Assert(!kuge::playAt(audio, sound, {10.0f, 2.0f, -100.0f}, ears), "out of hearing: no voice");
    AssertEq(backend.activeVoices(), 1u, "and no voice was taken");
}
