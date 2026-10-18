#pragma once
// What the client draws with: the textures and the character, loaded from the assets folder, the level made of
// boxes (the pieces of the map, which are also its colliders), and the few things that are made in code.

#include "Map.hpp"
#include <cmath>
#include <filesystem>
#include <kronk3d/scene/Animation.hpp>
#include <kronk3d/scene/Material.hpp>
#include <kronk3d/scene/Mesh.hpp>
#include <kronk3d/scene/Model.hpp>
#include <kronk3d/scene/Texture.hpp>
#include <map>
#include <memory>
#include <stdexcept>

namespace arena
{

    //! Pure channels (0 or 1) of a player: a shirt that is exactly this colour on the screen, whatever the light
    inline k3::Math::Color teamColor(std::uint32_t player)
    {
        static const k3::Math::Color colors[4] = {{0, 1, 1, 1}, {1, 0, 1, 1}, {0, 1, 0, 1}, {1, 0.5f, 0, 1}};

        return colors[(player + 3) % 4];       // (the first player, number 1, is cyan)
    }

    inline std::shared_ptr<k3::Texture> loadTexture(const std::filesystem::path& file)
    {
        auto texture = k3::Texture::load(file);

        if (!texture) {
            throw std::runtime_error("cannot read the texture " + file.string());
        }
        return texture;
    }

    // A box centred on the origin: six faces with their own normals, wound counter-clockwise seen from outside, and
    // texture coordinates that repeat the picture every `tile` meters (so that big pieces are not stretched)
    inline std::shared_ptr<k3::Mesh> boxMesh(Vec3 size, float tile)
    {
        auto mesh = std::make_shared<k3::Mesh>();
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
            const float lengthU = 2.0f * Vec3::length(u);
            const float lengthV = 2.0f * Vec3::length(v);

            for (const auto& corner : {std::pair{-1.0f, -1.0f}, std::pair{1.0f, -1.0f}, std::pair{1.0f, 1.0f}, std::pair{-1.0f, 1.0f}}) {
                mesh->positions.push_back({n.x * half.x + u.x * corner.first + v.x * corner.second,
                    n.y * half.y + u.y * corner.first + v.y * corner.second, n.z * half.z + u.z * corner.first + v.z * corner.second});
                mesh->normals.push_back(n);
                mesh->uvs.push_back({(corner.first + 1.0f) / 2.0f * lengthU / tile, (1.0f - (corner.second + 1.0f) / 2.0f) * lengthV / tile});
            }
            mesh->indices.insert(mesh->indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
        }
        return mesh;
    }

    //! The textures of the three kinds of surface of the level
    struct LevelTextures
    {
        std::map<Surface, std::shared_ptr<k3::Texture>> byKind;

        explicit LevelTextures(const std::filesystem::path& assets)
        {
            byKind[Surface::Floor] = loadTexture(assets / "floor.png");
            byKind[Surface::Wall] = loadTexture(assets / "wall.png");
            byKind[Surface::Platform] = loadTexture(assets / "platform.png");
        }

        //! The model of a piece of the map: a box of its size with the texture of its surface
        std::shared_ptr<const k3::Model> modelOf(const Piece& piece) const
        {
            auto model = std::make_shared<k3::Model>();
            auto material = std::make_shared<k3::Material>();

            material->diffuseMap = byKind.at(piece.surface);
            material->specular = {0.04f, 0.04f, 0.04f, 1.0f};
            material->shininess = 8.0f;
            model->primitives.push_back({boxMesh(piece.size, 4.0f), material});
            return model;
        }
    };

