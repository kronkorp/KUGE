extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Engine.hpp"
#include "Physics3D.hpp"
#include "PoseAnimator.hpp"
#include "StateMachine.hpp"
#include <cmath>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using Vec3 = k3::Math::Vector3f;

    bool near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) <= tolerance; }

    k3::AnimationChannel slide(int node, Vec3 from, Vec3 to, float duration)
    {
        k3::AnimationChannel channel;

        channel.node = node;
        channel.path = k3::AnimationChannel::Path::Translation;
        channel.times = {0.0f, duration};
        channel.values = {{from.x, from.y, from.z, 0.0f}, {to.x, to.y, to.z, 0.0f}};
        return channel;
    }

    //  root (0)
    //   ├─ spine (1) ── arm_l (2), arm_r (3), head (5)
    //   └─ leg_l (4)
    // Run: the left leg goes 1 m forward over a second, and the left arm 1 m to the side. Fire: the right arm goes up 1 m
    // in half a second. Crouch: the spine goes down 0.5 m in one second.
    std::shared_ptr<k3::AnimatedModel> makeModel()
    {
        auto model = std::make_shared<k3::AnimatedModel>();
        const auto node = [&](const char* name, int parent, Vec3 at) {
            k3::Node n;

            n.name = name;
            n.parent = parent;
            n.local.translation = at;
            model->nodes.push_back(n);
        };

        node("root", -1, {0.0f, 0.0f, 0.0f});
        node("spine", 0, {0.0f, 1.0f, 0.0f});
        node("arm_l", 1, {-0.3f, 0.5f, 0.0f});
        node("arm_r", 1, {0.3f, 0.5f, 0.0f});
        node("leg_l", 0, {-0.1f, 0.0f, 0.0f});
        node("head", 1, {0.0f, 0.65f, 0.0f});
        k3::Animation run{"Run", 1.0f, {slide(4, {-0.1f, 0.0f, 0.0f}, {-0.1f, 0.0f, 1.0f}, 1.0f), slide(2, {-0.3f, 0.5f, 0.0f}, {0.7f, 0.5f, 0.0f}, 1.0f)}};
        k3::Animation fire{"Fire", 0.5f, {slide(3, {0.3f, 0.5f, 0.0f}, {0.3f, 1.5f, 0.0f}, 0.5f)}};
        k3::Animation crouch{"Crouch", 1.0f, {slide(1, {0.0f, 1.0f, 0.0f}, {0.0f, 0.5f, 0.0f}, 1.0f)}};
        k3::Animation idle{"Idle", 1.0f, {slide(1, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 1.0f)}};

        model->animations = {idle, run, fire, crouch};
        return model;
    }

    const char* GRAPH = R"(
        # a character
        state idle   Idle   loop
        state run    Run    loop
        state fire   Fire   once
        state crouch Crouch once
        transition fire  idle 0.1 when end
        transition *     fire 0.05 when shoot == 1
        transition idle  crouch 0.0 when crouch == 1
        transition idle  run  0.15 when speed > 0.5
        transition run   idle 0.20 when speed <= 0.5
        start idle
    )";

    std::shared_ptr<const kuge::StateMachineDef> graphDef()
    {
        return std::make_shared<const kuge::StateMachineDef>(kuge::parseStateMachine(GRAPH));
    }
}

Test(pose, two_keys_sampled_in_the_middle)
{
    const auto model = makeModel();
    const kuge::Pose half = kuge::samplePose(*model, 1, 0.5f);

    Assert(near(half[4].translation.z, 0.5f), "the leg is half way: %f", half[4].translation.z);
    Assert(near(half[2].translation.x, 0.2f), "the arm too");
    Assert(near(half[1].translation.y, 1.0f), "what the clip does not move stays at rest");
    Assert(near(kuge::samplePose(*model, 1, 9.0f)[4].translation.z, 1.0f), "past the end: the last pose");
    Assert(near(kuge::samplePose(*model, -1, 0.5f)[4].translation.z, 0.0f), "no clip: the rest pose");
}

