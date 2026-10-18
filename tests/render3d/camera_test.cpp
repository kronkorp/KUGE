extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Components3D.hpp"
#include "Transform3D.hpp"
#include "fixture.hpp"
#include <cmath>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using render3d::Fixture;

    bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

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
    Assert(near(camera(fx).yaw, 0.25f), "100 pixels to the right: 0.25 rad");
    Assert(near(camera(fx).pitch, 0.1f), "40 pixels up: looks up");
    Assert(near(fx.world().get<kuge::CameraRig>(entity).yaw, 0.25f), "the game reads it in the rig");
    fx.engine.step(0.0);
    Assert(near(camera(fx).yaw, 0.25f), "a frame without moving does not turn it again");
}

Test(camera, pitch_stops_before_the_pole)
{
    Fixture fx;

    spawn(fx, 0.0f, 0.0f, 0.0f);
    moveMouse(fx, 0.0f, -100000.0f);
    fx.engine.step(0.0);
    Assert(near(camera(fx).pitch, 1.5f), "not straight up");
    moveMouse(fx, 0.0f, 100000.0f);
    fx.engine.step(0.0);
    Assert(near(camera(fx).pitch, -1.5f), "not straight down");
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
