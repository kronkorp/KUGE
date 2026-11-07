#include "Level.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>

namespace kuge
{
    namespace
    {
        bool startsWith(const std::string& name, const char* prefix)
        {
            const std::size_t length = std::strlen(prefix);

            return name.size() >= length && std::equal(prefix, prefix + length, name.begin(), [](char a, char b) { return a == std::tolower(static_cast<unsigned char>(b)); });
        }

        void mix(std::uint64_t& hash, const void* data, std::size_t size)
        {
            for (std::size_t i = 0; i < size; ++i) {
                hash = (hash ^ static_cast<const unsigned char*>(data)[i]) * 1099511628211ull;
            }
        }

        // The node, or the nearest of its parents, that has this prefix (-1: none)
        int under(const k3::AnimatedModel& scene, int node, const char* prefix)
        {
            for (; node >= 0; node = scene.nodes[static_cast<std::size_t>(node)].parent) {
                if (startsWith(scene.nodes[static_cast<std::size_t>(node)].name, prefix)) {
                    return node;
                }
            }
            return -1;
        }
    }

    LevelData readLevel(const k3::AnimatedModel& scene)
    {
        LevelData level;
        std::vector<k3::Transform> rest(scene.nodes.size());

        for (std::size_t i = 0; i < rest.size(); ++i) {
            rest[i] = scene.nodes[i].local;
        }
        const auto globals = scene.globalTransforms(rest);
        auto visible = std::make_shared<k3::AnimatedModel>(scene);
        std::map<int, std::pair<std::shared_ptr<CollisionMesh>, std::size_t>> meshes;   // by node of the collider
        std::map<int, k3::Math::Bounds3f> zones;

        visible->primitives.clear();
        for (const auto& primitive : scene.primitives) {
            const bool plain = primitive.skin < 0 && primitive.mesh;
            const int collides = plain ? under(scene, primitive.node, "col_") : -1;
            const int zone = plain ? under(scene, primitive.node, "zone_") : -1;

            if (collides < 0 && zone < 0) {
                visible->primitives.push_back(primitive);
                continue;
            }
            const auto& matrix = globals[static_cast<std::size_t>(primitive.node)];
            const auto& source = *primitive.mesh;

            if (zone >= 0) {
                for (const auto& position : source.positions) {
                    zones[zone].expand(matrix.transformPoint(position));
                }
                continue;
            }
            auto& made = meshes[collides].first;

            if (!made) {
                made = std::make_shared<CollisionMesh>();
            }
            const auto first = static_cast<std::uint32_t>(made->vertices.size());

            for (const auto& position : source.positions) {
                made->vertices.push_back(matrix.transformPoint(position));
            }
            if (source.indices.empty()) {
                for (std::uint32_t i = 0; i < source.positions.size(); ++i) {
                    made->indices.push_back(first + i);
                }
            }
            for (const auto index : source.indices) {
                made->indices.push_back(first + index);
            }
        }
        level.visible = std::move(visible);
        for (auto& [node, entry] : meshes) {
            LevelCollider collider{scene.nodes[static_cast<std::size_t>(node)].name, Collider3D::triangles(entry.first), {}};

            level.colliders.push_back(std::move(collider));
        }
        for (const auto& [node, box] : zones) {
            const auto size = box.max - box.min;
            LevelCollider zone{scene.nodes[static_cast<std::size_t>(node)].name, Collider3D::box(size.x, size.y, size.z).asTrigger(), {}};

            zone.transform.translation = box.center();
            level.colliders.push_back(std::move(zone));
        }
        for (std::size_t i = 0; i < scene.nodes.size(); ++i) {
            const std::string& name = scene.nodes[i].name;

            if (startsWith(name, "spawn_") || startsWith(name, "lamp")) {
                LevelMarker marker{name, {}};
                const k3::Transform placed = k3::Transform::fromMatrix(globals[i]);

                marker.transform.translation = placed.translation;
                marker.transform.rotation = placed.rotation;
                level.markers.push_back(std::move(marker));
            }
        }
        level.hash = 1469598103934665603ull;
        for (const auto& collider : level.colliders) {
            mix(level.hash, collider.name.data(), collider.name.size());
            mix(level.hash, &collider.transform.translation, sizeof(collider.transform.translation));
            mix(level.hash, &collider.collider.size, sizeof(collider.collider.size));
            if (collider.collider.mesh) {
                const auto& mesh = *collider.collider.mesh;

                mix(level.hash, mesh.vertices.data(), mesh.vertices.size() * sizeof(mesh.vertices[0]));
                mix(level.hash, mesh.indices.data(), mesh.indices.size() * sizeof(mesh.indices[0]));
            }
        }
        for (const auto& marker : level.markers) {
            mix(level.hash, marker.name.data(), marker.name.size());
            mix(level.hash, &marker.transform.translation, sizeof(marker.transform.translation));
            mix(level.hash, &marker.transform.rotation, sizeof(marker.transform.rotation));
        }
        return level;
    }

    std::vector<kw::Entity> spawnColliders(kw::World& world, const LevelData& level, std::uint32_t layer, std::uint32_t mask)
    {
        std::vector<kw::Entity> entities;

        for (const auto& made : level.colliders) {
            const auto entity = world.create();

            world.add<Transform3D>(entity, made.transform);
            world.add<Collider3D>(entity, made.collider.onLayer(layer, mask));
            entities.push_back(entity);
        }
        return entities;
    }

}