    //! The robot as the game wants it, for one player: the file has a robot that is 2.12 m tall, faces +z, and
    //! stands on the origin; this one is as tall as the capsule of the physics (feet at its bottom), faces -z like the
    //! player, has the lights of the colour of the player (a pure colour, unlit: it is the same on the screen
    //! whatever the light), and a clip for dying (it falls on its back). The file's model is not changed.
    inline std::shared_ptr<const k3::AnimatedModel> makeRobot(const k3::AnimatedModel& base, k3::Math::Color color)
    {
        constexpr float FILE_HEIGHT = 2.12f;      // the robot of the file (its root has a scale of 0.3, which its skin undoes)
        constexpr float HEIGHT = 1.75f;
        const k3::Math::Vector4f facing{0.0f, 1.0f, 0.0f, 0.0f};     // half a turn around y
        auto copy = std::make_shared<k3::AnimatedModel>(base);
        std::shared_ptr<k3::Material> lights;
        int root = -1;

        for (std::size_t i = 0; i < copy->nodes.size(); ++i) {
            auto& node = copy->nodes[i];

            if (node.parent < 0) {
                // (the scale of the file stays: the inverse bind matrices of the skin cancel it, so the robot is the
                // size of its vertices, and the root only has to shrink it)
                node.local.scale = node.local.scale * (HEIGHT / FILE_HEIGHT);
                node.local.translation = {0.0f, -0.9f, 0.0f};
                node.local.rotation = facing;
                root = root < 0 ? static_cast<int>(i) : root;
            }
        }
        for (auto& primitive : copy->primitives) {
            if (primitive.material && primitive.material->name == "player_glow") {
                if (!lights) {
                    lights = std::make_shared<k3::Material>(*primitive.material);
                    lights->diffuse = color;
                    lights->unlit = true;
                }
                primitive.material = lights;
            }
        }
        if (root >= 0) {
            // It falls backward around its feet: a quarter turn around x, after the half turn that makes it face -z
            k3::Animation death;
            k3::AnimationChannel fall;

            death.name = "death";
            death.duration = 0.9f;
            fall.node = root;
            fall.path = k3::AnimationChannel::Path::Rotation;
            fall.interpolation = k3::AnimationChannel::Interpolation::Linear;
            fall.times = {0.0f, 0.6f, 0.9f};
            for (const float angle : {0.0f, 1.45f, 1.55f}) {
                fall.values.push_back(multiply({std::sin(angle / 2), 0.0f, 0.0f, std::cos(angle / 2)}, facing));
            }
            death.channels.push_back(fall);
            copy->animations.push_back(death);
        }
        return copy;
    }

    //! What the player holds in first person: a gun, in dark metal
    inline std::shared_ptr<const k3::Model> gunModel(void)
    {
        auto model = std::make_shared<k3::Model>();
        auto metal = std::make_shared<k3::Material>();
        auto grip = std::make_shared<k3::Material>();

        metal->diffuse = {0.09f, 0.09f, 0.1f, 1.0f};
        metal->specular = {0.4f, 0.4f, 0.4f, 1.0f};
        metal->shininess = 48.0f;
        grip->diffuse = {0.03f, 0.03f, 0.035f, 1.0f};
        model->primitives.push_back({boxMesh({0.07f, 0.09f, 0.36f}, 1.0f), metal});
        model->primitives.push_back({boxMesh({0.05f, 0.13f, 0.07f}, 1.0f), grip});
        // (the grip is under the body of the gun, near its back: moved in the vertices, a model has no offsets)
        for (auto& vertex : std::const_pointer_cast<k3::Mesh>(model->primitives[1].mesh)->positions) {
            vertex = {vertex.x, vertex.y - 0.1f, vertex.z + 0.1f};
        }
        // (the light of the muzzle: a pure colour that shows)
        auto light = std::make_shared<k3::Material>();

        light->diffuse = {1.0f, 0.3f, 0.0f, 1.0f};
        light->unlit = true;
        model->primitives.push_back({boxMesh({0.025f, 0.025f, 0.02f}, 1.0f), light});
        for (auto& vertex : std::const_pointer_cast<k3::Mesh>(model->primitives[2].mesh)->positions) {
            vertex = {vertex.x, vertex.y + 0.06f, vertex.z - 0.16f};
        }
        return model;
    }

    //! A bullet: long, thin, bright (a pure yellow), and facing where it goes (-z at rest)
    inline std::shared_ptr<const k3::Model> bulletModel(void)
    {
        auto model = std::make_shared<k3::Model>();
        auto material = std::make_shared<k3::Material>();

        material->diffuse = {1.0f, 1.0f, 0.0f, 1.0f};
        material->unlit = true;
        model->primitives.push_back({boxMesh({0.07f, 0.07f, 0.55f}, 1.0f), material});
        return model;
    }

}
