extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Reflect.hpp"
#include "Physics3DComponents.hpp"
#include "Transform2D.hpp"
#include "Transform3D.hpp"

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    enum class Mood : std::uint8_t { Calm, Angry, Afraid };

    struct Health
    {
        int         points = 100;
        float       regen = 0.5f;
        bool        alive = true;
        std::string label = "npc";
        kuge::Vec2  home{0.0f, 0.0f};
        Mood        mood = Mood::Calm;
        kw::Entity  target = 0;
        std::uint8_t armor = 0;
    };

    void describeHealth(kuge::ComponentRegistry& registry)
    {
        registry.describe<Health>("Health", {
            kuge::field<Health>("points", &Health::points, {.min = 0, .max = 1000, .help = "Hit points"}),
            kuge::field<Health>("regen", &Health::regen),
            kuge::field<Health>("alive", &Health::alive),
            kuge::field<Health>("label", &Health::label),
            kuge::field<Health>("home", &Health::home),
            kuge::field<Health>("mood", &Health::mood, {.names = {"Calm", "Angry", "Afraid"}}),
            kuge::entityField<Health>("target", &Health::target),
            kuge::field<Health>("armor", &Health::armor),
        });
    }

    struct Fixture
    {
        kuge::ComponentRegistry registry;
        kw::World               world;
        kw::Entity              entity;

        Fixture()
        {
            describeHealth(registry);
            kuge::describeCore(registry);
            entity = world.create();
        }
    };
}

Test(reflect, set_a_field_by_name)
{
    Fixture f;

    Assert(!f.registry.has(f.world, f.entity, "Health"), "no component yet");
    Assert(f.registry.add(f.world, f.entity, "Health") && f.registry.has(f.world, f.entity, "Health"), "added by name");
    Assert(f.world.get<Health>(f.entity).points == 100, "with its default values");
    Assert(f.registry.set(f.world, f.entity, "Health", "points", std::int64_t{50}).ok && f.world.get<Health>(f.entity).points == 50, "an int");
    Assert(f.registry.set(f.world, f.entity, "Health", "regen", 2.5).ok && f.world.get<Health>(f.entity).regen == 2.5f, "a float");
    Assert(f.registry.set(f.world, f.entity, "Health", "regen", std::int64_t{3}).ok && f.world.get<Health>(f.entity).regen == 3.0f, "a whole number widens to a float");
    Assert(f.registry.set(f.world, f.entity, "Health", "alive", false).ok && !f.world.get<Health>(f.entity).alive, "a bool");
    Assert(f.registry.set(f.world, f.entity, "Health", "label", std::string("boss")).ok && f.world.get<Health>(f.entity).label == "boss", "a string");
    Assert(f.registry.set(f.world, f.entity, "Health", "home", kuge::Vec2{3.0f, 4.0f}).ok && f.world.get<Health>(f.entity).home.y == 4.0f, "a Vec2");
    Assert(f.registry.set(f.world, f.entity, "Health", "mood", std::string("Afraid")).ok && f.world.get<Health>(f.entity).mood == Mood::Afraid, "an enum by its name");
    Assert(f.registry.set(f.world, f.entity, "Health", "target", kuge::EntityRef{42}).ok && f.world.get<Health>(f.entity).target == 42, "an entity");
    Assert(f.registry.set(f.world, f.entity, "Health", "armor", std::int64_t{200}).ok && f.world.get<Health>(f.entity).armor == 200, "a small integer");
    const auto values = f.registry.get(f.world, f.entity, "Health");

    AssertEq(values.size(), 8u, "all the fields are read back");
    Assert(values[0].first == "points" && std::get<std::int64_t>(values[0].second) == 50, "in order, with their values");
    Assert(std::get<std::string>(values[5].second) == "Afraid", "an enum reads as its name");
    const kw::Entity other = f.world.create();

    Assert(f.registry.set(f.world, other, "Health", "points", std::int64_t{7}).ok && f.world.get<Health>(other).points == 7, "set adds the component if the entity has none");
    Assert(f.registry.remove(f.world, other, "Health") && !f.registry.has(f.world, other, "Health"), "and it can be removed");
}

