extern "C" {
    #include "kronklab/kronklab.h"
}
#include "audio_helpers.hpp"
#include "audio/Audio.hpp"
#include "audio/DummyAudio.hpp"
#include "client_fixture.hpp"
#include <cmath>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    bool near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) < tolerance; }

    // A mixer on a dummy backend, and a sound that exists
    struct Studio
    {
        kuge::DummyAudio audio;
        kuge::Audio      mixer{audio};
        SoundFile        file{"studio"};
        kuge::Sound      sound{audio, audio.loadSound(file.path)};
        kuge::Music      music{audio, audio.loadMusic(file.path)};
    };
}

Test(audio, volumes_multiply)
{
    Studio studio;

    studio.mixer.setMasterVolume(0.5f);
    studio.mixer.setBusVolume(kuge::Bus::Effects, 0.5f);
    const auto voice = studio.mixer.play(studio.sound, {.volume = 0.8f});

    Assert(voice, "it plays");
    Assert(near(studio.audio.voice(voice.id)->volume, 0.2f), "master x bus x sound = 0.2, got %f", studio.audio.voice(voice.id)->volume);
    studio.mixer.setBusVolume(kuge::Bus::Music, 0.5f);
    studio.mixer.playMusic(studio.music, {.volume = 0.5f});
    Assert(near(studio.audio.music().volume, 0.125f), "music: 0.5 x 0.5 x 0.5, got %f", studio.audio.music().volume);
}

Test(audio, buses_change_what_plays)
{
    Studio studio;
    const auto voice = studio.mixer.play(studio.sound, {.volume = 0.8f, .pan = 0.5f});

    studio.mixer.playMusic(studio.music, {.volume = 1.0f});
    studio.mixer.setBusVolume(kuge::Bus::Effects, 0.25f);
    Assert(near(studio.audio.voice(voice.id)->volume, 0.2f), "a playing voice follows its bus");
    Assert(near(studio.audio.voice(voice.id)->pan, 0.5f), "and keeps its pan");
    Assert(near(studio.audio.music().volume, 1.0f), "the music does not");
    studio.mixer.setBusVolume(kuge::Bus::Music, 0.4f);
    Assert(near(studio.audio.music().volume, 0.4f), "the music follows the music bus");
    Assert(near(studio.audio.voice(voice.id)->volume, 0.2f), "and the voices do not");
    studio.mixer.setMasterVolume(0.0f);
    Assert(near(studio.audio.voice(voice.id)->volume, 0.0f) && near(studio.audio.music().volume, 0.0f), "the master volume reaches both");
    AssertEq(studio.mixer.masterVolume(), 0.0f, "readable");
    AssertEq(studio.mixer.busVolume(kuge::Bus::Music), 0.4f, "and so are the buses");
}

Test(audio, values_are_kept_in_range)
{
    Studio studio;
    const float nan = std::nanf("");

    studio.mixer.setMasterVolume(7.0f);
    AssertEq(studio.mixer.masterVolume(), 1.0f, "too high: 1");
    studio.mixer.setMasterVolume(-3.0f);
    AssertEq(studio.mixer.masterVolume(), 0.0f, "negative: 0");
    studio.mixer.setMasterVolume(nan);
    AssertEq(studio.mixer.masterVolume(), 0.0f, "NaN: 0, not a NaN volume for the speakers");
    studio.mixer.setMasterVolume(1.0f);
    auto voice = studio.mixer.play(studio.sound, {.volume = 5.0f, .pan = -9.0f});
    Assert(near(studio.audio.voice(voice.id)->volume, 1.0f) && near(studio.audio.voice(voice.id)->pan, -1.0f), "volume and pan are brought back");
    voice = studio.mixer.play(studio.sound, {.volume = nan, .pan = nan});
    Assert(near(studio.audio.voice(voice.id)->volume, 0.0f) && near(studio.audio.voice(voice.id)->pan, 0.0f), "NaN too");
}

