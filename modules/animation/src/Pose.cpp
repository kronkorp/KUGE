#include "Pose.hpp"
#include <algorithm>

kuge::Pose kuge::restPose(const k3::AnimatedModel& model)
{
    Pose pose;

    pose.reserve(model.nodes.size());
    for (const k3::Node& node : model.nodes) {
        pose.push_back(node.local);
    }
    return pose;
}

kuge::Pose kuge::samplePose(const k3::AnimatedModel& model, int clip, float time)
{
    Pose pose = restPose(model);

    if (clip >= 0 && static_cast<std::size_t>(clip) < model.animations.size()) {
        model.animations[static_cast<std::size_t>(clip)].sample(time, pose);
    }
    return pose;
}

kuge::Pose kuge::blend(const Pose& a, const Pose& b, float k)
{
    Pose result = a;
    const std::size_t count = std::min(a.size(), b.size());
    const float t = std::clamp(k, 0.0f, 1.0f);

    for (std::size_t i = 0; i < count; ++i) {
        result[i] = k3::Transform::mix(a[i], b[i], t);
    }
    return result;
}

kuge::BoneMask kuge::boneMask(const k3::AnimatedModel& model, std::span<const std::string> roots, bool withChildren)
{
    BoneMask mask(model.nodes.size(), 0.0f);

    // Parents come first in the model (k3::Node::parent is lower than the node's own index): one pass
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        const bool named = std::find(roots.begin(), roots.end(), model.nodes[i].name) != roots.end();
        const int parent = model.nodes[i].parent;

        if (named || (withChildren && parent >= 0 && mask[static_cast<std::size_t>(parent)] > 0.0f)) {
            mask[i] = 1.0f;
        }
    }
    return mask;
}

kuge::Pose kuge::layer(const Pose& base, const Pose& over, const BoneMask& mask, float k)
{
    Pose result = base;
    const std::size_t count = std::min({base.size(), over.size(), mask.size()});

    for (std::size_t i = 0; i < count; ++i) {
        const float weight = std::clamp(mask[i] * k, 0.0f, 1.0f);

        if (weight > 0.0f) {
            result[i] = k3::Transform::mix(base[i], over[i], weight);
        }
    }
    return result;
}

std::vector<kuge::BonePlace> kuge::bonePlaces(const k3::AnimatedModel& model, const Pose& pose, std::span<const std::string> names)
{
    std::vector<BonePlace> places;
    const std::vector<k3::Math::Matrix4> globals = model.globalTransforms(pose);

    for (const std::string& name : names) {
        for (std::size_t i = 0; i < model.nodes.size() && i < globals.size(); ++i) {
            if (model.nodes[i].name == name) {
                const k3::Transform placed = k3::Transform::fromMatrix(globals[i]);

                places.push_back({name, placed.translation, placed.rotation});
                break;
            }
        }
    }
    return places;
}
