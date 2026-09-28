extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Snapshot.hpp"
#include "Transform2D.hpp"
#include <cstring>
#include <string>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    struct Health { int current = 0; int max = 0; };
    struct Name { std::string text; };
    struct Score { int points = 0; };
    struct Level { std::string name; int number = 0; };
    struct NotSaved { int secret = 42; };

    kuge::SnapshotRegistry makeRegistry(bool withHealth = true)
    {
        kuge::SnapshotRegistry registry;

        registry.component<kuge::Transform2D>("transform",
            [](kuge::ByteWriter& out, const kuge::Transform2D& t) {
                out.write(t.position.x); out.write(t.position.y); out.write(t.rotation); out.write(t.scale.x); out.write(t.scale.y);
            },
            [](kuge::ByteReader& in) {
                kuge::Transform2D t;
                t.position.x = in.read<float>(); t.position.y = in.read<float>(); t.rotation = in.read<float>();
                t.scale.x = in.read<float>(); t.scale.y = in.read<float>();
                return t;
            });
        if (withHealth) {
            registry.component<Health>("health",
                [](kuge::ByteWriter& out, const Health& h) { out.write(h.current); out.write(h.max); },
                [](kuge::ByteReader& in) { Health h; h.current = in.read<int>(); h.max = in.read<int>(); return h; });
        }
        registry.component<Name>("name",
            [](kuge::ByteWriter& out, const Name& n) { out.writeString(n.text); },
            [](kuge::ByteReader& in) { return Name{in.readString()}; });
        registry.resource<Score>("score",
            [](kuge::ByteWriter& out, const Score& s) { out.write(s.points); },
            [](kuge::ByteReader& in) { return Score{in.read<int>()}; });
        registry.resource<Level>("level",
            [](kuge::ByteWriter& out, const Level& l) { out.writeString(l.name); out.write(l.number); },
            [](kuge::ByteReader& in) { Level l; l.name = in.readString(); l.number = in.read<int>(); return l; });
        return registry;
    }

    kw::Entity hero(kw::World& world, float x, int hp, const char* name)
    {
        const kw::Entity e = world.create();

        world.add<kuge::Persistent>(e, kuge::Persistent{});
        world.add<kuge::Transform2D>(e, kuge::Transform2D{{x, x * 2.0f}, 15.0f, {1.5f, 2.0f}});
        world.add<Health>(e, Health{hp, 100});
        world.add<Name>(e, Name{name});
        return e;
    }

    std::vector<std::uint8_t> saved(kw::World& world, const kuge::SnapshotRegistry& registry)
    {
        kuge::ByteWriter out;

        registry.save(world, out);
        return out.bytes();
    }
}

Test(snapshot, save_and_load)
{
    const auto registry = makeRegistry();
    kw::World world;
    kw::World other;

    hero(world, 10.0f, 80, "Ada");
    hero(world, 20.0f, 55, "Bob");
    world.addResource<Score>(Score{1234});
    world.addResource<Level>(Level{"caves", 3});
    const auto bytes = saved(world, registry);

    kuge::ByteReader in(bytes);
    const auto created = registry.load(other, in);

    AssertEq(created.size(), 2, "two entities");
    Assert(in.atEnd(), "everything was read");
    AssertEq(other.getResource<Score>().points, 1234, "a resource");
    AssertStrEq(other.getResource<Level>().name.c_str(), "caves", "a resource with text");
    AssertEq(other.getResource<Level>().number, 3, "and a number");
    const auto& t = other.get<kuge::Transform2D>(created[0]);
    Assert(t.position == kuge::Vec2(10.0f, 20.0f) && t.rotation == 15.0f && t.scale == kuge::Vec2(1.5f, 2.0f), "the transform");
    AssertEq(other.get<Health>(created[0]).current, 80, "a component");
    AssertStrEq(other.get<Name>(created[1]).text.c_str(), "Bob", "in order");
    AssertEq(other.get<Health>(created[1]).current, 55, "the second one");
    Assert(other.has<kuge::Persistent>(created[0]), "still marked to be saved");
}

Test(snapshot, only_what_is_asked)
{
    const auto registry = makeRegistry();
    kw::World world;
    kw::World other;
    const kw::Entity a = hero(world, 1.0f, 10, "kept");
    const kw::Entity scenery = world.create();      // not Persistent
    const kw::Entity partial = world.create();      // saved, with some of the components

    world.add<kuge::Transform2D>(scenery, kuge::Transform2D{});
    world.add<kuge::Persistent>(partial, kuge::Persistent{});
    world.add<Health>(partial, Health{7, 7});
    world.add<NotSaved>(a, NotSaved{});             // a type that was not registered
    world.add<NotSaved>(partial, NotSaved{});

    const auto bytes = saved(world, registry);

    kuge::ByteReader in(bytes);
    const auto created = registry.load(other, in);

    AssertEq(created.size(), 2, "the scenery is not saved");
    Assert(!other.has<NotSaved>(created[0]) && !other.has<NotSaved>(created[1]), "nor a component that was not registered");
    Assert(other.has<Health>(created[1]) && !other.has<kuge::Transform2D>(created[1]) && !other.has<Name>(created[1]),
        "an entity keeps just the components it had");
}

