extern "C" {
    #include "kronklab/kronklab.h"
}
#include "animation/Animation.hpp"
#include "client_fixture.hpp"
#include <filesystem>
#include <fstream>
#include <unistd.h>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    const char* CLIPS = R"(# a hero
clip idle   fps=6  frames=0-3
clip run    fps=12 frames=4,5,6,7,6,5     # a walk cycle
clip attack fps=14 frames=8-11 loop=false next=idle cue.2=hit cue.0=swing
)";

    std::string errorOf(const std::string& text)
    {
        try {
            kuge::AnimationSet::parse(text);
        } catch (const kuge::AnimationError& error) {
            return error.what();
        }
        return {};
    }

    bool mentions(const std::string& message, const char* part) { return message.find(part) != std::string::npos; }

    std::filesystem::path tempPath(const char* name)
    {
        return std::filesystem::temp_directory_path() / ("kuge_test_" + std::to_string(::getpid()) + "_" + name);
    }

    // A sheet of 4 x 3 cells of 8 x 8, made by the renderer of the fixture: nothing
    // outside the world holds it (a texture must not outlive the renderer)
    std::shared_ptr<kuge::Spritesheet> makeSheet(kw::World& world)
    {
        auto sheet = std::make_shared<kuge::Spritesheet>();
        std::vector<std::uint8_t> pixels(32 * 24 * 4, 255);

        sheet->texture = kuge::Texture::fromPixels(*world.getResource<kuge::Ref<kuge::IRenderer2D>>(), 32, 24, pixels);
        sheet->frameWidth = 8;
        sheet->frameHeight = 8;
        return sheet;
    }

    kuge::Rect cell(int index) { return {static_cast<float>((index % 4) * 8), static_cast<float>((index / 4) * 8), 8.0f, 8.0f}; }

    // An entity with a Sprite that plays a clip. fps 30 at 60 ticks a second: a frame every 2 ticks,
    // and after an odd number of ticks it is in the middle of one (no rounding to worry about).
    kw::Entity animated(kw::World& world, const char* clips, const char* first)
    {
        const kw::Entity entity = world.create();

        world.add<kuge::Transform2D>(entity, kuge::Transform2D{});
        world.add<kuge::Sprite>(entity, kuge::Sprite{});
        world.add<kuge::Animator>(entity, kuge::Animator::of(makeSheet(world),
            std::make_shared<kuge::AnimationSet>(kuge::AnimationSet::parse(clips)), first));
        return entity;
    }
}

Test(animation, read_clips)
{
    const auto set = kuge::AnimationSet::parse(CLIPS);

    AssertEq(set.clips.size(), 3, "three clips");
    AssertEq(set.find("run"), 1, "found by name");
    AssertEq(set.find("nothing"), -1, "and not found");
    Assert(set.clips[0].frames == std::vector<int>({0, 1, 2, 3}), "a range");
    Assert(set.clips[1].frames == std::vector<int>({4, 5, 6, 7, 6, 5}), "a list");
    AssertEq(set.clips[0].fps, 6.0f, "fps");
    Assert(set.clips[0].loop && !set.clips[2].loop, "loops unless said otherwise");
    AssertStrEq(set.clips[2].next.c_str(), "idle", "what comes next");
    AssertEq(set.clips[2].cues.size(), 2, "two cues");
    Assert(set.clips[2].cues[0].frame == 0 && set.clips[2].cues[0].name == "swing", "in order of frame");
    Assert(set.clips[2].cues[1].frame == 2 && set.clips[2].cues[1].name == "hit", "the second");
    const auto mixed = kuge::AnimationSet::parse("clip a frames=1,3-4,9\n");
    Assert(mixed.clips[0].frames == std::vector<int>({1, 3, 4, 9}), "ranges and numbers together");
    AssertEq(mixed.clips[0].fps, 10.0f, "10 fps if not said");
}