Test(reflect, wrong_things_are_named)
{
    Fixture f;

    f.registry.add(f.world, f.entity, "Health");
    const auto why = [&](const char* field, const kuge::FieldValue& value) { return f.registry.set(f.world, f.entity, "Health", field, value).message; };

    Assert(why("points", std::string("lots")).find("Health.points is a whole number") == 0, "a string for an int: %s", why("points", std::string("lots")).c_str());
    Assert(why("points", 2.5).find("Health.points") == 0, "a fraction for an int");
    Assert(why("alive", std::int64_t{1}).find("Health.alive is a boolean") == 0, "a number for a bool");
    Assert(why("nothing", true).find("Health has no field 'nothing'") == 0, "an unknown field: %s", why("nothing", true).c_str());
    Assert(f.registry.set(f.world, f.entity, "Ghost", "x", true).message.find("no component 'Ghost'") != std::string::npos, "an unknown component");
    Assert(why("points", std::int64_t{5000}).find("Health.points: ") == 0 && why("points", std::int64_t{5000}).find("outside") != std::string::npos, "outside its limits: %s", why("points", std::int64_t{5000}).c_str());
    Assert(why("points", std::int64_t{-1}).find("outside") != std::string::npos, "below them");
    AssertEq(f.world.get<Health>(f.entity).points, 100, "a refused value changes nothing");
    Assert(why("mood", std::string("Happy")).find("'Happy' is not one of its values") != std::string::npos, "an enum value that is not one: %s", why("mood", std::string("Happy")).c_str());
    Assert(why("armor", std::int64_t{300}).find("does not fit") != std::string::npos, "an integer that does not fit its type");
    Assert(why("home", kuge::Vec2{}) == "", "(and a good one says nothing)");
}

Test(reflect, json_round_trip)
{
    Fixture f;

    f.registry.add(f.world, f.entity, "Health");
    f.world.get<Health>(f.entity) = Health{80, 1.5f, false, "orc", {2.0f, 3.0f}, Mood::Angry, 9, 7};
    f.world.add<kuge::Transform2D>(f.entity, kuge::Transform2D{{10.0f, 20.0f}, 90.0f, {2.0f, 2.0f}});
    const kuge::Json json = f.registry.toJson(f.world, f.entity, "Health");

    AssertStrEq(json["mood"].string().c_str(), "Angry", "an enum is written as its name");
    Assert(json["home"].items().size() == 2 && json["home"].at(1).number() == 3.0, "a Vec2 is [x, y]");
    const kw::Entity copy = f.world.create();
    const auto result = f.registry.fromJson(f.world, copy, "Health", kuge::Json::parse(json.dump()));

    Assert(result.ok, "read back: %s", result.message.c_str());
    const Health& a = f.world.get<Health>(f.entity);
    const Health& b = f.world.get<Health>(copy);

    Assert(a.points == b.points && a.regen == b.regen && a.alive == b.alive && a.label == b.label && a.home == b.home && a.mood == b.mood && a.target == b.target && a.armor == b.armor,
        "an equal component");
    const kuge::Json whole = f.registry.entityToJson(f.world, f.entity);

    Assert(whole.find("Health") && whole.find("Transform2D") && whole["Transform2D"]["position"].at(0).number() == 10.0, "an entity is every described component it has");
    const kw::Entity clone = f.world.create();

    Assert(f.registry.entityFromJson(f.world, clone, kuge::Json::parse(whole.dump(2))).ok, "and reads back");
    Assert(f.world.get<kuge::Transform2D>(clone).rotation == 90.0f && f.world.get<Health>(clone).label == "orc", "a clone");
    Assert(f.registry.entityToJson(f.world, clone) == whole, "which writes the same JSON");
}