Test(snapshot, same_world_same_bytes)
{
    const auto registry = makeRegistry();
    auto build = [&registry](bool churn) {
        auto world = std::make_unique<kw::World>();

        if (churn) {
            // Entities that come and go change how the ECS stores things
            for (int i = 0; i < 10; ++i) {
                const kw::Entity temporary = hero(*world, static_cast<float>(i), i, "x");

                world->remove(temporary);
            }
        }
        hero(*world, 5.0f, 50, "first");
        hero(*world, 6.0f, 60, "second");
        hero(*world, 7.0f, 70, "third");
        world->addResource<Score>(Score{9});
        return saved(*world, registry);
    };

    Assert(build(false) == build(false), "twice: the same bytes");
    Assert(build(false) == build(true), "and whatever happened to the World before");
}

Test(snapshot, damage_changes_nothing)
{
    const auto registry = makeRegistry();
    kw::World world;
    kw::World target;

    hero(world, 1.0f, 10, "a");
    hero(world, 2.0f, 20, "b");
    world.addResource<Score>(Score{5});
    const auto good = saved(world, registry);
    target.addResource<Score>(Score{777});
    int refused = 0;
    int tried = 0;

    // Cut at every length: none is loaded, and the World is left as it was
    for (std::size_t length = 0; length < good.size(); ++length) {
        kuge::ByteReader in(std::span<const std::uint8_t>(good.data(), length));

        ++tried;
        try {
            registry.load(target, in);
        } catch (const kuge::SerializerError&) {
            ++refused;
        }
    }
    AssertEq(refused, tried, "every cut is refused");
    AssertEq(target.getResource<Score>().points, 777, "the resource is untouched");
    int entities = 0;
    for ([[maybe_unused]] kw::Entity entity : target.view<kuge::Persistent>()) {
        ++entities;
    }
    AssertEq(entities, 0, "and no entity was created");
}

Test(snapshot, unknown_names_are_skipped)
{
    const auto full = makeRegistry(true);
    const auto old = makeRegistry(false);       // a version of the game that does not know Health
    kw::World world;
    kw::World other;

    hero(world, 3.0f, 33, "kept");
    const auto bytes = saved(world, full);
    kuge::ByteReader in(bytes);
    const auto created = old.load(other, in);

    AssertEq(created.size(), 1, "the entity loads");
    AssertStrEq(other.get<Name>(created[0]).text.c_str(), "kept", "with what this version knows");
    Assert(!other.has<Health>(created[0]), "and without what it does not");
    Assert(in.atEnd(), "the unknown part was skipped, the rest was read");
}

Test(snapshot, a_wrong_header)
{
    const auto registry = makeRegistry();
    kw::World world;
    const std::uint8_t garbage[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    kuge::ByteReader in(garbage);
    bool refused = false;

    try { registry.load(world, in); } catch (const kuge::SerializerError&) { refused = true; }
    Assert(refused, "not a snapshot");
}

Test(snapshot, a_missing_resource_is_skipped)
{
    const auto registry = makeRegistry();
    kw::World world;
    kw::World other;

    world.addResource<Score>(Score{4});         // no Level
    const auto bytes = saved(world, registry);
    kuge::ByteReader in(bytes);
    registry.load(other, in);
    AssertEq(other.getResource<Score>().points, 4, "the one that was there");
    bool absent = false;
    try { other.getResource<Level>(); } catch (const kw::ResourceError&) { absent = true; }
    Assert(absent, "and the other is still absent");
}

Test(snapshot, names_are_used_once)
{
    kuge::SnapshotRegistry registry;
    int refused = 0;
    auto write = [](kuge::ByteWriter&, const Score&) {};
    auto read = [](kuge::ByteReader&) { return Score{}; };

    registry.resource<Score>("score", write, read);
    try { registry.resource<Score>("score", write, read); } catch (const std::invalid_argument&) { ++refused; }
    try {
        registry.component<Health>("score", [](kuge::ByteWriter&, const Health&) {}, [](kuge::ByteReader&) { return Health{}; });
    } catch (const std::invalid_argument&) { ++refused; }
    try { registry.resource<Score>("", write, read); } catch (const std::invalid_argument&) { ++refused; }
    AssertEq(refused, 3, "a name is not empty, and belongs to one type: %d", refused);
}

Test(snapshot, a_big_world)
{
    const auto registry = makeRegistry();
    kw::World world;
    kw::World other;

    for (int i = 0; i < 5000; ++i) {
        hero(world, static_cast<float>(i), i % 100, "unit");
    }
    const auto bytes = saved(world, registry);
    kuge::ByteReader in(bytes);
    const auto created = registry.load(other, in);

    AssertEq(created.size(), 5000, "5000 entities");
    AssertEq(other.get<Health>(created[4999]).current, 99, "the last one");
    Assert(other.get<kuge::Transform2D>(created[1234]).position.x == 1234.0f, "one in the middle");
}