Test(pose, a_blend_of_a_quarter)
{
    const auto model = makeModel();
    const kuge::Pose from = kuge::samplePose(*model, 1, 0.0f);
    const kuge::Pose to = kuge::samplePose(*model, 1, 1.0f);

    Assert(near(kuge::blend(from, to, 0.25f)[4].translation.z, 0.25f), "a quarter of the way");
    Assert(near(kuge::blend(from, to, 0.0f)[4].translation.z, 0.0f) && near(kuge::blend(from, to, 1.0f)[4].translation.z, 1.0f), "the ends");
    Assert(near(kuge::blend(from, to, 7.0f)[4].translation.z, 1.0f), "kept between 0 and 1");
}

Test(pose, bone_masks_layer_clips)
{
    const auto model = makeModel();
    const std::vector<std::string> upper{"spine"};
    const kuge::BoneMask mask = kuge::boneMask(*model, upper);

    Assert(mask[1] == 1.0f && mask[2] == 1.0f && mask[3] == 1.0f && mask[5] == 1.0f, "the spine and what hangs from it");
    Assert(mask[0] == 0.0f && mask[4] == 0.0f, "not the root and the leg");
    AssertEq(kuge::boneMask(*model, upper, false)[2], 0.0f, "without the children: only the spine");
    // The legs run, the arm fires
    const kuge::Pose legs = kuge::samplePose(*model, 1, 1.0f);
    const kuge::Pose arms = kuge::samplePose(*model, 2, 0.5f);
    const kuge::Pose both = kuge::layer(legs, arms, mask);

    Assert(near(both[4].translation.z, 1.0f), "the leg runs");
    Assert(near(both[3].translation.y, 1.5f), "the right arm fires");
    Assert(near(both[2].translation.x, -0.3f), "the left arm is as the Fire clip leaves it (at rest), not as Run does");
    Assert(near(kuge::layer(legs, arms, mask, 0.5f)[3].translation.y, 1.0f), "half the layer, half the way");
}

Test(pose, the_animator_crossfades)
{
    kuge::PoseAnimator animator(makeModel());

    Assert(animator.play("Run", {.fade = 0.0f}), "it plays");
    Assert(!animator.play("Fly"), "a clip that is not there");
    animator.update(0.5f);
    Assert(near(animator.pose()[4].translation.z, 0.5f), "half way");
    animator.play("Crouch", {.fade = 1.0f});
    animator.update(0.5f);
    // Half a fade: the run (now at 1.0 s, the last of its loop: 0 or 1) mixed with the crouch (half)
    Assert(near(animator.pose()[1].translation.y, 0.875f), "the spine is between rest (1.0) and the crouch (0.75): %f", animator.pose()[1].translation.y);
    animator.update(2.0f);
    Assert(near(animator.pose()[4].translation.z, 0.0f), "the fade is over: only the crouch moves the pose (the leg is at rest)");
    AssertEq(std::string(animator.clip()), std::string("Crouch"), "it says what plays");
    animator.play("Fire", {.loop = false, .fade = 0.0f});
    Assert(!animator.finished(), "not yet");
    animator.update(0.6f);
    Assert(animator.finished() && near(animator.pose()[3].translation.y, 1.5f), "a clip that does not loop stops on its last pose");
}

Test(graph, goes_where_the_parameters_say)
{
    kuge::AnimationStateMachine machine(graphDef(), makeModel());
    const auto start = machine.update(0.0f);

    Assert(start && start->state->name == "idle" && start->fade == 0.0f, "the first call gives the start state");
    Assert(!machine.update(0.1f), "nothing holds: it stays");
    machine.set("speed", 3.0f);
    const auto run = machine.update(0.1f);

    Assert(run && run->state->clip == "Run" && near(run->fade, 0.15f), "speed > 0.5: run, with the fade of the file");
    machine.set("speed", 0.0f);
    const auto back = machine.update(0.1f);

    Assert(back && back->state->name == "idle", "and back");
    machine.set("shoot", 1.0f);
    const auto fire = machine.update(0.1f);

    Assert(fire && fire->state->name == "fire", "from any state (*)");
    machine.set("shoot", 0.0f);
    Assert(!machine.update(0.2f), "fire lasts 0.5 s: not over");
    const auto over = machine.update(0.4f);

    Assert(over && over->state->name == "idle", "when end: back to idle after the clip: %s", machine.state().c_str());
    AssertEq(machine.get("nothing"), 0.0f, "a parameter that was never set is 0");
}

