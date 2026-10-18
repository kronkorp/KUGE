#pragma once

#include <kronk3d/scene/Animator.hpp>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kuge
{

    //! Something that happens at a time of a clip ("step" when the foot touches the ground)
    struct Cue3D
    {
        float       time;   //!< Seconds since the clip began
        std::string name;
    };

    //! The cues of each clip, by the name of the clip
    using ClipCues = std::map<std::string, std::vector<Cue3D>, std::less<>>;

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Plays the clips of a glTF model on an entity that has a Transform3D (a component)
     *
     * It is kronk3d's k3::Animator (skeleton, clips, crossfades) and what KUGE adds to it: the clock of
     * the simulation (AnimateModels moves it forward at each tick), the cues of the clips (as
     * AnimationEvent, like the 2D ones) and a clip to go to when one that does not loop ends.
     *
     *     world.add<kuge::Animator3D>(e, kuge::Animator3D::of(model, "idle"));
     *     world.get<kuge::Animator3D>(e).play("run");   // a fade of 0.2 s from the clip that plays
     *     world.get<kuge::Animator3D>(e).play("attack", {.loop = false}, "idle");
     *
     * An entity with no Model3D is drawn by its Animator3D, and a model with no clip is drawn in its
     * rest pose. The pose changes at each tick, not at each frame.
     */
    ////////////////////////////////////////////////////////////////////////////
    struct Animator3D
    {
        std::shared_ptr<const k3::AnimatedModel> model;
        k3::Animator                             animator;
        std::shared_ptr<const ClipCues>          cues;      //!< Optional
        bool                                     visible = true;

        // Kept by play() and AnimateModels
        k3::PlayOptions            options{};
        std::string                next;       //!< Played when the clip ends (a clip that does not loop)
        float                      seen = -1;  //!< Time of the clip at the last tick, for the cues. -1: none yet.
        std::optional<std::uint64_t> revision; //!< ModelRevision seen by AnimateModels
        std::optional<std::uint8_t>  stateSerial; //!< AnimationState3D::serial played by PlayReplicatedClips

        static Animator3D of(std::shared_ptr<const k3::AnimatedModel> model, std::string_view first = {},
            const k3::PlayOptions& options = {}, std::shared_ptr<const ClipCues> cues = {});

        //! Starts a clip. Nothing starts again if it is the one that plays already, unless it ended.
        //! @param next  The clip to play when this one ends (it must not loop; "": stay on the last pose)
        //! @return  false if the model has no such clip
        bool play(std::string_view clip, const k3::PlayOptions& options = {}, std::string_view next = {});
    };

}
