extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Components3D.hpp"
#include "Transform3D.hpp"
#include "fixture.hpp"
#include <thread>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    const char* TRIANGLE = "v -1 -1 0\nv 1 -1 0\nv 0 1 0\nf 1 2 3\n";
    const char* QUAD = "v -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nf 1 2 3 4\n";
    const char* RED_QUAD =
        "mtllib red.mtl\nv -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvn 0 0 1\nusemtl red\n"
        "f 1//1 2//1 3//1 4//1\n";
    // One triangle (0 0 0, 1 0 0, 0 1 0) in an embedded buffer
    const char* GLTF =
        R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],)"
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],)"
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],)"
        R"("bufferViews":[{"buffer":0,"byteLength":36}],)"
        R"("buffers":[{"byteLength":36,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA"}]})";

    using render3d::Fixture;
    using render3d::Folder;
}

Test(models, obj_is_loaded_and_shared)
{
    const Folder folder("models_obj");
    Fixture fx;
    const auto file = folder.write("triangle.obj", TRIANGLE);
    const auto model = fx.models->models().load(file);

    AssertEq(model->primitives.size(), 1u, "one mesh");
    AssertEq(model->primitives[0].mesh->triangleCount(), 1u, "a triangle");
    Assert(fx.models->models().load(file) == model, "the same file, the same model");
    Assert(fx.world().getResource<kuge::Ref<kuge::AssetManager<k3::Model>>>().get().load(file) == model,
        "the scenes reach it too");
}

Test(models, a_bad_file_is_refused)
{
    const Folder folder("models_bad");
    Fixture fx;
    std::string message;

    try { fx.models->models().load(folder.path / "nothing.obj"); } catch (const kuge::ModelError& error) { message = error.what(); }
    Assert(message.find("nothing.obj") != std::string::npos, "the message names the file: '%s'", message.c_str());
    message.clear();
    try { fx.models->animatedModels().load(folder.write("broken.gltf", "{ not glTF")); }
    catch (const kuge::ModelError& error) { message = error.what(); }
    Assert(message.find("broken.gltf") != std::string::npos, "a broken glTF too: '%s'", message.c_str());
    AssertEq(fx.models->models().loaded(), 0u, "nothing is kept");
}

Test(models, gltf_is_loaded)
{
    const Folder folder("models_gltf");
    Fixture fx;
    const auto model = fx.models->animatedModels().load(folder.write("triangle.gltf", GLTF));

    AssertEq(model->primitives.size(), 1u, "one mesh");
    AssertEq(model->primitives[0].mesh->vertexCount(), 3u, "three vertices");
    AssertEq(model->animations.size(), 0u, "no clip");
}

Test(models, loaded_in_the_background)
{
    const Folder folder("models_async");
    Fixture fx;
    const auto file = folder.write("quad.obj", QUAD);
    const auto ticket = fx.models->models().loadAsync(file);

    for (int i = 0; i < 2000 && !ticket->done(); ++i) {
        fx.engine.step(0.0);   // (the next loop makes the model)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Assert(ticket->state() == decltype(ticket)::element_type::State::Ready, "ready: '%s'", ticket->error().c_str());
    AssertEq(ticket->asset()->primitives[0].mesh->triangleCount(), 2u, "a quad is two triangles");
    Assert(fx.models->models().load(file) == ticket->asset(), "load() gives the same one");
}

Test(models, a_changed_file_reloads)
{
    const Folder folder("models_reload");
    Fixture fx;
    const auto file = folder.write("shape.obj", TRIANGLE);
    const auto model = fx.models->models().load(file);

    AssertEq(fx.models->reloadAssets(), 0u, "nothing changed");
    folder.write("shape.obj", QUAD, 1);
    AssertEq(fx.models->reloadAssets(), 1u, "reloaded");
    AssertEq(model->primitives[0].mesh->triangleCount(), 2u, "whoever holds the model sees the quad");
    folder.write("shape.obj", "f 1 2 3\n", 2);
    AssertEq(fx.models->reloadAssets(), 0u, "a file that cannot be read...");
    AssertEq(model->primitives[0].mesh->triangleCount(), 2u, "...leaves the model as it was");
}

Test(models, watching_reloads_by_itself)
{
    const Folder folder("models_watch");
    Fixture fx(kuge::Render3DConfig{.watchAssets = 0.001});
    const auto model = fx.models->models().load(folder.write("shape.obj", TRIANGLE));

    folder.write("shape.obj", QUAD, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    fx.engine.step(0.0);
    AssertEq(model->primitives[0].mesh->triangleCount(), 2u, "reloaded at the start of the loop");
}

Test(models, a_loaded_model_is_drawn)
{
    const Folder folder("models_draw");
    Fixture fx;

    folder.write("red.mtl", "newmtl red\nKd 1 0 0\nillum 1\n");
    kw::World& world = fx.world();
    auto& camera = world.getResource<k3::Camera>();
    const kw::Entity model = world.create();
    const kw::Entity sun = world.create();

    camera.position = {0.f, 0.f, 5.f};
    camera.lookAt({0.f, 0.f, 0.f});
    world.getResource<k3::Environment>().clearColor = {0.f, 0.f, 0.f, 1.f};
    world.add<kuge::Transform3D>(model, kuge::Transform3D{});
    world.add<kuge::Model3D>(model, kuge::Model3D{fx.models->models().load(folder.write("red.obj", RED_QUAD))});
    world.add<kuge::Light3D>(sun, kuge::Light3D{k3::Light::directional({0.f, 0.f, -1.f})});
    fx.engine.step(0.0);

    for (const auto& call : fx.dummy.renderer->lastFrame()) {
        if (call.kind == kuge::DummyRenderer::Call::Kind::Texture && call.destination.w == 64.0f) {
            const std::uint8_t* pixel = fx.dummy.renderer->texture(call.texture.texture)->rgba.data() + (24 * 64 + 32) * 4;

            Assert(pixel[0] > 150 && pixel[1] < 40 && pixel[2] < 40, "red at the center: %d %d %d", pixel[0], pixel[1], pixel[2]);
            return;
        }
    }
    Assert(false, "the 3D image was drawn");
}