Test(reflect, json_is_checked_first)
{
    Fixture f;

    f.registry.add(f.world, f.entity, "Health");
    const auto bad = [&](const char* text) { return f.registry.fromJson(f.world, f.entity, "Health", kuge::Json::parse(text)); };

    Assert(bad(R"({"points": 10, "label": 5})").message.find("Health.label is a string") == 0, "the field that is wrong is named");
    AssertEq(f.world.get<Health>(f.entity).points, 100, "and nothing was changed, not even the good field before it");
    Assert(bad(R"({"unknown": 1})").message.find("no field 'unknown'") != std::string::npos, "a field that is not described");
    Assert(bad(R"({"points": 5000})").message.find("outside") != std::string::npos, "outside the limits");
    AssertEq(f.world.get<Health>(f.entity).points, 100, "still nothing");
    Assert(bad(R"({"home": [1, 2, 3]})").message.find("Health.home is a pair") == 0, "a pair with three numbers");
    Assert(bad(R"({"mood": 1})").message.find("Health.mood") == 0, "an enum by number");
    Assert(!f.registry.fromJson(f.world, f.entity, "Health", kuge::Json::parse("[1]")).ok, "an array is not fields");
    const kw::Entity fresh = f.world.create();

    Assert(!f.registry.fromJson(f.world, fresh, "Health", kuge::Json::parse(R"({"points": 9999})")).ok && !f.registry.has(f.world, fresh, "Health"), "a refused JSON leaves no component behind");
    Assert(!f.registry.entityFromJson(f.world, fresh, kuge::Json::parse(R"({"Transform2D": {"rotation": 1}, "Health": {"points": -5}})")).ok && !f.registry.has(f.world, fresh, "Transform2D"),
        "an entity with one bad component adds none of them");
    Assert(f.registry.entityFromJson(f.world, fresh, kuge::Json::parse(R"({"Ghost": {}})")).message.find("no component 'Ghost'") != std::string::npos, "an unknown component");
    Assert(f.registry.fromJson(f.world, f.entity, "Health", kuge::Json::parse(R"({"points": 3})")).ok && f.world.get<Health>(f.entity).regen == 0.5f, "a field that is not in the JSON keeps its value");
}

Test(reflect, names_are_unique)
{
    kuge::ComponentRegistry registry;
    bool twice = false, fields = false;

    describeHealth(registry);
    try { describeHealth(registry); } catch (const std::invalid_argument&) { twice = true; }
    try {
        registry.describe<Mood>("Moody", {kuge::field<Health>("a", &Health::points), kuge::field<Health>("a", &Health::regen)});
    } catch (const std::invalid_argument&) { fields = true; }
    Assert(twice, "two components with the same name are refused");
    Assert(fields, "so are two fields with the same name");
    Assert(registry.byName("Health") && !registry.byName("Nothing"), "by name");
    AssertEq(registry.all().size(), 1u, "in registration order");
}

Test(reflect, the_core_is_described)
{
    Fixture f;

    f.registry.add(f.world, f.entity, "Transform3D");
    f.registry.add(f.world, f.entity, "Body3D");
    f.registry.add(f.world, f.entity, "Collider3D");
    Assert(f.registry.set(f.world, f.entity, "Transform3D", "translation", k3::Math::Vector3f{1.0f, 2.0f, 3.0f}).ok && f.world.get<kuge::Transform3D>(f.entity).translation.z == 3.0f, "a Vec3");
    Assert(f.registry.set(f.world, f.entity, "Transform3D", "rotation", k3::Math::Vector4f{0.0f, 0.0f, 0.0f, 1.0f}).ok, "a Vec4 (a quaternion)");
    Assert(f.registry.set(f.world, f.entity, "Body3D", "type", std::string("Kinematic")).ok && f.world.get<kuge::Body3D>(f.entity).type == kuge::Body3D::Type::Kinematic, "an enum of the engine");
    Assert(!f.registry.set(f.world, f.entity, "Collider3D", "restitution", 3.0).ok, "restitution is limited to 0..1");
    Assert(f.registry.set(f.world, f.entity, "Collider3D", "shape", std::string("Sphere")).ok && f.world.get<kuge::Collider3D>(f.entity).shape == kuge::Collider3D::Shape::Sphere, "a shape by name");
    const auto names = f.registry.componentsOf(f.world, f.entity);

    Assert(names.size() == 3 && names[0] == "Transform3D" && names[1] == "Collider3D" && names[2] == "Body3D", "which components an entity has, in registration order");
    for (const char* name : {"Transform2D", "Transform3D", "Collider3D", "Body3D", "Character3D"}) {
        Assert(f.registry.byName(name) != nullptr, "%s is described", name);
    }
}
