extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Save.hpp"
#include "Serializer.hpp"
#include "UserDirectory.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    // A folder of its own, that goes when the test does
    struct Folder
    {
        std::filesystem::path path;

        explicit Folder(const char* name)
            : path(std::filesystem::temp_directory_path() / ("kuge_test_" + std::to_string(::getpid()) + "_" + name))
        {
            std::filesystem::remove_all(path);
        }
        ~Folder() { std::filesystem::remove_all(path); }
    };

    kuge::SaveSlots slotsIn(const Folder& folder, const char* game = "testgame", std::uint16_t version = 3)
    {
        return kuge::SaveSlots(folder.path, game, version, [] { return std::uint64_t{1700000000}; });
    }

    std::vector<std::uint8_t> bytesOf(const char* text)
    {
        return {text, text + std::char_traits<char>::length(text)};
    }

    // What went wrong when action fails, or nothing
    template<typename F>
    std::optional<kuge::SaveError::Reason> reasonOf(F&& action)
    {
        try {
            action();
        } catch (const kuge::SaveError& error) {
            return error.reason();
        }
        return std::nullopt;
    }

    using Reason = kuge::SaveError::Reason;
}

Test(saves, write_and_read)
{
    Folder folder("saves_basic");
    auto saves = slotsIn(folder);
    const auto payload = bytesOf("the state of a game");

    saves.write("slot1", "Level 3 - 12:04", payload);
    const auto data = saves.read("slot1");

    Assert(data.payload == payload, "the same bytes");
    AssertStrEq(data.info.slot.c_str(), "slot1", "slot");
    AssertStrEq(data.info.label.c_str(), "Level 3 - 12:04", "label");
    AssertEq(data.info.version, 3, "version");
    AssertEq(data.info.savedAt, 1700000000, "when");
    AssertEq(data.info.size, payload.size(), "size");
    Assert(saves.exists("slot1") && !saves.exists("slot2"), "exists");
}

Test(saves, an_empty_payload_is_fine)
{
    Folder folder("saves_empty");
    auto saves = slotsIn(folder);

    saves.write("nothing", "", {});
    const auto data = saves.read("nothing");

    AssertEq(data.payload.size(), 0, "no data");
    AssertEq(data.info.label.size(), 0, "no label");
}

Test(saves, a_big_save)
{
    Folder folder("saves_big");
    auto saves = slotsIn(folder);
    std::vector<std::uint8_t> big(2 * 1024 * 1024);

    for (std::size_t i = 0; i < big.size(); ++i) {
        big[i] = static_cast<std::uint8_t>(i * 31 + i / 7);
    }
    saves.write("big", "2 MiB", big);
    Assert(saves.read("big").payload == big, "2 MiB come back");
}

Test(saves, overwriting_keeps_one)
{
    Folder folder("saves_over");
    auto saves = slotsIn(folder);

    saves.write("a", "first", bytesOf("one"));
    saves.write("a", "second", bytesOf("two"));
    AssertStrEq(saves.read("a").info.label.c_str(), "second", "the new one");
    AssertEq(saves.list().size(), 1, "still one slot");
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder.path)) {
        (void)entry;
        ++files;
    }
    AssertEq(files, 1, "and no temporary file left, got %zu files", files);
}

Test(saves, the_folder_is_made)
{
    Folder folder("saves_made");
    auto saves = kuge::SaveSlots(folder.path / "a" / "b", "testgame", 1);

    saves.write("x", "", bytesOf("data"));
    Assert(std::filesystem::is_directory(folder.path / "a" / "b"), "nested folders are created");
    Assert(saves.read("x").info.savedAt > 1600000000, "the real clock by default");
}

