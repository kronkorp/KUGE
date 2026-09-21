extern "C" {
    #include "kronklab/kronklab.h"
}
#include "ConfigFile.hpp"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unistd.h>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    std::filesystem::path tempPath(const char* name)
    {
        return std::filesystem::temp_directory_path()
            / ("kuge_test_" + std::to_string(::getpid()) + "_" + name);
    }

    // The message of the ConfigError that action throws, "" if it does not throw
    template<typename F>
    std::string errorOf(F&& action)
    {
        try {
            action();
        } catch (const kuge::ConfigError& e) {
            return e.what();
        }
        return {};
    }
}

Test(configfile, keys_and_sections)
{
    kuge::ConfigFile config;

    config.parse("name = KUGE\n[audio]\nvolume = 0.8\n[video]\nwidth=1280\nnested.key = x\n");
    AssertStrEq(config.getString("name").c_str(), "KUGE", "a key without section");
    AssertStrEq(config.getString("audio.volume").c_str(), "0.8", "a key of a section");
    AssertEq(config.getInt("video.width", 0), 1280, "no spaces around =");
    AssertStrEq(config.getString("video.nested.key").c_str(), "x", "dots are kept");
    AssertEq(config.has("volume"), false, "the section is part of the key");
}

Test(configfile, comments_and_blank_lines)
{
    kuge::ConfigFile config;

    config.parse("# a comment\n\n; another\n   \nkey = value # not a comment\r\nother = 1\r\n");
    AssertEq(config.entries().size(), 2, "only 2 keys, got %zu", config.entries().size());
    AssertStrEq(config.getString("key").c_str(), "value # not a comment", "the value is the rest of the line");
    AssertEq(config.getInt("other", 0), 1, "CRLF line endings are fine");
}

Test(configfile, typed_getters)
{
    kuge::ConfigFile config;

    config.parse("i = 42\nspaced =   7  \nbad = 4x\nfloat = 1.5\nd = 1e3\nyes = YES\noff = Off\nmaybe = maybe\n");
    AssertEq(config.getInt("i", -1), 42, "int");
    AssertEq(config.getInt("spaced", -1), 7, "trimmed");
    AssertEq(config.getInt("bad", -1), -1, "unreadable gives the fallback");
    AssertEq(config.getInt("float", -1), -1, "1.5 is not an int");
    AssertEq(config.getInt("missing", -1), -1, "missing gives the fallback");
    AssertEq(config.getDouble("d", 0.0), 1000.0, "double");
    AssertEq(config.getDouble("float", 0.0), 1.5, "double with a fraction");
    Assert(config.getBool("yes", false), "YES");
    AssertEq(config.getBool("off", true), false, "Off");
    Assert(config.getBool("maybe", true), "unreadable bool gives the fallback");
    AssertEq(config.getBool("maybe", false), false, "whatever the fallback is");
    AssertStrEq(config.getString("missing", "def").c_str(), "def", "string fallback");
}

Test(configfile, set_overloads)
{
    kuge::ConfigFile config;

    config.set("text", "literal");
    config.set("int", 42);
    config.set("neg", -7);
    config.set("real", 0.1);
    config.set("flag", true);
    AssertStrEq(config.getString("text").c_str(), "literal", "a string literal is text, not a bool");
    AssertEq(config.getInt("int", 0), 42, "int");
    AssertEq(config.getInt("neg", 0), -7, "negative int");
    AssertEq(config.getDouble("real", 0.0), 0.1, "double");
    AssertStrEq(config.getString("flag").c_str(), "true", "bool");
    Assert(config.getBool("flag", false), "read back");
}

Test(configfile, text_round_trip)
{
    kuge::ConfigFile config;
    kuge::ConfigFile back;

    config.set("pi", 3.141592653589793);
    config.set("tiny", 1e-7);
    config.set("third", 1.0 / 3.0);
    config.set("zeta", "last");
    config.set("alpha", "first");
    back.parse(config.toString());
    AssertEq(back.getDouble("pi", 0.0), 3.141592653589793, "pi keeps every digit");
    AssertEq(back.getDouble("tiny", 0.0), 1e-7, "tiny");
    AssertEq(back.getDouble("third", 0.0), 1.0 / 3.0, "a third");
    AssertStrEq(back.toString().c_str(), config.toString().c_str(), "same text after a round trip");
    Assert(config.toString().find("alpha") < config.toString().find("zeta"), "alphabetical order");
}

Test(configfile, bad_lines_say_which)
{
    kuge::ConfigFile config;
    const std::string message = errorOf([&] { config.parse("a = 1\nthis is wrong\nb = 2\n"); });

    Assert(message.find("line 2") != std::string::npos, "the message names the line: '%s'", message.c_str());
    AssertEq(errorOf([&] { config.parse("[oops\n"); }).empty(), false, "unterminated section");
    AssertEq(errorOf([&] { config.parse("[]\n"); }).empty(), false, "empty section");
    AssertEq(errorOf([&] { config.parse("= value\n"); }).empty(), false, "empty key");
    AssertEq(errorOf([&] { config.parse("two words = 1\n"); }).empty(), false, "key with a space");
}

Test(configfile, invalid_keys_are_refused)
{
    kuge::ConfigFile config;

    AssertEq(errorOf([&] { config.set("", "x"); }).empty(), false, "empty key");
    AssertEq(errorOf([&] { config.set("a b", "x"); }).empty(), false, "space in a key");
    AssertEq(errorOf([&] { config.set("a=b", "x"); }).empty(), false, "= in a key");
    AssertEq(errorOf([&] { config.set("#a", "x"); }).empty(), false, "comment as a key");
    AssertEq(errorOf([&] { config.set("[a", "x"); }).empty(), false, "section as a key");
    AssertEq(errorOf([&] { config.set("a", "x\ny"); }).empty(), false, "line break in a value");
    AssertEq(config.entries().size(), 0, "nothing was added");
}

Test(configfile, parse_merges)
{
    kuge::ConfigFile config;

    config.parse("a = 1\nb = 2\n");
    config.parse("b = 3\nc = 4\n");
    AssertEq(config.getInt("a", 0), 1, "kept");
    AssertEq(config.getInt("b", 0), 3, "replaced");
    AssertEq(config.getInt("c", 0), 4, "added");
    Assert(config.remove("a"), "removed");
    AssertEq(config.remove("a"), false, "not there anymore");
    AssertEq(config.has("a"), false, "gone");
}

Test(configfile, files)
{
    const auto path = tempPath("config.cfg");
    kuge::ConfigFile config;
    kuge::ConfigFile back;

    AssertEq(back.load(path), false, "a missing file is not an error");
    config.set("volume", 0.5);
    config.set("name", "player one");
    config.save(path);
    AssertEq(std::filesystem::exists(path.string() + ".tmp"), false, "no temporary file left");
    Assert(back.load(path), "loaded");
    AssertEq(back.getDouble("volume", 0.0), 0.5, "volume");
    AssertStrEq(back.getString("name").c_str(), "player one", "a value with a space");

    {
        std::ofstream broken(path);
        broken << "not a config\n";
    }
    AssertEq(errorOf([&] { back.load(path); }).empty(), false, "an invalid file is an error");
    AssertEq(back.getDouble("volume", 0.0), 0.5, "and the settings are not lost");
    std::filesystem::remove(path);

    AssertEq(errorOf([&] { config.save(tempPath("no_such_dir") / "x.cfg"); }).empty(), false, "cannot save");
}