Test(animation, mistakes_name_the_line)
{
    Assert(mentions(errorOf(""), "no clips"), "empty");
    Assert(mentions(errorOf("# nothing\n"), "no clips"), "only comments");
    Assert(mentions(errorOf("hello a frames=1\n"), "line 1"), "not a clip");
    Assert(mentions(errorOf("clip a\n"), "line 1"), "nothing after the name");
    Assert(mentions(errorOf("clip a fps=10\n"), "no frames"), "no frames");
    Assert(mentions(errorOf("clip a frames=1 fps=0\n"), "fps"), "fps 0");
    Assert(mentions(errorOf("clip a frames=1 fps=abc\n"), "fps"), "fps not a number");
    Assert(mentions(errorOf("clip a frames=5-2\n"), "'5-2'"), "a range backwards");
    Assert(mentions(errorOf("clip a frames=x\n"), "'x'"), "not a number");
    Assert(mentions(errorOf("clip a frames=-3\n"), "line 1"), "negative");
    Assert(mentions(errorOf("clip a frames=1 loop=maybe\n"), "loop"), "loop is true or false");
    Assert(mentions(errorOf("clip a frames=1 speed=2\n"), "'speed'"), "an unknown key");
    Assert(mentions(errorOf("clip a frames=1 oops\n"), "key=value"), "not key=value");
    Assert(mentions(errorOf("clip a frames=1\nclip a frames=2\n"), "line 2"), "a name used twice");
    Assert(mentions(errorOf("clip a frames=0-1 cue.5=x\n"), "cue.5"), "a cue after the end");
    Assert(mentions(errorOf("clip a frames=0-1 cue.x=y\n"), "cue"), "a cue without a frame number");
    Assert(mentions(errorOf("clip a frames=1 next=b\n"), "'b'"), "next: a clip that is not there");
    Assert(errorOf("clip a frames=1 next=b\n\nclip b frames=2\n").empty(), "but a clip that comes later is fine");
}

Test(animation, files)
{
    const auto path = tempPath("hero.anim");
    std::string message;

    {
        std::ofstream file(path);
        file << CLIPS;
    }
    AssertEq(kuge::AnimationSet::load(path).clips.size(), 3, "loaded");
    {
        std::ofstream file(path);
        file << "clip a frames=1\nbroken\n";
    }
    try { kuge::AnimationSet::load(path); } catch (const kuge::AnimationError& error) { message = error.what(); }
    Assert(mentions(message, "hero.anim") && mentions(message, "line 2"), "an error names the file and the line: '%s'", message.c_str());
    std::filesystem::remove(path);
    message.clear();
    try { kuge::AnimationSet::load(path); } catch (const kuge::AnimationError& error) { message = error.what(); }
    Assert(!message.empty(), "a missing file");
}

Test(animation, animator_plays_clips)
{
    auto set = std::make_shared<kuge::AnimationSet>(kuge::AnimationSet::parse(CLIPS));
    kuge::Animator animator = kuge::Animator::of(nullptr, set, "run");
    bool refused = false;

    AssertStrEq(std::string(animator.current()).c_str(), "run", "starts on the clip asked for");
    animator.time = 1.0f;
    animator.seen = 5;
    Assert(animator.play("run"), "playing the same clip...");
    AssertEq(animator.time, 1.0f, "...does not restart it");
    Assert(animator.play("run", true), "unless asked");
    AssertEq(animator.time, 0.0f, "back to the start");
    AssertEq(animator.seen, -1, "and the cues count again");
    Assert(animator.play("attack"), "another clip");
    AssertStrEq(std::string(animator.current()).c_str(), "attack", "is the current one");
    animator.finished = true;
    Assert(animator.play("attack"), "a clip that ended...");
    Assert(!animator.finished, "...plays again when asked for");
    Assert(!animator.play("nothing"), "no such clip");
    AssertStrEq(std::string(animator.current()).c_str(), "attack", "and nothing changed");
    try { kuge::Animator::of(nullptr, set, "nothing"); } catch (const kuge::AnimationError&) { refused = true; }
    Assert(refused, "of() with a clip that is not there");
    AssertEq(kuge::Animator{}.current().size(), 0, "no set, no clip");
}