Test(audio, sound_that_comes_from_a_place)
{
    const kuge::Listener listener{{100.0f, 50.0f}, 500.0f, 200.0f};
    const auto here = kuge::spatialize({100.0f, 50.0f}, listener);
    const auto half = kuge::spatialize({350.0f, 50.0f}, listener);
    const auto left = kuge::spatialize({0.0f, 50.0f}, listener);
    const auto far = kuge::spatialize({700.0f, 50.0f}, listener);

    Assert(near(here.volume, 1.0f) && near(here.pan, 0.0f), "on the spot: full, centered");
    Assert(near(half.volume, 0.5f) && near(half.pan, 1.0f), "250 of 500 away: half, and beyond the pan width: all on the right");
    Assert(near(left.pan, -0.5f), "100 to the left of a pan width of 200: half left, got %f", left.pan);
    Assert(near(far.volume, 0.0f), "beyond the range: not heard");
    Assert(near(kuge::spatialize({100.0f, 549.0f}, listener).volume, 0.002f, 1e-3f), "just inside: nearly nothing");
    Assert(kuge::spatialize({100.0f, 550.0f}, listener).volume == 0.0f, "at the range: nothing");
    Assert(near(kuge::spatialize({130.0f, 50.0f}, {{100.0f, 50.0f}, 500.0f, 0.0f}).pan, 0.0f), "no pan width: no pan, no division by 0");
    Assert(kuge::spatialize({0.0f, 0.0f}, {{0.0f, 0.0f}, 0.0f, 1.0f}).volume == 0.0f, "a listener that hears nothing");
}

Test(audio, playing_at_a_place)
{
    Studio studio;
    const kuge::Listener listener{{0.0f, 0.0f}, 400.0f, 100.0f};
    const auto near_ = studio.mixer.playAt(studio.sound, {200.0f, 0.0f}, listener, {.volume = 0.5f});

    Assert(near_, "in range");
    Assert(near(studio.audio.voice(near_.id)->volume, 0.25f) && near(studio.audio.voice(near_.id)->pan, 1.0f),
        "half the distance, half the volume asked, on the right: %f", studio.audio.voice(near_.id)->volume);
    const auto far_ = studio.mixer.playAt(studio.sound, {500.0f, 0.0f}, listener);

    Assert(!far_, "out of range: no voice");
    AssertEq(studio.audio.activeVoices(), 1, "and nothing was asked of the backend");
}

Test(audio, voices_end_and_stop)
{
    Studio studio;
    const auto a = studio.mixer.play(studio.sound);
    const auto b = studio.mixer.play(studio.sound, {.loop = true});

    Assert(studio.audio.voice(b.id)->loop && !studio.audio.voice(a.id)->loop, "loop is passed on");
    AssertEq(studio.mixer.voices(), 2, "two voices");
    studio.audio.finish(a.id);
    Assert(!studio.mixer.playing(a) && studio.mixer.playing(b), "one ended");
    AssertEq(studio.mixer.voices(), 2, "the mixer learns it at the next update");
    studio.mixer.update();
    AssertEq(studio.mixer.voices(), 1, "then it forgets it");
    studio.mixer.stop(b);
    Assert(!studio.mixer.playing(b), "stopped");
    AssertEq(studio.mixer.voices(), 0, "and forgotten at once");
    Assert(!studio.mixer.playing(kuge::Voice{}), "no voice does not play");
    studio.mixer.stop(kuge::Voice{});
    studio.mixer.setVoice(kuge::Voice{999}, 1.0f, 0.0f);
}

Test(audio, no_free_voice)
{
    Studio studio;

    studio.audio.setMaxVoices(2);
    Assert(studio.mixer.play(studio.sound) && studio.mixer.play(studio.sound), "two fit");
    Assert(!studio.mixer.play(studio.sound), "the third does not: an empty voice, not a crash");
    AssertEq(studio.mixer.voices(), 2, "and is not remembered");
}

Test(audio, a_voice_can_be_changed)
{
    Studio studio;
    const auto voice = studio.mixer.play(studio.sound, {.volume = 1.0f});

    studio.mixer.setBusVolume(kuge::Bus::Effects, 0.5f);
    studio.mixer.setVoice(voice, 0.4f, -0.5f);
    Assert(near(studio.audio.voice(voice.id)->volume, 0.2f) && near(studio.audio.voice(voice.id)->pan, -0.5f), "its own volume, then the bus");
    studio.mixer.setBusVolume(kuge::Bus::Effects, 1.0f);
    Assert(near(studio.audio.voice(voice.id)->volume, 0.4f), "and the new volume is kept when the bus changes again");
}

