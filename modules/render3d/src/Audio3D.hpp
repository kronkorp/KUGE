#pragma once

#include "audio/Audio.hpp"
#include <kronk3d/math/Vector.hpp>
#include <kronk3d/scene/Camera.hpp>

namespace kuge
{

    //! Who hears, in 3D: where the ears are, which way is right, and how far a sound reaches
    struct Listener3D
    {
        k3::Math::Vector3f position{};
        k3::Math::Vector3f right{1.0f, 0.0f, 0.0f};    //!< A unit vector
        float              range = 40.0f;              //!< Meters. Farther than this, a sound is not heard at all.
        float              panWidth = 10.0f;           //!< Meters. Sideways, this far is all on one side.
    };

    //! The ears of a camera: where it is, and its right
    Listener3D listenerOf(const k3::Camera& camera, float range = 40.0f, float panWidth = 10.0f) noexcept;

    //! What a sound at a place is heard as: the volume falls from 1 on the spot to 0 at the end of the range (as in 2D),
    //! and the pan is how far to the right of the listener it is (behind or ahead does not matter)
    Spatial spatialize(k3::Math::Vector3f source, const Listener3D& listener) noexcept;

    //! Plays a sound that comes from a place. Empty if it is out of hearing: no voice is taken.
    //! @param options  volume and loop are used (the pan is worked out)
    Voice playAt(Audio& audio, const Sound& sound, k3::Math::Vector3f source, const Listener3D& listener, PlayOptions options = {});

}
