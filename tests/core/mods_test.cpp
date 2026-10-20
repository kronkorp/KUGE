extern "C" {
    #include "kronklab/kronklab.h"
}
#include "AssetManager.hpp"
#include "Mods.hpp"
#include <filesystem>
#include <fstream>
#include <unistd.h>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    namespace fs = std::filesystem;

    // A folder of the test, with files written as asked, removed with it
    struct Tree
    {
        fs::path path;

        Tree() : path(fs::temp_directory_path() / ("kuge_mods_" + std::to_string(::getpid()) + "_" + std::to_string(counter()++)))
        {
            fs::create_directories(path);
        }
        ~Tree() { std::error_code ignored; fs::remove_all(path, ignored); }

        static int& counter(void) { static int n = 0; return n; }

        void write(const std::string& relative, const std::string& text) const
        {
            fs::create_directories((path / relative).parent_path());
            std::ofstream(path / relative, std::ios::binary) << text;
        }

        void mod(const std::string& name, const std::string& depends = "[]", const std::string& version = "1.0") const
        {
            write("mods/" + name + "/mod.json", "{\"name\": \"" + name + "\", \"version\": \"" + version + "\", \"depends\": " + depends + "}");
        }
    };

    std::string readAll(const fs::path& file)
    {
        std::ifstream in(file, std::ios::binary);

        return std::string(std::istreambuf_iterator<char>(in), {});
    }

    std::string modError(const fs::path& folder)
    {
        try { kuge::Mods::scan(folder); } catch (const kuge::ModError& error) { return error.what(); }
        return "accepted";
    }
}

Test(roots, the_upper_one_wins)
{
    Tree game, mod;

    game.write("data/a.json", "game a");
    game.write("data/b.json", "game b");
    mod.write("data/a.json", "mod a");
    mod.write("data/c.json", "mod c");
    kuge::AssetRoots roots(game.path);
    roots.add(mod.path, 1, "dark");
    AssertStrEq(readAll(roots.resolve("data/a.json")).c_str(), "mod a", "a file in both gives the upper root's");
    AssertStrEq(readAll(roots.resolve("data/b.json")).c_str(), "game b", "a file in the game only gives that one");
    AssertStrEq(readAll(roots.resolve("data/c.json")).c_str(), "mod c", "a file in the mod only too");
    AssertStrEq(roots.whoHas("data/a.json").c_str(), "dark", "it says who has it");
    AssertStrEq(roots.whoHas("data/b.json").c_str(), "game", "the game");
    AssertStrEq(roots.whoHas("data/z.json").c_str(), "", "nobody");
    Tree another;

    another.write("data/a.json", "second mod");
    roots.add(another.path, 1, "later");
    AssertStrEq(readAll(roots.resolve("data/a.json")).c_str(), "second mod", "at the same priority the last added wins");
    const auto files = roots.list("data");

    AssertEq(files.size(), 3u, "a, b and c, once each");
    AssertStrEq(readAll(files[0]).c_str(), "second mod", "and the file of the highest root for each");
}

Test(roots, a_missing_file_names_the_roots)
{
    Tree game, mod;
    kuge::AssetRoots roots(game.path);

    roots.add(mod.path, 1, "dark");
    bool named = false;

    try {
        roots.resolve("data/none.json");
    } catch (const kuge::AssetNotFound& error) {
        const std::string text = error.what();

        named = text.find("data/none.json") != std::string::npos && text.find(game.path.string()) != std::string::npos
            && text.find(mod.path.string()) != std::string::npos && text.find("dark") != std::string::npos;
    }
    Assert(named, "the error names the file and every root searched");
    Assert(!roots.find("data/none.json"), "find says nothing instead");
    Assert(roots.find(game.path / "x") == std::nullopt, "an absolute path is used as it is (and is not there)");
}

Test(roots, an_asset_manager_uses_them)
{
    Tree game, mod;

    game.write("a.txt", "from the game");
    game.write("b.txt", "only the game");
    mod.write("a.txt", "from the mod");
    auto roots = std::make_shared<kuge::AssetRoots>(game.path);
    kuge::AssetManager<std::string> texts([](const fs::path& path) { return std::make_shared<std::string>(readAll(path)); });

    roots->add(mod.path, 1, "dark");
    texts.setRoots(roots);
    const auto a = texts.load("a.txt");

    AssertStrEq(a->c_str(), "from the mod", "a relative name is looked for in the roots");
    AssertStrEq(texts.load("b.txt")->c_str(), "only the game", "down to the game");
    Assert(texts.load("a.txt") == a, "and shared as before");
}