Test(saves, list_for_a_menu)
{
    Folder folder("saves_list");
    auto saves = slotsIn(folder);

    saves.write("b", "second", bytesOf("2"));
    saves.write("a", "first", bytesOf("1"));
    saves.write("c", "third", bytesOf("3"));
    {
        std::ofstream not_a_save(folder.path / "notes.txt");
        not_a_save << "not a save";
    }
    auto bytes = kuge::readFile(folder.path / "c.ksave");
    bytes[bytes.size() / 2] ^= 0xFF;
    kuge::writeFile(folder.path / "c.ksave", bytes);

    const auto list = saves.list();
    AssertEq(list.size(), 3, "three saves (the text file is not one)");
    AssertStrEq(list[0].slot.c_str(), "a", "by name");
    AssertStrEq(list[2].slot.c_str(), "c", "the damaged one too");
    Assert(list[0].info && list[0].info->label == "first", "with what a menu shows");
    Assert(!list[2].info && !list[2].problem.empty(), "the damaged one says so: '%s'", list[2].problem.c_str());
    AssertEq(kuge::SaveSlots(folder.path / "nothing", "testgame", 1).list().size(), 0, "no folder, no saves");
}

Test(saves, no_change_goes_unseen)
{
    Folder folder("saves_flip");
    auto saves = slotsIn(folder);

    saves.write("s", "label", bytesOf("some data to protect"));
    const auto good = kuge::readFile(folder.path / "s.ksave");
    int refused = 0;

    // Whatever byte changes, the save is refused (never read as if it were good)
    for (std::size_t i = 0; i < good.size(); ++i) {
        auto bad = good;

        bad[i] ^= 0x01;
        kuge::writeFile(folder.path / "s.ksave", bad);
        const auto why = reasonOf([&] { saves.read("s"); });

        if (why == Reason::Corrupt) {
            ++refused;
        }
    }
    AssertEq(refused, static_cast<int>(good.size()), "each of the %zu bytes is protected, %d refused", good.size(), refused);
}

Test(saves, a_cut_file_is_refused)
{
    Folder folder("saves_cut");
    auto saves = slotsIn(folder);

    saves.write("s", "label", bytesOf("some data"));
    const auto good = kuge::readFile(folder.path / "s.ksave");
    int refused = 0;

    for (std::size_t length = 0; length < good.size(); ++length) {
        kuge::writeFile(folder.path / "s.ksave", std::span<const std::uint8_t>(good.data(), length));
        if (reasonOf([&] { saves.read("s"); }) == Reason::Corrupt) {
            ++refused;
        }
    }
    AssertEq(refused, static_cast<int>(good.size()), "every shorter file is refused");
    {
        std::ofstream garbage(folder.path / "g.ksave");
        garbage << "this is not a save at all, but it is long enough to have a header";
    }
    Assert(reasonOf([&] { saves.read("g"); }) == Reason::Corrupt, "a file that is not a save");
}

Test(saves, another_game)
{
    Folder folder("saves_game");
    auto mine = slotsIn(folder, "mygame");
    auto other = slotsIn(folder, "othergame");

    mine.write("s", "", bytesOf("x"));
    Assert(reasonOf([&] { other.read("s"); }) == Reason::WrongGame, "the name of the game is checked");
    Assert(!reasonOf([&] { mine.read("s"); }), "and its own game reads it");
}

Test(saves, versions)
{
    Folder folder("saves_version");
    auto old = slotsIn(folder, "g", 2);
    auto current = slotsIn(folder, "g", 3);
    auto future = slotsIn(folder, "g", 5);

    old.write("old", "", bytesOf("v2"));
    future.write("new", "", bytesOf("v5"));
    Assert(!reasonOf([&] { current.read("old"); }), "an older save is read...");
    AssertEq(current.read("old").info.version, 2, "...and says which version it is, to convert it");
    Assert(reasonOf([&] { current.read("new"); }) == Reason::TooNew, "a newer one is refused");
    Assert(!reasonOf([&] { future.read("old"); }), "the newer game reads the older save");
}

