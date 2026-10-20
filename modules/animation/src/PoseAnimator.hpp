#pragma once

#include "Pose.hpp"
#include <memory>
#include <string_view>

namespace kuge
{

    //! How a clip is played (the same as k3::PlayOptions)
    struct ClipPlay
    {
        bool  loop  = true;
        float fade  = 0.2f;   //!< Seconds spent blending from the clip that was playing
        float speed = 1.0f;   //!< Negative plays backward
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Plays the clips of a model on the CPU and gives the pose, with crossfades: what k3::Animator does,
     *         without the drawing, so that the room (which has no window) can know where a bone is
     *
     *     PoseAnimator animator(model);
     *     animator.play("run");
     *     animator.update(1.0f / 60.0f);                                   // each tick
     *     auto bones = kuge::bonePlaces(*model, animator.pose(), {"head"}); // for the Hitboxes
     */
    ////////////////////////////////////////////////////////////////////////////
    class PoseAnimator
    {
        public:
            explicit PoseAnimator(std::shared_ptr<const k3::AnimatedModel> model);

            //! Starts `clip`, fading from the current one. Asking for the clip that plays (and has not finished) only
            //! updates the options. @return  false if the model has no such clip
            bool play(std::string_view clip, const ClipPlay& options = {});

            void update(float seconds);

            //! A clip that does not loop reached its end (it stays on its last pose)
            bool finished(void) const noexcept;

            std::string_view clip(void) const noexcept;
            float            time(void) const noexcept { return m_current.time; }

            //! The pose now (the rest pose before any clip)
            const Pose& pose(void);

        private:
            struct Track
            {
                int   clip  = -1;
                float time  = 0.0f;
                float speed = 1.0f;
                bool  loop  = true;
            };

            void advance(Track& track, float seconds) const;

            std::shared_ptr<const k3::AnimatedModel> m_model;
            Track m_current;
            Track m_previous;   // fading out while its clip is >= 0
            float m_fadeElapsed = 0.0f;
            float m_fadeDuration = 0.0f;
            Pose  m_pose;
            bool  m_dirty = true;
    };

}
