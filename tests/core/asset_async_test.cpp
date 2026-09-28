extern "C" {
    #include "kronklab/kronklab.h"
}
#include "AssetManager.hpp"
#include "Serializer.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <set>
#include <thread>
#include <unistd.h>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    struct Doc { std::string text; };
    using Ticket = kuge::AssetManager<Doc>::Ticket;

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

        std::filesystem::path write(const std::string& name, const std::string& text)
        {
            const auto file = path / name;

            kuge::writeFile(file, std::vector<std::uint8_t>(text.begin(), text.end()));
            return file;
        }
    };

    std::string readText(const std::filesystem::path& file)
    {
        const auto bytes = kuge::readFile(file);

        return std::string(bytes.begin(), bytes.end());
    }

    // Docs whose two halves note the thread they run on
    struct Docs
    {
        std::mutex                 mutex;
        std::set<std::thread::id>  preparedOn, finishedOn;
        std::atomic<int>           prepares{0}, finishes{0};
        std::atomic<bool>          hold{false};   // a prepare waits while it is true
        kuge::ThreadPool           pool{2};
        kuge::AssetManager<Doc>    manager{[this](const std::filesystem::path& file) { return std::make_shared<Doc>(Doc{readText(file)}); }};

        Docs(void)
        {
            manager.enableAsync(
                [this]() -> kuge::ThreadPool& { return pool; },
                [this](const std::filesystem::path& file) -> std::any {
                    ++prepares;
                    {
                        std::lock_guard lock(mutex);
                        preparedOn.insert(std::this_thread::get_id());
                    }
                    while (hold) { std::this_thread::yield(); }
                    return readText(file);
                },
                [this](std::any&& data, const std::filesystem::path&) {
                    ++finishes;
                    {
                        std::lock_guard lock(mutex);
                        finishedOn.insert(std::this_thread::get_id());
                    }
                    return std::make_shared<Doc>(Doc{std::any_cast<std::string>(std::move(data))});
                });
        }

        // Pumps until n loads were finished
        bool pumpUntil(std::size_t n, double seconds = 20.0)
        {
            const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
            std::size_t done = 0;

            while (done < n) {
                done += manager.pump();
                if (std::chrono::steady_clock::now() > end) {
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return true;
        }
    };
}

Test(async, halves_on_two_threads)
{
    Folder folder("as_threads");
    Docs docs;
    const auto ticket = docs.manager.loadAsync(folder.write("a.txt", "hello"));

    Assert(ticket->state() == Ticket::State::Loading, "not ready at once: only pump() turns it");
    AssertEq(docs.finishes.load(), 0, "and nothing is made until pump()");
    Assert(docs.pumpUntil(1), "it is finished by a pump");
    Assert(ticket->done() && ticket->asset() != nullptr, "the ticket is ready");
    AssertStrEq(ticket->asset()->text.c_str(), "hello", "with the content of the file");
    Assert(!docs.preparedOn.count(std::this_thread::get_id()), "the file was read on a worker");
    Assert(docs.finishedOn.count(std::this_thread::get_id()) == 1 && docs.finishedOn.size() == 1, "and the asset was made here");
}

Test(async, the_same_file_is_read_once)
{
    Folder folder("as_once");
    Docs docs;
    const auto file = folder.write("a.txt", "hello");

    docs.hold = true;
    const auto a = docs.manager.loadAsync(file);
    const auto b = docs.manager.loadAsync(folder.path / "." / "a.txt");
    const auto c = docs.manager.loadAsync(file);

    AssertEq(docs.manager.loading(), 1, "one load on its way for the three");
    docs.hold = false;
    Assert(docs.pumpUntil(1), "finished");
    AssertEq(docs.prepares.load(), 1, "read once");
    AssertEq(docs.finishes.load(), 1, "made once");
    Assert(a->asset() != nullptr && a->asset() == b->asset() && b->asset() == c->asset(), "the three tickets have the same object");
    AssertEq(docs.manager.loading(), 0, "nothing on its way");
}

Test(async, a_loaded_file_is_ready_at_once)
{
    Folder folder("as_cached");
    Docs docs;
    const auto file = folder.write("a.txt", "hello");
    auto held = docs.manager.load(file);
    const auto ticket = docs.manager.loadAsync(file);

    Assert(ticket->done() && ticket->asset() == held, "the object that is already there, with no wait");
    AssertEq(docs.prepares.load(), 0, "nothing read");
    held.reset();
}

Test(async, a_failure_is_a_failure)
{
    Folder folder("as_fail");
    Docs docs;
    const auto file = folder.path / "missing.txt";
    const auto ticket = docs.manager.loadAsync(file);

    Assert(docs.pumpUntil(1), "finished");
    Assert(ticket->state() == Ticket::State::Failed, "failed");
    Assert(ticket->asset() == nullptr, "no asset");
    Assert(!ticket->error().empty(), "with a reason: '%s'", ticket->error().c_str());
    AssertEq(docs.finishes.load(), 0, "nothing made");
    AssertEq(docs.manager.loaded(), 0, "nothing kept");
    folder.write("missing.txt", "here now");
    const auto again = docs.manager.loadAsync(file);

    Assert(docs.pumpUntil(1), "asked again");
    Assert(again->asset() != nullptr && again->asset()->text == "here now", "it works once the file is there");
}

Test(async, a_finish_that_throws)
{
    Folder folder("as_finishfail");
    kuge::ThreadPool pool(1);
    kuge::AssetManager<Doc> manager([](const std::filesystem::path&) { return std::make_shared<Doc>(); });

    manager.enableAsync([&pool]() -> kuge::ThreadPool& { return pool; },
        [](const std::filesystem::path&) -> std::any { return 1; },
        [](std::any&&, const std::filesystem::path&) -> std::shared_ptr<Doc> { throw std::runtime_error("cannot make it"); });
    const auto ticket = manager.loadAsync(folder.write("a.txt", "x"));

    while (manager.pump() == 0) { std::this_thread::yield(); }
    Assert(ticket->state() == kuge::AssetManager<Doc>::Ticket::State::Failed && ticket->error() == "cannot make it", "the reason is kept: '%s'", ticket->error().c_str());
}

Test(async, load_wins_no_double)
{
    // A synchronous load() made while the same file is on its way: one object for everyone
    Folder folder("as_race");
    Docs docs;
    const auto file = folder.write("a.txt", "hello");

    docs.hold = true;
    const auto ticket = docs.manager.loadAsync(file);
    const auto sync = docs.manager.load(file);

    docs.hold = false;
    Assert(docs.pumpUntil(1), "finished");
    Assert(ticket->asset() == sync, "the ticket gets the object that load() made");
}

Test(async, without_enable_it_says_so)
{
    kuge::AssetManager<Doc> manager([](const std::filesystem::path&) { return std::make_shared<Doc>(); });
    bool threw = false;

    try { manager.loadAsync("x"); } catch (const std::logic_error&) { threw = true; }
    Assert(threw, "loadAsync() needs enableAsync()");
}

Test(async, many_at_once)
{
    Folder folder("as_many");
    Docs docs;
    std::vector<std::shared_ptr<Ticket>> tickets;

    for (int i = 0; i < 100; ++i) {
        tickets.push_back(docs.manager.loadAsync(folder.write("f" + std::to_string(i) + ".txt", "file " + std::to_string(i))));
    }
    Assert(docs.pumpUntil(100), "100 loads");
    for (int i = 0; i < 100; ++i) {
        Assert(tickets[static_cast<std::size_t>(i)]->asset() && tickets[static_cast<std::size_t>(i)]->asset()->text == "file " + std::to_string(i), "file %d has its own content", i);
    }
    Assert(docs.preparedOn.size() <= 2, "on the 2 workers: %zu threads", docs.preparedOn.size());
}

Test(async, manager_gone_workers_safe)
{
    // The manager can be destroyed while workers still read: they only touch what they share
    Folder folder("as_gone");
    kuge::ThreadPool pool(2);
    std::atomic<bool> release{false};
    std::atomic<int> started{0};
    {
        kuge::AssetManager<Doc> manager([](const std::filesystem::path&) { return std::make_shared<Doc>(); });

        manager.enableAsync([&pool]() -> kuge::ThreadPool& { return pool; },
            [&](const std::filesystem::path&) -> std::any { ++started; while (!release) { std::this_thread::yield(); } return 1; },
            [](std::any&&, const std::filesystem::path&) { return std::make_shared<Doc>(); });
        manager.loadAsync(folder.write("a.txt", "x"));
        manager.loadAsync(folder.write("b.txt", "x"));
        while (started < 2) { std::this_thread::yield(); }
    }
    release = true;
    pool.waitIdle();
    Assert(true, "no crash, and TSan / ASan say nothing");
}
