extern "C" {
    #include "kronklab/kronklab.h"
}
#include "AssetManager.hpp"
#include <memory>
#include <stdexcept>
#include <string>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    struct Loads {
        int         count = 0;
        bool        fail  = false;
        std::string last;
    };

    // An "asset" that is the number of the load that made it
    kuge::AssetManager<int> makeManager(Loads& loads)
    {
        return kuge::AssetManager<int>([&loads](const std::filesystem::path& path) {
            if (loads.fail) {
                throw std::runtime_error("cannot load " + path.string());
            }
            loads.last = path.string();
            return std::make_shared<int>(++loads.count);
        });
    }
}

Test(assets, same_file_same_object)
{
    Loads loads;
    auto manager = makeManager(loads);
    auto a = manager.load("textures/hero.png");
    auto b = manager.load("textures/hero.png");
    auto c = manager.load("textures/enemy.png");

    Assert(a == b, "the same object for the same file");
    Assert(a != c, "another file, another object");
    AssertEq(loads.count, 2, "2 loads, got %d", loads.count);
}

Test(assets, paths_are_normalized)
{
    Loads loads;
    auto manager = makeManager(loads);
    auto a = manager.load("textures/hero.png");
    auto b = manager.load("textures/./sub/../hero.png");

    Assert(a == b, "different spellings of the same path");
    AssertEq(loads.count, 1, "loaded once");
}

Test(assets, gone_when_nobody_holds)
{
    Loads loads;
    auto manager = makeManager(loads);

    {
        auto held = manager.load("a.png");
        AssertEq(manager.loaded(), 1, "alive while held");
    }
    AssertEq(manager.loaded(), 0, "gone when let go");
    auto again = manager.load("a.png");
    AssertEq(loads.count, 2, "read again from the file");
    AssertEq(*again, 2, "a new object");
}

Test(assets, purge_forgets_the_gone)
{
    Loads loads;
    auto manager = makeManager(loads);
    auto kept = manager.load("kept.png");

    manager.load("dropped.png");
    manager.purge();
    AssertEq(manager.loaded(), 1, "only the held one");
    Assert(manager.load("kept.png") == kept, "and it is still shared");
}

Test(assets, failed_load_keeps_nothing)
{
    Loads loads;
    auto manager = makeManager(loads);
    bool threw = false;

    loads.fail = true;
    try {
        manager.load("missing.png");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    Assert(threw, "the error comes out");
    AssertEq(manager.loaded(), 0, "nothing kept");
    loads.fail = false;
    AssertEq(*manager.load("missing.png"), 1, "it can be loaded once it works");
}