Test(audio, music_one_at_a_time)
{
    Studio studio;
    kuge::Music other(studio.audio, studio.audio.loadMusic(studio.file.path));

    Assert(!studio.mixer.musicPlaying(), "silence at first");
    studio.mixer.playMusic(studio.music, {.volume = 0.6f, .loop = false, .fadeIn = 2.0f});
    Assert(studio.mixer.musicPlaying() && studio.audio.music().music == studio.music.id(), "it plays");
    Assert(!studio.audio.music().loop && near(studio.audio.music().fadeIn, 2.0f), "with what was asked");
    studio.mixer.playMusic(other);
    Assert(studio.audio.music().music == other.id() && studio.audio.music().loop, "another one replaces it, and loops by default");
    studio.mixer.stopMusic(1.5f);
    Assert(!studio.mixer.musicPlaying() && near(studio.audio.music().fadeOut, 1.5f), "stopped, with a fade");
    studio.mixer.playMusic(other, {.fadeIn = -4.0f});
    Assert(near(studio.audio.music().fadeIn, 0.0f), "a negative fade is no fade");
    studio.mixer.stopMusic(-2.0f);
    Assert(near(studio.audio.music().fadeOut, 0.0f), "same when stopping");
}

Test(audio, sounds_are_shared_assets)
{
    SoundFile file("assets");
    auto dummy = kuge::makeDummyBackend();
    kuge::Engine engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed});
    auto& client = engine.addModule<kuge::ClientModule>(std::move(dummy.backend));

    auto a = client.sounds().load(file.path);
    auto b = client.sounds().load(file.path);
    Assert(a == b, "one file, one sound");
    AssertEq(dummy.audio->soundCount(), 1, "kept once by the backend");
    a.reset();
    b.reset();
    AssertEq(dummy.audio->soundCount(), 0, "and given back when nobody holds it");
    bool missing = false;
    try { client.sounds().load("/nonexistent/boom.wav"); } catch (const kuge::AudioError&) { missing = true; }
    Assert(missing, "a file that is not there is an error");
    auto music = client.musics().load(file.path);
    AssertEq(dummy.audio->musicCount(), 1, "musics too");
    music.reset();
}

Test(audio, scenes_get_the_mixer)
{
    SoundFile file("scene");
    kuge::DummyAudio* audio = nullptr;
    std::shared_ptr<kuge::Sound> sound;
    Fixture fx;

    audio = fx.dummy.audio;
    Assert(&fx.scene->world().getResource<kuge::Ref<kuge::Audio>>().get() == &fx.client->audio(), "Ref<Audio>");
    Assert(&fx.scene->world().getResource<kuge::Ref<kuge::AssetManager<kuge::Sound>>>().get() == &fx.client->sounds(), "sounds");
    Assert(&fx.scene->world().getResource<kuge::Ref<kuge::AssetManager<kuge::Music>>>().get() == &fx.client->musics(), "musics");
    sound = fx.client->sounds().load(file.path);
    const auto voice = fx.client->audio().play(*sound);

    AssertEq(audio->activeVoices(), 1, "a system can play a sound");
    audio->finish(voice.id);
    fx.frame();
    AssertEq(fx.client->audio().voices(), 0, "and the client forgets the voices that ended, at each loop");
    sound.reset();
}

Test(audio, no_sound_no_problem)
{
    SoundFile file("silent");
    auto dummy = kuge::makeDummyBackend();
    kuge::Engine engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed});

    dummy.backend.audio.reset();                 // a machine with no sound card
    auto& client = engine.addModule<kuge::ClientModule>(std::move(dummy.backend));
    auto sound = client.sounds().load(file.path);

    Assert(client.audio().play(*sound), "the game plays sounds that nobody hears");
    client.audio().setMasterVolume(0.5f);
    engine.scenes().change<TestScene>();
    Assert(engine.step(1.0 / 60.0), "and goes on");
    sound.reset();
}
