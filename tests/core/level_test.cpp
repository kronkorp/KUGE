extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Level.hpp"

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using k3::Math::Vector3f;

    std::shared_ptr<k3::Mesh> quad(float size)
    {
        auto mesh = std::make_shared<k3::Mesh>();

        mesh->positions = {{-size, 0.0f, -size}, {size, 0.0f, -size}, {size, 0.0f, size}, {-size, 0.0f, size}};
        mesh->indices = {0, 2, 1, 0, 3, 2};
        return mesh;
    }

    k3::Node node(const char* name, int parent, Vector3f at = {}) { return {name, parent, {at, {0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}}}; }

    // A scene as an editor makes it: a house to see, a roof to stand on (at y = 3, under the node "col_roof" that is at 2), a zone, two markers
    k3::AnimatedModel scene(void)
    {
        k3::AnimatedModel model;

        model.nodes = {node("root", -1), node("house", 0), node("col_roof", 0, {0.0f, 2.0f, 0.0f}), node("zone_a", 0, {10.0f, 0.0f, 0.0f}),
            node("Spawn_attack", 0, {1.0f, 0.0f, 2.0f}), node("lamp_1", 0, {0.0f, 5.0f, 0.0f}), node("roof_child", 2, {0.0f, 1.0f, 0.0f})};
        model.primitives.push_back({quad(1.0f), nullptr, 1, -1});     // seen
        model.primitives.push_back({quad(4.0f), nullptr, 6, -1});     // under col_roof (through its child): world y = 3
        model.primitives.push_back({quad(2.0f), nullptr, 3, -1});     // the zone: 4 by 4 around (10, 0, 0)
        return model;
    }
}

Test(level, a_scene_says_what_plays)
{
    const auto level = kuge::readLevel(scene());

    AssertEq(level.visible->primitives.size(), 1u, "only the house is seen");
    AssertEq(level.colliders.size(), 2u, "a roof and a zone");
    const auto& roof = level.colliders[0];

    Assert(roof.name == "col_roof" && roof.collider.shape == kuge::Collider3D::Shape::Mesh, "the roof is a mesh");
    Assert(roof.collider.mesh && roof.collider.mesh->vertices.size() == 4, "of its four corners");
    Assert(roof.collider.mesh->vertices[0].y == 3.0f && roof.collider.mesh->vertices[0].x == -4.0f, "in world space: y = %f", roof.collider.mesh->vertices[0].y);
    const auto& zone = level.colliders[1];

    Assert(zone.name == "zone_a" && zone.collider.trigger && zone.collider.shape == kuge::Collider3D::Shape::Box, "the zone is a trigger box");
    Assert(zone.transform.translation.x == 10.0f && zone.collider.size.x == 4.0f && zone.collider.size.z == 4.0f, "around its meshes");
    AssertEq(level.markers.size(), 2u, "two markers (the case of the name does not count)");
    Assert(level.markers[0].name == "Spawn_attack" && level.markers[0].transform.translation.z == 2.0f, "at their places");
    Assert(level.markers[1].name == "lamp_1" && level.markers[1].transform.translation.y == 5.0f, "both");
}

Test(level, the_same_scene_the_same_hash)
{
    const auto one = kuge::readLevel(scene());
    const auto two = kuge::readLevel(scene());
    auto moved = scene();

    moved.nodes[4].local.translation.x = 1.5f;
    Assert(one.hash != 0 && one.hash == two.hash, "a client and a server read the same colliders");
    Assert(kuge::readLevel(moved).hash != one.hash, "and a marker that moved is another level");
}

Test(level, colliders_are_spawned)
{
    kw::World world;
    const auto level = kuge::readLevel(scene());
    const auto entities = kuge::spawnColliders(world, level, 4, 2);

    AssertEq(entities.size(), 2u, "one entity for each");
    Assert(world.has<kuge::Collider3D>(entities[0]) && world.has<kuge::Transform3D>(entities[1]), "with what the physics needs");
    Assert(world.get<kuge::Collider3D>(entities[0]).layer == 4 && world.get<kuge::Collider3D>(entities[0]).mask == 2, "on the layer asked");
    Assert(world.get<kuge::Transform3D>(entities[1]).translation.x == 10.0f, "the zone is where it is");
}
