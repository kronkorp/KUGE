#pragma once

#include "Physics3DComponents.hpp"
#include <kronk3d/scene/Animation.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kuge
{

    //! The local transform of every node of a model (k3::AnimatedModel::nodes), at one instant
    using Pose = std::vector<k3::Transform>;

    //! How much of an overlaid pose each node takes, 0 to 1 (one per node): legs run, arms fire
    using BoneMask = std::vector<float>;

    //! The rest pose of the model
    Pose restPose(const k3::AnimatedModel& model);

    //! The pose of a clip (an index in model.animations, -1: the rest pose) at a time, clamped to its duration.
    //! No window and no drawing: the room uses it to place hitboxes.
    Pose samplePose(const k3::AnimatedModel& model, int clip, float time);

    //! From `a` (k = 0) to `b` (k = 1): translations and scales in a line, rotations by the short way. Same size.
    Pose blend(const Pose& a, const Pose& b, float k);

    //! The nodes named in `roots` and, with `withChildren`, everything under them, at 1; the others at 0.
    //! A name that is not in the model is ignored.
    BoneMask boneMask(const k3::AnimatedModel& model, std::span<const std::string> roots, bool withChildren = true);

    //! `over` on `base`, node by node: each node moves from `base` to `over` by mask * k. A layered blend.
    Pose layer(const Pose& base, const Pose& over, const BoneMask& mask, float k = 1.0f);

    //! Where the named nodes are in the space of the model for a pose, as the bones a Hitboxes component wants
    //! (translation and rotation; the scale is dropped). Names that are not nodes are left out.
    std::vector<BonePlace> bonePlaces(const k3::AnimatedModel& model, const Pose& pose, std::span<const std::string> names);

}
