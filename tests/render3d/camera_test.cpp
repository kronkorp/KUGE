extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Components3D.hpp"
#include "Picking.hpp"
#include "Transform3D.hpp"
#include "fixture.hpp"
#include <cmath>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using render3d::Fixture;

    bool near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) < tolerance; }

    kw::Entity spawn(Fixture& fx, float x, float y, float z, kuge::CameraRig rig = {})
    {
        kw::World& world = fx.world();
        const kw::Entity entity = world.create();
        kuge::Transform3D transform;

        transform.translation = {x, y, z};
        world.add<kuge::Transform3D>(entity, transform);
        world.add<kuge::CameraRig>(entity, rig);
        return entity;
    }

    void moveMouse(Fixture& fx, float dx, float dy)
    {
        fx.dummy.input->push(kuge::MouseMoveEvent{{10.0f, 10.0f}, {dx, dy}});
    }

    const k3::Camera& camera(Fixture& fx) { return fx.world().getResource<k3::Camera>(); }
}

Test(camera, first_person_is_at_the_eyes)
{
    Fixture fx;

    spawn(fx, 3.0f, 0.0f, 2.0f);
    fx.engine.step(0.0);
    Assert(near(camera(fx).position.x, 3.0f) && near(camera(fx).position.y, 1.7f) && near(camera(fx).position.z, 2.0f),
        "at the entity, 1.7 m above");
}

Test(camera, the_mouse_turns_the_view)
{
    Fixture fx;
    const kw::Entity entity = spawn(fx, 0.0f, 0.0f, 0.0f);

    moveMouse(fx, 100.0f, -40.0f);
    fx.engine.step(0.0);
    Assert(near(camera(fx).yaw, 0.13f), "100 pixels to the right: 0.13 rad (0.0013 per pixel): %f", camera(fx).yaw);
    Assert(near(camera(fx).pitch, 0.052f), "40 pixels up: looks up: %f", camera(fx).pitch);
    Assert(near(fx.world().get<kuge::CameraRig>(entity).yaw, 0.13f), "the game reads it in the rig");
    fx.engine.step(0.0);
    Assert(near(camera(fx).yaw, 0.13f), "a frame without moving does not turn it again");
}

Test(camera, pitch_stops_before_the_pole)
{
    Fixture fx;

    spawn(fx, 0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 10; ++i) {
        moveMouse(fx, 0.0f, -250.0f);
        fx.engine.step(0.0);
    }
    Assert(near(camera(fx).pitch, 1.5f), "not straight up");
    for (int i = 0; i < 20; ++i) {
        moveMouse(fx, 0.0f, 250.0f);
        fx.engine.step(0.0);
    }
    Assert(near(camera(fx).pitch, -1.5f), "not straight down");
}

Test(camera, a_glitch_is_not_a_hand)
{
    Fixture fx;

    spawn(fx, 0.0f, 0.0f, 0.0f);
    moveMouse(fx, 100000.0f, 0.0f);      // (a jump of the mouse: the cursor brought to the middle of the window...)
    fx.engine.step(0.0);
    Assert(near(camera(fx).yaw, 250.0f * 0.0013f), "no more than 250 pixels count in a frame: %f", camera(fx).yaw);
}

Test(camera, orbit_stays_behind)
{
    Fixture fx;
    kuge::CameraRig rig;

    rig.mode = kuge::CameraRig::Mode::Orbit;
    rig.offset = {0.0f, 0.0f, 0.0f};
    rig.distance = 5.0f;
    const kw::Entity entity = spawn(fx, 1.0f, 2.0f, 3.0f, rig);

    fx.engine.step(0.0);
    Assert(near(camera(fx).position.x, 1.0f) && near(camera(fx).position.y, 2.0f) && near(camera(fx).position.z, 8.0f),
        "looking toward -z: 5 m behind, on +z");
    fx.world().get<kuge::CameraRig>(entity).yaw = 1.5707964f;   // looking toward +x
    fx.engine.step(0.0);
    Assert(near(camera(fx).position.x, -4.0f) && near(camera(fx).position.z, 3.0f), "a quarter turn: behind, on -x");
}

Test(camera, look_off_keeps_the_view)
{
    Fixture fx;
    kuge::CameraRig rig;

    rig.look = false;
    rig.yaw = 0.5f;
    spawn(fx, 0.0f, 0.0f, 0.0f, rig);
    moveMouse(fx, 100.0f, 100.0f);
    fx.engine.step(0.0);
    Assert(near(camera(fx).yaw, 0.5f) && near(camera(fx).pitch, 0.0f), "the mouse is the game's");
}

Test(camera, the_first_rig_wins)
{
    Fixture fx;
    const kw::Entity first = spawn(fx, 1.0f, 0.0f, 0.0f);

    spawn(fx, 50.0f, 0.0f, 0.0f);
    (void)first;
    fx.engine.step(0.0);
    Assert(near(camera(fx).position.x, 1.0f), "the lowest entity");
}

Test(camera, it_follows_between_ticks)
{
    Fixture fx;
    const kw::Entity entity = spawn(fx, 10.0f, 0.0f, 0.0f);
    kuge::PreviousTransform3D previous;

    previous.value.translation = {0.0f, 0.0f, 0.0f};
    fx.world().add<kuge::PreviousTransform3D>(entity, previous);
    fx.engine.step(0.5 / 60.0);
    Assert(camera(fx).position.x > 0.0f && camera(fx).position.x < 10.0f, "drawn between the two, so is the view: %f", camera(fx).position.x);
}

Test(camera, the_ray_under_a_pixel)
{
    k3::Camera view;
    const kuge::Vec2 screen{800.0f, 600.0f};

    view.position = {1.0f, 2.0f, 3.0f};
    view.lookAt({1.0f, 2.0f, -7.0f});               // toward -z
    const auto middle = kuge::screenRay(view, {399.5f, 299.5f}, screen);

    Assert(near(middle.origin.x, 1.0f) && near(middle.origin.z, 3.0f), "from the eye");
    Assert(near(middle.direction.z, -1.0f) && near(middle.direction.x, 0.0f) && near(middle.direction.y, 0.0f), "the middle looks ahead");
    const auto right = kuge::screenRay(view, {800.0f - 0.5f, 299.5f}, screen);
    const auto top = kuge::screenRay(view, {399.5f, -0.5f}, screen);
    const float half = std::tan(view.fovY * 0.5f);

    Assert(near(right.direction.x / -right.direction.z, half * 800.0f / 600.0f, 1e-3f), "the right edge: the half of the field of view, wider by the aspect: %f", right.direction.x / -right.direction.z);
    Assert(near(top.direction.y / -top.direction.z, half, 1e-3f) && top.direction.y > 0.0f, "the top edge is up");
    const auto corner = kuge::screenRay(view, {0.0f, 0.0f}, screen);

    Assert(near(std::sqrt(corner.direction.x * corner.direction.x + corner.direction.y * corner.direction.y + corner.direction.z * corner.direction.z), 1.0f), "a unit vector");
    Assert(corner.direction.x < 0.0f && corner.direction.y > 0.0f, "the top left looks left and up");
    view.projection = k3::Projection::Orthographic;
    view.orthoHeight = 10.0f;
    const auto flat = kuge::screenRay(view, {0.0f, 299.5f}, screen);

    Assert(near(flat.direction.z, -1.0f) && near(flat.origin.x, 1.0f - 5.0f * 800.0f / 600.0f, 0.01f), "orthographic: parallel rays, starting across the view: %f", flat.origin.x);
}
