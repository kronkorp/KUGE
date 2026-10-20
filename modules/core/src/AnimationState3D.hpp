#pragma once

#include <cstdint>
#include <string>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The clip that a 3D character plays, as the simulation says it (a component)
     *
     * The server has no Animator3D (no model): it only says which clip an entity plays, and
     * replicates this (registerAnimationState3D). The client's PlayReplicatedClips plays it on the
     * entity's Animator3D. Say the same clip again (an attack that starts a second time) by moving
     * `serial`.
     *
     *     world.get<kuge::AnimationState3D>(ship) = {"attack", false, ++serial};
     */
    ////////////////////////////////////////////////////////////////////////////
    struct AnimationState3D
    {
        std::string   clip;
        bool          loop = true;
        std::uint8_t  serial = 0;

        bool operator==(const AnimationState3D&) const = default;
    };

}
