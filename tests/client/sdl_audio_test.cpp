extern "C" {
    #include "kronklab/kronklab.h"
}
#include "audio_helpers.hpp"
#include "ClientModule.hpp"
#include "Engine.hpp"
#include "backend/SdlBackend.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <thread>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    // No sound card needed: SDL's own dummy audio driver, which plays in real time
    void useNoSoundCard(void)
    {
        setenv("SDL_VIDEODRIVER", "dummy", 1);
        setenv("SDL_RENDER_DRIVER", "software", 1);
        setenv("SDL_AUDIODRIVER", "dummy", 1);
    }

    void wait(int milliseconds)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
}

Test(sdlaudio, a_sound_plays_and_ends)
{
    useNoSoundCard();
    SoundFile file("sdl_short", 0.1);
    auto audio = kuge::makeSdlAudio();
    const kuge::SoundId sound = audio->loadSound(file.path);
    const kuge::VoiceId voice = audio->play(sound, 1.0f, 0.0f, false);

    Assert(sound != kuge::NO_SOUND && voice != kuge::NO_VOICE, "loaded, and started");
    Assert(audio->playing(voice), "it plays");
    wait(400);
    Assert(!audio->playing(voice), "and ends by itself after 0.1 s");
    audio->destroySound(sound);
}

Test(sdlaudio, a_voice_can_be_stopped)
{
    useNoSoundCard();
    SoundFile file("sdl_stop", 2.0);
    auto audio = kuge::makeSdlAudio();
    const kuge::SoundId sound = audio->loadSound(file.path);
    const kuge::VoiceId voice = audio->play(sound, 1.0f, 0.0f, false);

    Assert(audio->playing(voice), "playing");
    audio->stop(voice);
    Assert(!audio->playing(voice), "stopped");
    audio->stop(voice);                                   // twice: nothing happens
    audio->stop(kuge::NO_VOICE);
    audio->setVoice(voice, 0.5f, 0.5f);                   // on a voice that is gone: nothing happens
    Assert(!audio->playing(kuge::NO_VOICE), "no voice is not playing");
    audio->destroySound(sound);
}

Test(sdlaudio, loops_go_on)
{
    useNoSoundCard();
    SoundFile file("sdl_loop", 0.05);
    auto audio = kuge::makeSdlAudio();
    const kuge::SoundId sound = audio->loadSound(file.path);
    const kuge::VoiceId voice = audio->play(sound, 1.0f, 0.0f, true);

    wait(400);
    Assert(audio->playing(voice), "a loop of 0.05 s still plays after 0.4 s");
    audio->stop(voice);
    audio->destroySound(sound);
}

Test(sdlaudio, an_old_voice_is_not_a_new_one)
{
    useNoSoundCard();
    SoundFile file("sdl_reuse", 2.0);
    auto audio = kuge::makeSdlAudio();
    const kuge::SoundId sound = audio->loadSound(file.path);
    const kuge::VoiceId first = audio->play(sound, 1.0f, 0.0f, false);

    audio->stop(first);
    const kuge::VoiceId second = audio->play(sound, 1.0f, 0.0f, false);   // the same channel
    Assert(first != second, "another number");
    Assert(!audio->playing(first), "the old voice does not seem to play because its channel is used again");
    Assert(audio->playing(second), "the new one does");
    audio->stop(second);
    audio->destroySound(sound);
}

Test(sdlaudio, all_voices_busy)
{
    useNoSoundCard();
    SoundFile file("sdl_busy", 2.0);
    auto audio = kuge::makeSdlAudio();
    const kuge::SoundId sound = audio->loadSound(file.path);
    std::vector<kuge::VoiceId> voices;
    kuge::VoiceId last = 1;

    for (int i = 0; i < 40 && last != kuge::NO_VOICE; ++i) {
        last = audio->play(sound, 0.1f, 0.0f, true);
        if (last != kuge::NO_VOICE) {
            voices.push_back(last);
        }
    }
    AssertEq(voices.size(), 32, "32 at once, then none: an empty voice, not a crash (got %zu)", voices.size());
    for (kuge::VoiceId voice : voices) {
        audio->stop(voice);
    }
    audio->destroySound(sound);
}

Test(sdlaudio, volume_and_pan_ends)
{
    useNoSoundCard();
    SoundFile file("sdl_pan", 1.0);
    auto audio = kuge::makeSdlAudio();
    const kuge::SoundId sound = audio->loadSound(file.path);

    for (float pan : {-3.0f, -1.0f, 0.0f, 1.0f, 3.0f}) {
        for (float volume : {-1.0f, 0.0f, 0.5f, 1.0f, 4.0f}) {
            const kuge::VoiceId voice = audio->play(sound, volume, pan, false);

            Assert(voice != kuge::NO_VOICE, "starts at volume %f, pan %f", volume, pan);
            audio->setVoice(voice, 1.0f - volume, -pan);
            audio->stop(voice);
        }
    }
    audio->destroySound(sound);
}