Test(animation, a_loop_goes_round)
{
    kw::Entity e = 0;
    Fixture fx([&e](TestScene& scene) { e = animated(scene.world(), "clip walk fps=30 frames=0-3\n", "walk"); });
    auto& sprite = fx.scene->world().get<kuge::Sprite>(e);
    const int expected[] = {0, 1, 1, 2, 2, 3, 3, 0, 0, 1};   // after 1, 2, 3... ticks

    for (int tick = 0; tick < 10; ++tick) {
        fx.frame(1.0 / 60.0);
        if (tick % 2 == 0) {   // in the middle of a frame: no rounding to worry about
            Assert(sprite.source == cell(expected[tick]), "after %d ticks, cell %d: got x=%f y=%f", tick + 1, expected[tick], sprite.source.x, sprite.source.y);
        }
    }
    Assert(sprite.texture != nullptr, "the sprite got the picture of the sheet");
}

Test(animation, a_clip_that_ends_hands_over)
{
    kw::Entity e = 0;
    Fixture fx([&e](TestScene& scene) {
        e = animated(scene.world(), "clip hit fps=30 frames=4,5 loop=false next=idle\nclip idle fps=30 frames=0-3\n", "hit");
    });
    auto& sprite = fx.scene->world().get<kuge::Sprite>(e);
    auto& animator = fx.scene->world().get<kuge::Animator>(e);

    fx.frame(1.0 / 60.0);
    Assert(sprite.source == cell(4), "the first frame");
    fx.frame(1.0 / 60.0);
    fx.frame(1.0 / 60.0);
    Assert(sprite.source == cell(5) && animator.current() == "hit", "the second");
    fx.frame(1.0 / 60.0);
    fx.frame(1.0 / 60.0);
    Assert(animator.current() == "idle", "then it goes to the next clip");
    Assert(sprite.source == cell(0), "on its first frame");
}

Test(animation, a_clip_can_stay_at_the_end)
{
    kw::Entity e = 0;
    Fixture fx([&e](TestScene& scene) { e = animated(scene.world(), "clip die fps=30 frames=8-10 loop=false\n", "die"); });
    auto& sprite = fx.scene->world().get<kuge::Sprite>(e);
    auto& animator = fx.scene->world().get<kuge::Animator>(e);

    for (int i = 0; i < 40; ++i) {
        fx.frame(1.0 / 60.0);
    }
    Assert(sprite.source == cell(10), "it holds its last frame");
    Assert(animator.finished && animator.current() == "die", "and knows it ended");
    fx.scene->world().get<kuge::Animator>(e).play("die");
    fx.frame(1.0 / 60.0);
    Assert(sprite.source == cell(8) && !animator.finished, "asked again, it plays from the start");
}

Test(animation, speed_and_pause)
{
    kw::Entity e = 0;
    Fixture fx([&e](TestScene& scene) { e = animated(scene.world(), "clip walk fps=30 frames=0-11\n", "walk"); });
    auto& sprite = fx.scene->world().get<kuge::Sprite>(e);
    auto& animator = fx.scene->world().get<kuge::Animator>(e);

    animator.speed = 0.0f;
    for (int i = 0; i < 10; ++i) {
        fx.frame(1.0 / 60.0);
    }
    Assert(sprite.source == cell(0), "speed 0: frozen");
    animator.speed = 1.0f;
    animator.playing = false;
    for (int i = 0; i < 10; ++i) {
        fx.frame(1.0 / 60.0);
    }
    Assert(sprite.source == cell(0), "not playing: frozen too");
    animator.playing = true;
    animator.speed = 2.0f;
    for (int i = 0; i < 5; ++i) {
        fx.frame(1.0 / 60.0);
    }
    // 5 ticks at twice the speed: 10 ticks of a clip that runs at 30 fps = 5 frames, in the middle of frame 5
    Assert(sprite.source == cell(5), "twice as fast: got x=%f y=%f", sprite.source.x, sprite.source.y);
}

