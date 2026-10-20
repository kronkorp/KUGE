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
        k3::Math::Vector3f forward{0.0f, 0.0f, -1.0f}; //!< A unit vector: a sound behind it is quieter and muffled
        float              curve = 1.0f;               //!< The volume is (1 - distance / range) to this power: 1 falls evenly, 2 falls faster
    };

    //! The ears of a camera: where it is, and its right
    Listener3D listenerOf(const k3::Camera& camera, float range = 40.0f, float panWidth = 10.0f) noexcept;

    //! What a sound at a place is heard as: the volume falls from 1 on the spot to 0 at the end of the range (along
    //! `curve`), the pan is how far to the right of the listener it is, and the highs are cut for what is far away,
    //! behind the listener (up to a fifth quieter and half way muffled when it is right behind) or behind a wall.
    //! @param occlusion  0 to 1, how much is in the way: the game finds it (a raycast of the physics, see
    //!                   Physics3D::raycast()). 1 halves the volume and cuts most of the highs.
    Spatial spatialize(k3::Math::Vector3f source, const Listener3D& listener, float occlusion = 0.0f) noexcept;

    //! Plays a sound that comes from a place. Empty if it is out of hearing: no voice is taken. The bus, the
    //! priority (and so which sounds a crowded mix keeps: the nearest) are those of the options.
    //! @param options  volume, loop, bus and priority are used (the pan and the filter are worked out)
    Voice playAt(Audio& audio, const Sound& sound, k3::Math::Vector3f source, const Listener3D& listener, PlayOptions options = {},
        float occlusion = 0.0f);

    //! A source that moves, or a listener that turns: gives a voice its volume, pan and filter for the place it is
    //! at now. Call it each frame.
    //! @param volume  The volume of the sound itself, before the distance (what PlayOptions::volume was)
    //! @return  false if it is out of hearing now (the voice is silent, but it keeps its place)
    bool moveVoice(Audio& audio, Voice voice, k3::Math::Vector3f source, const Listener3D& listener, float volume = 1.0f,
        float occlusion = 0.0f);

}