Test(mods, depends_orders_them)
{
    Tree tree;

    tree.mod("zeta");
    tree.mod("alpha", "[\"zeta\"]");
    tree.mod("mid", "[\"alpha\"]");
    tree.mod("beta");
    tree.write("mods/not_a_mod/readme.txt", "no mod.json: ignored");
    const auto mods = kuge::Mods::scan(tree.path / "mods");
    std::string order;

    for (const auto& mod : mods.ordered()) {
        order += mod.name + " ";
    }
    AssertStrEq(order.c_str(), "beta zeta alpha mid ", "dependencies first, then by name (beta has none and comes first by name)");
    kuge::Mods again = kuge::Mods::scan(tree.path / "mods");

    Assert(again.ordered().size() == 4 && again.ordered()[0].name == "beta", "the same every time");
    Assert(kuge::Mods::scan(tree.path / "no_such_folder").empty(), "no folder, no mod");
    kuge::AssetRoots roots("game");

    mods.apply(roots);
    AssertEq(roots.roots().size(), 5u, "the game and four mods");
    AssertStrEq(roots.roots().front().name.c_str(), "mid", "the last one loaded is looked in first");
}

Test(mods, bad_mods_are_named)
{
    {
        Tree tree;

        tree.mod("a", "[\"b\"]");
        tree.mod("b", "[\"a\"]");
        const std::string why = modError(tree.path / "mods");

        Assert(why.find("wait for each other") != std::string::npos && why.find("a, b") != std::string::npos, "a cycle: %s", why.c_str());
    }
    {
        Tree tree;

        tree.mod("a", "[\"ghost\"]");
        const std::string why = modError(tree.path / "mods");

        Assert(why.find("'a' needs 'ghost'") != std::string::npos, "a missing dependency names both: %s", why.c_str());
    }
    {
        Tree tree;

        tree.write("mods/broken/mod.json", "{\"name\": }");
        const std::string why = modError(tree.path / "mods");

        Assert(why.find("broken") != std::string::npos && why.find("line 1") != std::string::npos, "a bad mod.json says which mod and where: %s", why.c_str());
    }
    {
        Tree tree;

        tree.write("mods/x/mod.json", "{\"version\": \"1\"}");
        Assert(modError(tree.path / "mods").find("no name") != std::string::npos || modError(tree.path / "mods").find("no member 'name'") != std::string::npos, "a mod with no name");
    }
    {
        Tree tree;

        tree.write("mods/one/mod.json", "{\"name\": \"same\"}");
        tree.write("mods/two/mod.json", "{\"name\": \"same\"}");
        Assert(modError(tree.path / "mods").find("two mods named 'same'") != std::string::npos, "two mods with a name");
    }
    {
        Tree tree;

        tree.write("mods/old/mod.json", "{\"name\": \"old\", \"kuge\": \"0.0\"}");
        try {
            kuge::Mods::scan(tree.path / "mods", "0.1");
            Assert(false, "a mod made for another KUGE is refused");
        } catch (const kuge::ModError& error) {
            Assert(std::string(error.what()).find("'old'") != std::string::npos, "and named: %s", error.what());
        }
    }
}

Test(mods, what_they_replace)
{
    Tree tree;

    tree.write("game/data/a.json", "game");
    tree.write("game/data/b.json", "game");
    tree.mod("first");
    tree.write("mods/first/data/a.json", "first");
    tree.write("mods/first/data/new.json", "first");
    tree.mod("second", "[\"first\"]");
    tree.write("mods/second/data/a.json", "second");
    const auto mods = kuge::Mods::scan(tree.path / "mods");
    const auto replaced = mods.overrides(tree.path / "game");

    AssertEq(replaced.size(), 2u, "two files are replaced (a new file replaces nothing, mod.json is not data)");
    Assert(replaced[0].file == "data/a.json" && replaced[0].mod == "first" && replaced[0].replaces == "game", "first replaces the game's a.json");
    Assert(replaced[1].file == "data/a.json" && replaced[1].mod == "second" && replaced[1].replaces == "first", "second replaces first's");
}

Test(mods, a_fingerprint_of_the_files)
{
    Tree one, two;

    for (const Tree* tree : {&one, &two}) {
        tree->mod("a");
        tree->write("mods/a/data/x.json", "{\"damage\": 3}");
        tree->write("mods/a/resources/s.png", std::string("png\0bytes", 9));
    }
    const auto first = kuge::Mods::scan(one.path / "mods").fingerprint();
    const auto same = kuge::Mods::scan(two.path / "mods").fingerprint();

    Assert(first != 0 && first == same, "two mods with the same files have the same fingerprint (in other folders)");
    two.write("mods/a/data/x.json", "{\"damage\": 4}");
    Assert(kuge::Mods::scan(two.path / "mods").fingerprint() != first, "a changed byte changes it");
    two.write("mods/a/data/x.json", "{\"damage\": 3}");
    two.write("mods/a/data/extra.json", "{}");
    Assert(kuge::Mods::scan(two.path / "mods").fingerprint() != first, "so does a new file");
    two.write("mods/a/data/extra.json", "{}");
    Tree versioned;

    versioned.mod("a", "[]", "2.0");
    versioned.write("mods/a/data/x.json", "{\"damage\": 3}");
    versioned.write("mods/a/resources/s.png", std::string("png\0bytes", 9));
    Assert(kuge::Mods::scan(versioned.path / "mods").fingerprint() != first, "and the version");
    Tree empty;

    AssertEq(kuge::Mods::scan(empty.path / "mods").fingerprint(), 0u, "no mod: 0");
}
