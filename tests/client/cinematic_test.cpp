extern "C" {
    #include "kronklab/kronklab.h"
}
#include "audio_helpers.hpp"
#include "audio/Audio.hpp"
#include "audio/DummyAudio.hpp"
#include "cinematic/Cinematic.hpp"
#include "cinematic/CinematicScene.hpp"
#include "client_fixture.hpp"
#include "ClientReflect.hpp"
#include <cmath>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using namespace kuge;

    // (a comma inside braces would split the arguments of Assert)
    Color rgba(int r, int g, int b, int a) { return Color{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(a)}; }
    Vec2  vec(float x, float y) { return Vec2{x, y}; }

    std::shared_ptr<const Timeline> parse(const std::string& text)
    {
        return std::make_shared<const Timeline>(Timeline::fromJson(Json::parse(text), "test.json"));
    }

    // A world with one named entity, a camera, and the services a cinematic uses
    struct Stage
    {
        kw::World                 world;
        ComponentRegistry         registry;
        Camera2D                  camera;
        kw::Entity                boss;
        std::vector<std::string>  events;
        std::unique_ptr<Cinematic> cinematic;

        explicit Stage(const std::string& timeline, Audio* audio = nullptr, std::function<std::shared_ptr<Sound>(const std::string&)> sound = {},
            std::function<std::shared_ptr<Music>(const std::string&)> music = {})
        {
            describeCore(registry);
            describeClient(registry);
            boss = world.create();
            world.add<Transform2D>(boss, Transform2D{});
            Cinematic::Services services;

            services.world = &world;
            services.registry = &registry;
            services.camera = &camera;
            services.audio = audio;
            services.find = [this](const std::string& name) -> std::optional<kw::Entity> {
                return name == "boss" ? std::optional<kw::Entity>(boss) : std::nullopt;
            };
            services.sound = std::move(sound);
            services.music = std::move(music);
            services.event = [this](const std::string& name, const Json& key) { events.push_back(name + (key.find("data") ? ":" + key["data"].dump() : "")); };
            cinematic = std::make_unique<Cinematic>(parse(timeline), std::move(services));
        }

        void run(double seconds, double step = 0.1)
        {
            for (double done = 0.0; done < seconds - 1e-9; done += step) {
                cinematic->update(step);
            }
        }
        Vec2 position(void) { return world.get<Transform2D>(boss).position; }
        bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }
    };

    const char* const MOVE = R"({ "duration": 4, "tracks": [
        { "kind": "transform", "keys": [ { "t": 1, "duration": 2, "target": "boss", "to": { "position": [100, 40], "rotation": 90 } } ] } ] })";
}

Test(cinematic, a_transform_tweens)
{
    Stage stage(MOVE);

    stage.run(1.0);
    Assert(stage.near(stage.position().x, 0.0f), "not yet moving at the time of the key");
    stage.run(1.0);   // 2.0: half way
    Assert(stage.near(stage.position().x, 50.0f) && stage.near(stage.position().y, 20.0f), "half way after half the time: %f, %f", stage.position().x, stage.position().y);
    Assert(stage.near(stage.world.get<Transform2D>(stage.boss).rotation, 45.0f), "a number goes with it");
    stage.run(2.0);
    Assert(stage.position().x == 100.0f && stage.position().y == 40.0f, "and ends exactly at its value");
    Assert(stage.cinematic->finished(), "the cinematic is over");
}

Test(cinematic, a_curve_bends_the_move)
{
    Stage stage(R"({ "duration": 2, "tracks": [
        { "kind": "transform", "keys": [ { "t": 0, "duration": 2, "ease": "inQuad", "target": "boss", "to": { "position": [100, 0] } } ] } ] })");

    stage.run(1.0);
    Assert(stage.near(stage.position().x, 25.0f), "inQuad: a quarter of the way at half the time: %f", stage.position().x);
}

Test(cinematic, skip_is_the_end_state)
{
    const std::string file = R"({ "duration": 5, "tracks": [
        { "kind": "transform", "keys": [ { "t": 1, "duration": 2, "target": "boss", "to": { "position": [100, 40] } } ] },
        { "kind": "camera", "keys": [ { "t": 0.5, "duration": 1, "position": [10, 20], "zoom": 2 }, { "t": 2, "duration": 1, "shake": { "amplitude": 8 } } ] },
        { "kind": "fade", "keys": [ { "t": 3, "duration": 1, "color": [0, 0, 0, 1] } ] },
        { "kind": "text", "keys": [ { "t": 1, "duration": 3, "text": "Hello" } ] },
        { "kind": "event", "keys": [ { "t": 2.5, "name": "gate", "data": 7 }, { "t": 4.5, "name": "end" } ] } ] })";
    Stage played(file), skipped(file);

    played.run(6.0);
    skipped.run(1.2);
    skipped.cinematic->skip();
    Assert(skipped.cinematic->finished(), "skipped: it is over");
    Assert(skipped.position().x == played.position().x && skipped.position().y == played.position().y, "the same position");
    Assert(skipped.camera.position == played.camera.position && skipped.camera.zoom == played.camera.zoom, "the same camera (and no shake left)");
    Assert(skipped.cinematic->fade() == played.cinematic->fade() && skipped.cinematic->fade() == rgba(0, 0, 0, 255), "the same fade");
    Assert(!skipped.cinematic->subtitle() && !played.cinematic->subtitle(), "no line left on the screen");
    Assert(skipped.events == played.events && skipped.events.size() == 2, "the same events, in the same order");
    AssertStrEq(skipped.events[0].c_str(), "gate:7", "with their data");
    AssertStrEq(skipped.events[1].c_str(), "end", "in order");
}

