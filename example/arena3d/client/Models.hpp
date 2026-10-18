#pragma once
// The models of Arena, made of boxes in code: no file to ship. (A game loads its own with
// Render3DModule::models() for .obj and animatedModels() for .glb.)

#include <kronk3d/scene/Material.hpp>
#include <kronk3d/scene/Mesh.hpp>
#include <kronk3d/scene/Model.hpp>
#include <memory>

namespace arena
{

    using Vec3 = k3::Math::Vector3f;

    // Adds a box to a model: six faces with their own normals, wound counter-clockwise seen from outside
    inline void addBox(k3::Model& model, Vec3 center, Vec3 size, k3::Math::Color color, bool unlit = false)
    {
        auto mesh = std::make_shared<k3::Mesh>();
        auto material = std::make_shared<k3::Material>();
        const Vec3 half{size.x / 2, size.y / 2, size.z / 2};
        // For each face: its normal, then two edges (u x v = normal)
        const Vec3 faces[6][3] = {
            {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
            {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
            {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
            {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
            {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
            {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
        };

        for (const auto& face : faces) {
            const std::uint32_t first = static_cast<std::uint32_t>(mesh->positions.size());
            const Vec3 n = face[0];
            const Vec3 u{face[1].x * half.x, face[1].y * half.y, face[1].z * half.z};
            const Vec3 v{face[2].x * half.x, face[2].y * half.y, face[2].z * half.z};
            const Vec3 c{center.x + n.x * half.x, center.y + n.y * half.y, center.z + n.z * half.z};

            for (const auto& corner : {std::pair{-1.0f, -1.0f}, std::pair{1.0f, -1.0f}, std::pair{1.0f, 1.0f}, std::pair{-1.0f, 1.0f}}) {
                mesh->positions.push_back({c.x + u.x * corner.first + v.x * corner.second, c.y + u.y * corner.first + v.y * corner.second,
                    c.z + u.z * corner.first + v.z * corner.second});
                mesh->normals.push_back(n);
            }
            mesh->indices.insert(mesh->indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
        }
        material->diffuse = color;
        material->unlit = unlit;
        model.primitives.push_back({mesh, material});
    }

    // Pure colors (0 or 1 in each channel) come out exactly as 0 or 255 whatever the lighting: a script can look for them
    inline std::shared_ptr<const k3::Model> coinModel(void)
    {
        auto model = std::make_shared<k3::Model>();

        addBox(*model, {0, 0, 0}, {0.55f, 0.55f, 0.12f}, {1.0f, 1.0f, 0.0f, 1.0f}, true);
        addBox(*model, {0, 0, 0}, {0.12f, 0.55f, 0.55f}, {1.0f, 1.0f, 0.0f, 1.0f}, true);
        return model;
    }

    // A body, a head and a visor, standing on its feet (the entity is the middle of the capsule)
    inline std::shared_ptr<const k3::Model> playerModel(k3::Math::Color color)
    {
        auto model = std::make_shared<k3::Model>();

        addBox(*model, {0, -0.1f, 0}, {0.7f, 1.2f, 0.45f}, color, true);
        addBox(*model, {0, 0.7f, 0}, {0.45f, 0.4f, 0.45f}, color, true);
        addBox(*model, {0, 0.72f, -0.24f}, {0.3f, 0.12f, 0.06f}, {0.0f, 0.0f, 0.0f, 1.0f}, true);
        return model;
    }

    inline std::shared_ptr<const k3::Model> crateModel(void)
    {
        auto model = std::make_shared<k3::Model>();

        addBox(*model, {0, 0, 0}, {1.0f, 1.0f, 1.0f}, {0.55f, 0.32f, 0.12f, 1.0f});
        return model;
    }

    inline std::shared_ptr<const k3::Model> floorModel(void)
    {
        auto model = std::make_shared<k3::Model>();

        addBox(*model, {0, 0, 0}, {30.0f, 1.0f, 30.0f}, {0.18f, 0.2f, 0.22f, 1.0f});
        return model;
    }

    inline std::shared_ptr<const k3::Model> wallModel(float length, bool alongX)
    {
        auto model = std::make_shared<k3::Model>();

        addBox(*model, {0, 0, 0}, alongX ? Vec3{length, 3.0f, 1.0f} : Vec3{1.0f, 3.0f, length}, {0.35f, 0.38f, 0.42f, 1.0f});
        return model;
    }

}