Test(saves, missing_and_removed)
{
    Folder folder("saves_missing");
    auto saves = slotsIn(folder);

    Assert(reasonOf([&] { saves.read("nothing"); }) == Reason::Missing, "no such slot");
    saves.write("s", "", bytesOf("x"));
    Assert(saves.remove("s"), "removed");
    Assert(!saves.remove("s"), "and it is not there any more");
    Assert(!saves.exists("s"), "gone");
}

Test(saves, slot_names)
{
    Folder folder("saves_names");
    auto saves = slotsIn(folder);
    const std::string tooLong(65, 'a');

    for (const char* bad : {"", ".", "..", "../evil", "a/b", "a b", "a.b", "é"}) {
        Assert(reasonOf([&] { saves.write(bad, "", {}); }) == Reason::BadName, "'%s' is refused", bad);
    }
    Assert(reasonOf([&] { saves.read(tooLong); }) == Reason::BadName, "65 characters is too long");
    Assert(reasonOf([&] { saves.exists("x/y"); }) == Reason::BadName, "exists too");
    Assert(!reasonOf([&] { saves.write("Slot_1-b", "", {}); }), "letters, digits, _ and - are fine");
    Assert(!reasonOf([&] { saves.write(std::string(64, 'a'), "", {}); }), "64 characters is fine");
}

Test(saves, the_disk_can_refuse)
{
    Folder folder("saves_io");
    std::filesystem::create_directories(folder.path);
    {
        std::ofstream file(folder.path / "blocked");
        file << "a file where a folder should be";
    }
    auto saves = kuge::SaveSlots(folder.path / "blocked", "g", 1);

    Assert(reasonOf([&] { saves.write("s", "", bytesOf("x")); }) == Reason::Io, "an I/O error, not a crash");
}

Test(saves, checksum_is_crc32)
{
    const auto bytes = bytesOf("123456789");

    AssertEq(kuge::checksum(bytes), 0xCBF43926u, "the standard check value of CRC-32");
    AssertEq(kuge::checksum({}), 0u, "of nothing");
}

Test(userdir, follows_the_xdg_variables)
{
    Folder folder("userdir");
    const std::string config = (folder.path / "cfg").string();
    const std::string data = (folder.path / "dat").string();
    const char* oldConfig = std::getenv("XDG_CONFIG_HOME");
    const char* oldData = std::getenv("XDG_DATA_HOME");
    const std::string keepConfig = oldConfig ? oldConfig : "";
    const std::string keepData = oldData ? oldData : "";
    bool refused = true;

    setenv("XDG_CONFIG_HOME", config.c_str(), 1);
    setenv("XDG_DATA_HOME", data.c_str(), 1);
    const auto a = kuge::userDirectory(kuge::UserDir::Config, "mygame");
    const auto b = kuge::userDirectory(kuge::UserDir::Data, "mygame");
    Assert(a == folder.path / "cfg" / "mygame", "settings: %s", a.c_str());
    Assert(b == folder.path / "dat" / "mygame", "saves: %s", b.c_str());
    Assert(std::filesystem::is_directory(a) && std::filesystem::is_directory(b), "and they exist");
    for (const char* bad : {"", ".", "..", "a/b", "a\\b"}) {
        try { kuge::userDirectory(kuge::UserDir::Data, bad); refused = false; } catch (const std::invalid_argument&) {}
    }
    Assert(refused, "a game name is a single folder name");

    setenv("XDG_DATA_HOME", "", 1);
    setenv("HOME", folder.path.c_str(), 1);
    Assert(kuge::userDirectory(kuge::UserDir::Data, "g") == folder.path / ".local/share" / "g", "without XDG: under HOME");
    if (oldConfig) { setenv("XDG_CONFIG_HOME", keepConfig.c_str(), 1); } else { unsetenv("XDG_CONFIG_HOME"); }
    if (oldData) { setenv("XDG_DATA_HOME", keepData.c_str(), 1); } else { unsetenv("XDG_DATA_HOME"); }
}