Test(animation, cues_fire_once)
{
    kw::Entity e = 0;
    std::vector<std::string> heard;
    Fixture fx([&e](TestScene& scene) {
        e = animated(scene.world(), "clip swing fps=30 frames=0-3 loop=false cue.1=hit cue.3=end\n", "swing");
    });

    for (int i = 0; i < 12; ++i) {
        fx.frame(1.0 / 60.0);
        for (const auto& event : fx.scene->world().getResource<kuge::AnimationEvents>().list) {
            Assert(event.entity == e, "for the entity that plays");
            heard.push_back(event.name);
        }
    }
    Assert(heard == std::vector<std::string>({"hit", "end"}), "each cue once, in order, got %zu", heard.size());
}

Test(animation, cues_repeat_with_loops)
{
    kw::Entity e = 0;
    int hits = 0;
    Fixture fx([&e](TestScene& scene) { e = animated(scene.world(), "clip spin fps=30 frames=0-1 cue.0=tick\n", "spin"); });

    for (int i = 0; i < 20; ++i) {
        fx.frame(1.0 / 60.0);
        hits += static_cast<int>(fx.scene->world().getResource<kuge::AnimationEvents>().list.size());
    }
    // 20 ticks = 10 frames = 5 loops of 2 frames: the cue of frame 0 fires at each loop
    Assert(hits >= 4 && hits <= 6, "once per loop: %d in 5 loops", hits);
}

Test(animation, no_cue_is_missed_at_speed)
{
    kw::Entity e = 0;
    Fixture fx([&e](TestScene& scene) {
        e = animated(scene.world(), "clip a fps=30 frames=0-5 loop=false cue.1=one cue.2=two cue.4=four\n", "a");
        scene.world().get<kuge::Animator>(e).speed = 40.0f;   // several frames in one tick
    });
    std::vector<std::string> heard;

    fx.frame(1.0 / 60.0);
    for (const auto& event : fx.scene->world().getResource<kuge::AnimationEvents>().list) {
        heard.push_back(event.name);
    }
    Assert(heard == std::vector<std::string>({"one", "two", "four"}), "frames jumped over still give their cues, in order (got %zu)", heard.size());
    fx.frame(1.0 / 60.0);
    AssertEq(fx.scene->world().getResource<kuge::AnimationEvents>().list.size(), 0, "and the events are those of the last tick");
}

Test(animation, events_by_entity)
{
    kw::Entity a = 0, b = 0;
    Fixture fx([&](TestScene& scene) {
        a = animated(scene.world(), "clip x fps=30 frames=0-1 cue.0=go\n", "x");
        b = animated(scene.world(), "clip x fps=30 frames=0-1 cue.0=go\n", "x");
    });

    fx.frame(1.0 / 60.0);
    const auto& list = fx.scene->world().getResource<kuge::AnimationEvents>().list;
    AssertEq(list.size(), 2, "one each");
    Assert(list[0].entity == a && list[1].entity == b, "always by increasing entity");
}

Test(animation, incomplete_animators_wait)
{
    Fixture fx([](TestScene& scene) {
        const kw::Entity e = scene.world().create();

        scene.world().add<kuge::Transform2D>(e, kuge::Transform2D{});
        scene.world().add<kuge::Sprite>(e, kuge::Sprite{});
        scene.world().add<kuge::Animator>(e, kuge::Animator{});       // no sheet, no clips
    });

    fx.frame(1.0 / 60.0);
    fx.frame(1.0 / 60.0);
    Assert(fx.scene->world().get<kuge::Sprite>(*fx.scene->world().view<kuge::Animator>().begin()).texture == nullptr,
        "nothing happens, and nothing breaks");
}

Test(animation, the_frame_is_drawn)
{
    kw::Entity e = 0;
    Fixture fx([&e](TestScene& scene) { e = animated(scene.world(), "clip a fps=30 frames=6\n", "a"); });

    fx.frame(1.0 / 60.0);
    const auto& calls = fx.dummy.renderer->lastFrame();

    AssertEq(calls.size(), 1, "one sprite");
    Assert(calls[0].kind == kuge::DummyRenderer::Call::Kind::Texture, "drawn with its texture");
    Assert(calls[0].texture.source == cell(6), "showing the cell of the frame");
    Assert(calls[0].destination.w == 8.0f && calls[0].destination.h == 8.0f, "at the size of a cell");
}