Test(sdlaudio, a_sound_can_go_while_it_plays)
{
    useNoSoundCard();
    SoundFile file("sdl_destroy", 2.0);
    auto audio = kuge::makeSdlAudio();
    const kuge::SoundId sound = audio->loadSound(file.path);
    const kuge::VoiceId voice = audio->play(sound, 1.0f, 0.0f, true);

    audio->destroySound(sound);
    Assert(!audio->playing(voice), "the voice is stopped before the sound is freed");
    audio->destroySound(sound);                           // twice: nothing happens
    Assert(audio->play(sound, 1.0f, 0.0f, false) == kuge::NO_VOICE, "and a sound that is gone cannot be played");
}

Test(sdlaudio, bad_files_are_errors)
{
    useNoSoundCard();
    auto audio = kuge::makeSdlAudio();
    const auto garbage = std::filesystem::temp_directory_path() / ("kuge_test_" + std::to_string(::getpid()) + "_garbage.wav");
    bool missing = false, notSound = false, missingMusic = false;

    {
        std::ofstream file(garbage);
        file << "this is not a sound";
    }
    try { audio->loadSound("/nonexistent/x.wav"); } catch (const kuge::AudioError&) { missing = true; }
    try { audio->loadSound(garbage); } catch (const kuge::AudioError&) { notSound = true; }
    try { audio->loadMusic("/nonexistent/x.ogg"); } catch (const kuge::AudioError&) { missingMusic = true; }
    std::filesystem::remove(garbage);
    Assert(missing, "a file that is not there");
    Assert(notSound, "a file that is not a sound");
    Assert(missingMusic, "a music that is not there");
}

Test(sdlaudio, music)
{
    useNoSoundCard();
    SoundFile file("sdl_music", 2.0);
    auto audio = kuge::makeSdlAudio();
    const kuge::MusicId music = audio->loadMusic(file.path);

    Assert(!audio->musicPlaying(), "silence at first");
    audio->playMusic(music, 0.5f, true, 0.0f);
    Assert(audio->musicPlaying(), "it plays");
    audio->setMusicVolume(0.2f);
    audio->stopMusic(0.0f);
    Assert(!audio->musicPlaying(), "stopped at once");
    audio->playMusic(music, 1.0f, false, 0.05f);
    Assert(audio->musicPlaying(), "with a fade in");
    audio->destroyMusic(music);
    Assert(!audio->musicPlaying(), "a music that goes is stopped first");
    audio->playMusic(music, 1.0f, true, 0.0f);            // gone: nothing happens
    Assert(!audio->musicPlaying(), "and cannot be played");
}

Test(sdlaudio, through_the_client)
{
    useNoSoundCard();
    SoundFile file("sdl_client", 1.0);
    kuge::WindowConfig window;

    window.width = 100;
    window.height = 100;
    window.vsync = false;
    kuge::Engine engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed});
    auto backend = kuge::makeSdlBackend(window);

    Assert(backend.audio != nullptr, "the SDL backend brings its sound");
    auto& client = engine.addModule<kuge::ClientModule>(std::move(backend));
    auto sound = client.sounds().load(file.path);
    const auto voice = client.audio().play(*sound);

    Assert(voice && client.audio().playing(voice), "played through the mixer, by SDL_mixer");
    client.audio().stop(voice);
    sound.reset();
}

Test(sdlaudio, a_machine_with_no_sound)
{
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_RENDER_DRIVER", "software", 1);
    setenv("SDL_AUDIODRIVER", "no_such_driver", 1);
    bool refused = false;
    kuge::WindowConfig window;

    window.width = 100;
    window.height = 100;
    window.vsync = false;
    try { kuge::makeSdlAudio(); } catch (const kuge::AudioError&) { refused = true; }
    Assert(refused, "no audio device: an error that says so");
    auto backend = kuge::makeSdlBackend(window);
    Assert(backend.audio == nullptr && backend.window && backend.renderer, "the backend comes with everything but the sound");
    kuge::Engine engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed});
    auto& client = engine.addModule<kuge::ClientModule>(std::move(backend));

    SoundFile file("sdl_none", 0.1);
    auto sound = client.sounds().load(file.path);
    Assert(client.audio().play(*sound), "and the game plays sounds that nobody hears, without an error");
    sound.reset();
    setenv("SDL_AUDIODRIVER", "dummy", 1);
}