Test(graph, the_file_says_where_it_is_wrong)
{
    const auto message = [](const char* text) {
        try { kuge::parseStateMachine(text); } catch (const kuge::AnimationGraphError& error) { return std::string(error.what()); }
        return std::string("accepted");
    };

    Assert(message("state a A\nstate b\n").find("line 2") != std::string::npos, "a state without a clip: %s", message("state a A\nstate b\n").c_str());
    Assert(message("state a A\ntransition a b 0.1\n").find("not there") != std::string::npos, "a transition to nowhere");
    Assert(message("state a A\nstate a B\n").find("already") != std::string::npos, "the same state twice");
    Assert(message("state a A\ntransition a a 0.1 when x ~ 1\n").find("line 2") != std::string::npos, "a bad operator");
    Assert(message("state a A\ntransition a a fast\n").find("not a number") != std::string::npos, "a bad fade");
    Assert(message("# nothing\n").find("no state") != std::string::npos, "no state");
    Assert(message("state a A\nstart b\n").find("start state") != std::string::npos, "a start that is not a state");
    Assert(message("state a A\njump a\n").find("line 2") != std::string::npos, "an unknown word");
    Assert(message("state a A\nstate b B once speed 2\ntransition * a 0 when end\n") == "accepted", "a good file");
}

Test(graph, the_room_places_the_bones)
{
    const auto model = makeModel();
    kuge::AnimationGraph graph(graphDef(), model);
    const std::vector<std::string> names{"head", "arm_l", "nothing"};

    graph.update(0.0f);
    const auto standing = kuge::bonePlaces(*model, graph.pose(), names);

    AssertEq(standing.size(), 2u, "a bone that is not there is left out");
    Assert(standing[0].name == "head" && near(standing[0].translation.y, 1.65f), "the head at rest: 1 + 0.65 (y = %f)", standing[0].translation.y);
    graph.set("speed", 2.0f);
    for (int i = 0; i < 30; ++i) {
        graph.update(1.0f / 60.0f);
    }
    AssertEq(graph.state(), std::string("run"), "it runs");
    const auto running = kuge::bonePlaces(*model, graph.pose(), names);

    Assert(running[1].translation.x > standing[1].translation.x + 0.1f, "the left arm swings out as the clip says: %f", running[1].translation.x);
}

namespace
{
    struct Arena : kuge::Scene
    {
        using kuge::Scene::world;

        void onEnter(void) override { kuge::installPhysics3D(setup()); }
    };
}

Test(graph, hitboxes_follow_the_pose)
{
    // The room: an animation graph gives the bones, the hitboxes read them, a shot finds the head where the animation put it
    const auto model = makeModel();
    kuge::AnimationGraph graph(graphDef(), model);
    kuge::Engine engine;
    const std::vector<std::string> names{"head"};

    engine.scenes().change<Arena>();
    engine.step(0.0);
    auto& world = static_cast<Arena*>(engine.scenes().top())->world();
    const kw::Entity player = world.create();
    kuge::Hitboxes boxes;

    boxes.parts.push_back({"head", kuge::Hitbox::Shape::Sphere, {0.15f, 0.0f, 0.0f}, {}, "head", 2.0f});
    world.add<kuge::Hitboxes>(player, boxes);
    world.add<kuge::Transform3D>(player, kuge::Transform3D{});
    engine.step(1.0 / 60.0);
    auto& physics = world.getResource<kuge::Physics3D>();
    const auto shootAt = [&](float height) {
        return physics.shoot(world, {0.0f, height, 5.0f}, {0.0f, 0.0f, -1.0f}, 20.0f);
    };

    graph.update(0.0f);
    world.get<kuge::Hitboxes>(player).bones = kuge::bonePlaces(*model, graph.pose(), names);
    Assert(shootAt(1.65f) && !shootAt(1.15f), "standing: the head is at 1.65 m, not at 1.15");
    graph.set("crouch", 1.0f);
    for (int i = 0; i < 80; ++i) {
        graph.update(1.0f / 60.0f);   // a second of the Crouch clip: the spine is 0.5 m lower
    }
    AssertEq(graph.state(), std::string("crouch"), "the graph crouches");
    world.get<kuge::Hitboxes>(player).bones = kuge::bonePlaces(*model, graph.pose(), names);
    Assert(!shootAt(1.65f) && shootAt(1.15f), "crouched: the head went down with its bone");
}
