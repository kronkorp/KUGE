extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Serializer.hpp"
#include "AssetManager.hpp"
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <unistd.h>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using namespace std::chrono_literals;

    struct Doc { std::string text; };

    struct Folder
    {
        std::filesystem::path path;

        explicit Folder(const char* name)
            : path(std::filesystem::temp_directory_path() / ("kuge_test_" + std::to_string(::getpid()) + "_" + name))
        {
            std::filesystem::remove_all(path);
            std::filesystem::create_directories(path);
        }
        ~Folder() { std::filesystem::remove_all(path); }

        // Writes a file, and gives it the date that is asked (so that tests do not wait for the clock)
        std::filesystem::path write(const char* name, const std::string& text, int seconds)
        {
            const auto file = path / name;

            kuge::writeFile(file, std::vector<std::uint8_t>(text.begin(), text.end()));
            std::filesystem::last_write_time(file, std::filesystem::file_time_type{} + std::chrono::seconds(1000 + seconds));
            return file;
        }
    };

    std::string readText(const std::filesystem::path& file)
    {
        const auto bytes = kuge::readFile(file);

        return std::string(bytes.begin(), bytes.end());
    }

    struct Docs
    {
        int loads = 0;
        int reloads = 0;
        kuge::AssetManager<Doc> manager{
            [this](const std::filesystem::path& file) { ++loads; return std::make_shared<Doc>(Doc{readText(file)}); },
            [this](Doc& doc, const std::filesystem::path& file) {
                ++reloads;
                const std::string text = readText(file);

                if (text.rfind("BAD", 0) == 0) {
                    throw std::runtime_error("bad content");
                }
                doc.text = text;
            }};
    };
}

Test(reload, nothing_changed)
{
    Folder folder("rl_none");
    Docs docs;
    const auto file = folder.write("a.txt", "one", 1);
    auto doc = docs.manager.load(file);

    Assert(docs.manager.reloadChanged().reloaded.empty(), "the file is as it was loaded");
    AssertEq(docs.reloads, 0, "the reloader was not called");
}

Test(reload, changed_in_place)
{
    Folder folder("rl_place");
    Docs docs;
    const auto file = folder.write("a.txt", "one", 1);
    auto doc = docs.manager.load(file);
    const Doc* address = doc.get();

    folder.write("a.txt", "two!", 2);
    const auto report = docs.manager.reloadChanged();

    AssertEq(report.reloaded.size(), 1, "one reloaded");
    Assert(report.reloaded[0] == file && report.failed.empty(), "the right one");
    AssertStrEq(doc->text.c_str(), "two!", "the holder sees the new content");
    Assert(doc.get() == address && docs.manager.load(file) == doc, "and it is the same object");
    AssertEq(docs.loads, 1, "no second load");
    AssertEq(docs.manager.reloadChanged().reloaded.size(), 0, "once: it is up to date now");
}

Test(reload, size_alone_is_seen)
{
    Folder folder("rl_size");
    Docs docs;
    const auto file = folder.write("a.txt", "one", 1);
    auto doc = docs.manager.load(file);

    folder.write("a.txt", "longer", 1);      // the same date
    AssertEq(docs.manager.reloadChanged().reloaded.size(), 1, "reloaded");
    AssertStrEq(doc->text.c_str(), "longer", "with its content");
}

Test(reload, date_alone_is_seen)
{
    Folder folder("rl_date");
    Docs docs;
    const auto file = folder.write("a.txt", "one", 1);
    auto doc = docs.manager.load(file);

    folder.write("a.txt", "two", 5);         // the same size
    AssertEq(docs.manager.reloadChanged().reloaded.size(), 1, "reloaded");
    AssertStrEq(doc->text.c_str(), "two", "with its content");
}

Test(reload, only_what_changed)
{
    Folder folder("rl_only");
    Docs docs;
    auto a = docs.manager.load(folder.write("a.txt", "a1", 1));
    auto b = docs.manager.load(folder.write("b.txt", "b1", 1));

    folder.write("b.txt", "b2", 2);
    const auto report = docs.manager.reloadChanged();

    AssertEq(report.reloaded.size(), 1, "one");
    AssertStrEq(a->text.c_str(), "a1", "a is untouched");
    AssertStrEq(b->text.c_str(), "b2", "b is new");
}