Test(cinematic, the_same_twice)
{
    const std::string file = R"({ "duration": 3, "tracks": [
        { "kind": "camera", "keys": [ { "t": 0, "duration": 3, "shake": { "amplitude": 10 } } ] } ] })";
    Stage a(file), b(file);
    std::vector<Vec2> first, second;

    for (int i = 0; i < 35; ++i) {
        a.cinematic->update(0.1);
        b.cinematic->update(0.1);
        first.push_back(a.camera.position);
        second.push_back(b.camera.position);
    }
    Assert(first == second, "two plays give the same positions");
    Assert(first[3] != Vec2{} || first[4] != Vec2{}, "and the camera did shake");
    Assert(a.camera.position == Vec2{}, "back where it was at the end");
}

Test(cinematic, text_and_events)
{
    Stage stage(R"({ "duration": 6, "tracks": [
        { "kind": "text", "keys": [ { "t": 1, "duration": 2, "speaker": "Boss", "text": "You came." }, { "t": 4, "text": "Stays" }, { "t": 5, "text": "" } ] },
        { "kind": "event", "keys": [ { "t": 2, "name": "a" }, { "t": 2, "name": "b" } ] } ] })");

    stage.run(0.5);
    Assert(!stage.cinematic->subtitle(), "no line before its time");
    stage.run(1.0);
    Assert(stage.cinematic->subtitle() && stage.cinematic->subtitle()->speaker == "Boss" && stage.cinematic->subtitle()->text == "You came.", "the line, with its speaker");
    stage.run(2.0);   // 3.5
    Assert(!stage.cinematic->subtitle(), "gone when its duration is over");
    AssertEq(stage.events.size(), 2u, "both events came");
    stage.run(1.0);   // 4.5
    Assert(stage.cinematic->subtitle() && stage.cinematic->subtitle()->text == "Stays", "a line with no duration stays");
    stage.run(1.0);   // 5.5
    Assert(!stage.cinematic->subtitle(), "until an empty text clears it");
}

Test(cinematic, the_fade_goes_to_its_colour)
{
    Stage stage(R"({ "duration": 3, "tracks": [
        { "kind": "fade", "keys": [ { "t": 0, "duration": 2, "color": [1, 0, 0, 1] }, { "t": 2, "color": [0, 0, 0, 0] } ] } ] })");

    Assert(stage.cinematic->fade() == rgba(0, 0, 0, 0), "clear at first");
    stage.run(1.0);
    Assert(stage.cinematic->fade() == rgba(128, 0, 0, 128), "half way");
    stage.run(1.0);
    Assert(stage.cinematic->fade() == rgba(0, 0, 0, 0), "and an instant key sets it at once");
}

Test(cinematic, keys_that_fail_are_named)
{
    Stage stage(R"({ "duration": 3, "tracks": [
        { "kind": "transform", "keys": [ { "t": 0, "target": "nobody", "to": { "position": [1, 1] } },
                                         { "t": 0, "target": "boss", "to": { "banana": 1 } },
                                         { "t": 0, "target": "boss", "component": "Ghost", "to": { "x": 1 } },
                                         { "t": 1, "target": "boss", "to": { "position": [5, 5] } } ] },
        { "kind": "event", "keys": [ { "t": 2, "name": "still_there" } ] } ] })");

    stage.run(3.0);
    Assert(stage.cinematic->finished(), "the cinematic goes on");
    Assert(stage.position().x == 5.0f, "the good key worked");
    AssertEq(stage.events.size(), 1u, "and the events came");
}

Test(cinematic, any_component_tweens)
{
    Stage stage(R"({ "duration": 2, "tracks": [
        { "kind": "transform", "keys": [ { "t": 0, "duration": 2, "target": "boss", "component": "Sprite", "to": { "size": [10, 20], "visible": false, "frame": 4 } } ] } ] })");

    stage.world.add<Sprite>(stage.boss, Sprite{});
    stage.run(1.0);
    Assert(stage.world.get<Sprite>(stage.boss).visible, "a bool waits for the end");
    stage.run(1.0);
    const Sprite& sprite = stage.world.get<Sprite>(stage.boss);

    Assert(sprite.size == vec(10.0f, 20.0f) && !sprite.visible && sprite.frame == 4, "and a described component goes to its values");
}

