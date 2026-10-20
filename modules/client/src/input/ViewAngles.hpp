#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Where a first or third person player looks: a yaw and a pitch, turned by the mouse
     *
     * Turn it at each **frame** (a camera that turns once per tick feels late) with what the mouse did,
     * read the angles for the camera at once, and sample them at each tick for the input:
     *
     *     view.turn(input.axisValue(Look::Yaw), input.axisValue(Look::Pitch));   // each frame
     *     ...
     *     message.yaw   = kuge::packAngle(view.yaw);                              // each tick, absolute
     *     message.pitch = kuge::packAngle(view.pitch);
     *
     * The input carries the angles, not the moves: a lost input then loses nothing of the aim.
     * Same signs as CameraRig: a positive yaw turns right, a positive pitch looks up (the mouse going
     * down, y positive, looks down).
     */
    ////////////////////////////////////////////////////////////////////////////
    struct ViewAngles
    {
        static constexpr float MAX_PITCH = 89.0f * std::numbers::pi_v<float> / 180.0f;   //!< (radians; not 90: the view would flip)

        float yaw   = 0.0f;   //!< Radians, kept in [-pi, pi)
        float pitch = 0.0f;   //!< Radians, kept in [-MAX_PITCH, MAX_PITCH]

        //! @param yawMove    Mouse x (right is positive), times the sensitivity
        //! @param pitchMove  Mouse y (down is positive), times the sensitivity
        //! @param radiansPerUnit  How far a unit turns (default: a little more than a tenth of a degree)
        void turn(float yawMove, float pitchMove, float radiansPerUnit = 0.0022f) noexcept
        {
            constexpr float PI = std::numbers::pi_v<float>;

            yaw += yawMove * radiansPerUnit;
            yaw = std::remainder(yaw, 2.0f * PI);   // [-pi, pi]
            if (yaw >= PI) {
                yaw -= 2.0f * PI;
            }
            pitch = std::clamp(pitch - pitchMove * radiansPerUnit, -MAX_PITCH, MAX_PITCH);
        }
    };

    //! An angle in radians (any, wrapped to [-pi, pi)) as 16 bits: a step of 0.0055 degrees, enough to aim
    inline std::uint16_t packAngle(float radians) noexcept
    {
        constexpr float TURN = 2.0f * std::numbers::pi_v<float>;
        float turns = radians / TURN;

        turns -= std::floor(turns);   // [0, 1)
        return static_cast<std::uint16_t>(std::lround(turns * 65536.0f) & 0xFFFF);
    }

    //! The angle of packAngle(), in [-pi, pi)
    inline float unpackAngle(std::uint16_t packed) noexcept
    {
        constexpr float TURN = 2.0f * std::numbers::pi_v<float>;
        const float radians = static_cast<float>(packed) / 65536.0f * TURN;

        return radians >= std::numbers::pi_v<float> ? radians - TURN : radians;
    }

}