Test(reload, a_missing_file_is_left)
{
    Folder folder("rl_missing");
    Docs docs;
    const auto file = folder.write("a.txt", "one", 1);
    auto doc = docs.manager.load(file);

    std::filesystem::remove(file);
    Assert(docs.manager.reloadChanged().failed.empty() && docs.reloads == 0, "an editor may be about to write it: nothing is said");
    AssertStrEq(doc->text.c_str(), "one", "the asset stays");
    folder.write("a.txt", "back", 3);
    AssertEq(docs.manager.reloadChanged().reloaded.size(), 1, "and when the file is back, it is reloaded");
    AssertStrEq(doc->text.c_str(), "back", "with its content");
}

Test(reload, a_failure_keeps_the_asset)
{
    Folder folder("rl_fail");
    Docs docs;
    const auto file = folder.write("a.txt", "good", 1);
    auto doc = docs.manager.load(file);

    folder.write("a.txt", "BAD half-written", 2);
    const auto report = docs.manager.reloadChanged();

    AssertEq(report.reloaded.size(), 0, "not reloaded");
    AssertEq(report.failed.size(), 1, "and said");
    Assert(report.failed[0].first == file && report.failed[0].second == "bad content", "which file and why: '%s'", report.failed[0].second.c_str());
    AssertStrEq(doc->text.c_str(), "good", "the asset is as it was");
    AssertEq(docs.manager.reloadChanged().failed.size(), 0, "it is not tried again while the file is the same");
    AssertEq(docs.reloads, 1, "(the reloader ran once)");
    folder.write("a.txt", "fixed", 3);
    AssertEq(docs.manager.reloadChanged().reloaded.size(), 1, "but it is when the file changes");
    AssertStrEq(doc->text.c_str(), "fixed", "and then it works");
}

Test(reload, what_nobody_holds_is_left)
{
    Folder folder("rl_gone");
    Docs docs;
    const auto file = folder.write("a.txt", "one", 1);

    docs.manager.load(file).reset();
    folder.write("a.txt", "two", 2);
    AssertEq(docs.manager.reloadChanged().reloaded.size(), 0, "no one holds it: nothing to update");
    AssertEq(docs.reloads, 0, "the reloader did not run");
    AssertStrEq(docs.manager.load(file)->text.c_str(), "two", "the next load reads the new file");
}

Test(reload, no_reloader_no_reload)
{
    Folder folder("rl_noreloader");
    int loads = 0;
    kuge::AssetManager<Doc> manager([&loads](const std::filesystem::path& file) { ++loads; return std::make_shared<Doc>(Doc{readText(file)}); });
    const auto file = folder.write("a.txt", "one", 1);
    auto doc = manager.load(file);

    folder.write("a.txt", "two", 2);
    AssertEq(manager.reloadChanged().reloaded.size(), 0, "nothing without a reloader");
    AssertStrEq(doc->text.c_str(), "one", "the asset is as it was");
}

Test(reload, stamp_before_the_read)
{
    // A file that changes while it is being loaded must be seen as changed afterwards
    Folder folder("rl_race");
    const auto file = folder.write("a.txt", "one", 1);
    int reloads = 0;
    kuge::AssetManager<Doc> manager(
        [&](const std::filesystem::path& path) {
            auto doc = std::make_shared<Doc>(Doc{readText(path)});

            folder.write("a.txt", "two", 2);       // someone saves during the load
            return doc;
        },
        [&](Doc& doc, const std::filesystem::path& path) { ++reloads; doc.text = readText(path); });
    auto doc = manager.load(file);

    AssertStrEq(doc->text.c_str(), "one", "it read the old one");
    manager.reloadChanged();
    AssertEq(reloads, 1, "and it does not miss the new one");
    AssertStrEq(doc->text.c_str(), "two", "which it reads");
}