Test(cinematic, audio_and_the_skip)
{
    DummyAudio dummy;
    Audio mixer(dummy);
    SoundFile file("cinematic");
    std::vector<std::shared_ptr<Sound>> loaded;
    Stage stage(R"({ "duration": 4, "tracks": [
        { "kind": "audio", "keys": [ { "t": 1, "sound": "voice.wav", "bus": "dialogue", "volume": 0.5 }, { "t": 2, "music": "theme.ogg", "loop": false, "fadeIn": 1.5 },
                                     { "t": 3, "stopMusic": 2 } ] } ] })", &mixer,
        [&](const std::string&) { loaded.push_back(std::make_shared<Sound>(dummy, dummy.loadSound(file.path))); return loaded.back(); },
        [&](const std::string&) { return std::make_shared<Music>(dummy, dummy.loadMusic(file.path)); });

    stage.run(1.5);
    AssertEq(mixer.voices(), 1u, "the sound plays at its time");
    Assert(dummy.music().playing == false, "the music not yet");
    stage.run(1.0);
    Assert(dummy.music().playing && !dummy.music().loop && dummy.music().fadeIn == 1.5f, "the music, with its options");
    stage.run(1.0);
    Assert(dummy.music().fadeOut == 2.0f, "and it is stopped with a fade");

    Stage cut(R"({ "duration": 4, "tracks": [ { "kind": "audio", "keys": [ { "t": 1, "sound": "voice.wav" }, { "t": 2, "music": "theme.ogg" } ] } ] })", &mixer,
        [&](const std::string&) { loaded.push_back(std::make_shared<Sound>(dummy, dummy.loadSound(file.path))); return loaded.back(); },
        [&](const std::string&) { return std::make_shared<Music>(dummy, dummy.loadMusic(file.path)); });
    const std::size_t voices = mixer.voices();

    cut.cinematic->skip();
    AssertEq(mixer.voices(), voices, "a skip does not play the sounds it jumps over");
    Assert(dummy.music().playing, "but the music that should be playing is");
}

Test(cinematic, a_scene_plays_and_ends)
{
    int done = 0;
    bool skipped = false;
    std::vector<std::string> events;
    Fixture fx;
    CinematicScene::Config config;

    config.timeline = parse(R"({ "duration": 1, "tracks": [
        { "kind": "transform", "keys": [ { "t": 0, "duration": 1, "target": "hero", "to": { "position": [30, 0] } } ] },
        { "kind": "fade", "keys": [ { "t": 0, "color": [0, 0, 0, 1] } ] },
        { "kind": "event", "keys": [ { "t": 0.5, "name": "half" } ] } ] })");
    config.setup = [](CinematicScene& scene) {
        const kw::Entity hero = scene.stage().create();

        scene.stage().add<Transform2D>(hero, Transform2D{});
        scene.name("hero", hero);
    };
    config.onEvent = [&](const std::string& name, const Json&) { events.push_back(name); };
    config.onDone = [&](SceneContext&, bool wasSkipped) { ++done; skipped = wasSkipped; };
    fx.engine.scenes().change<CinematicScene>(config);
    fx.engine.step(0.0);
    const auto& frame = fx.frame(0.1);
    bool covered = false;

    for (const auto& call : frame) {
        covered = covered || (call.kind == DummyRenderer::Call::Kind::Fill && call.color == rgba(0, 0, 0, 255) && call.destination.w >= 640.0f);
    }
    Assert(covered, "the fade covers the screen");
    AssertEq(done, 0, "not over yet");
    for (int i = 0; i < 6; ++i) {
        fx.frame(0.1);   // (a frame longer than a quarter of a second is cut short by the engine)
    }
    AssertStrEq(events.empty() ? "" : events[0].c_str(), "half", "the event came");
    for (int i = 0; i < 6; ++i) {
        fx.frame(0.1);
    }
    AssertEq(done, 1, "over, and told once");
    Assert(!skipped, "it was played to the end");
}

Test(cinematic, escape_skips_the_scene)
{
    int done = 0;
    bool skipped = false;
    Fixture fx;
    CinematicScene::Config config;

    config.timeline = parse(R"({ "duration": 30, "tracks": [ { "kind": "event", "keys": [ { "t": 29, "name": "the_end" } ] } ] })");
    std::vector<std::string> events;

    config.onEvent = [&](const std::string& name, const Json&) { events.push_back(name); };
    config.onDone = [&](SceneContext&, bool wasSkipped) { ++done; skipped = wasSkipped; };
    fx.engine.scenes().change<CinematicScene>(config);
    fx.engine.step(0.0);
    fx.frame(0.2);
    AssertEq(done, 0, "playing");
    fx.dummy.input->push(KeyEvent{Key::Escape, true});
    fx.dummy.input->push(KeyEvent{Key::Escape, false});
    fx.frame(0.1);
    fx.frame(0.1);
    AssertEq(done, 1, "Escape ends it");
    Assert(skipped, "as a skip");
    AssertEq(events.size(), 1u, "with the events that were left");
    fx.frame(0.2);
    AssertEq(done, 1, "and it is told once");
}
