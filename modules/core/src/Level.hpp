#pragma once

#include "Physics3DComponents.hpp"
#include "Transform3D.hpp"
#include "kronkworld/Kronkworld.hpp"
#include <cstdint>
#include <kronk3d/scene/Animation.hpp>
#include <memory>
#include <string>
#include <vector>


namespace kuge
{

    //! A collider of a level: where it is, and what it is made of
    struct LevelCollider
    {
        std::string name;
        Collider3D  collider;
        Transform3D transform;   //!< A mesh is made in world space (this is the identity); a zone is a box at its center
    };

    //! A place of a level that the game gives a meaning to (a spawn, a lamp)
    struct LevelMarker
    {
        std::string name;
        Transform3D transform;   //!< Without scale
    };

    //! What a level scene says (see readLevel())
    struct LevelData
    {
        std::shared_ptr<const k3::AnimatedModel> visible;      //!< What is seen: the scene without the colliders and the zones, in its rest pose
        std::vector<LevelCollider>               colliders;
        std::vector<LevelMarker>                 markers;
        std::uint64_t                            hash = 0;     //!< Of the colliders and the markers: the same file gives the same on the server and on a client
    };

    //! On the entity that spawnColliders() makes for a collider of a level: its number in `LevelData::colliders`, the same on every side
    struct LevelPiece
    {
        std::uint32_t id = 0;
    };

    //! The pieces of a level that are gone (a wall that a blast opened). One entity of the game has it, and it is replicated
    //! (registerLevelBreaks); each side removes the pieces it names with applyBreaks(), the room, the clients and the predictions alike.
    struct LevelBreaks
    {
        std::vector<std::uint32_t> broken;   //!< Sorted, without repeats

        bool has(std::uint32_t piece) const noexcept;
        //! @return  false if it was already gone
        bool add(std::uint32_t piece);
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A level made in an editor (Blender, a glTF): its meshes are what is seen, its named nodes say what plays
     *
     *     auto scene = models.animatedModels().load("maps/rooftops.glb");
     *     kuge::LevelData level = kuge::readLevel(*scene);
     *     kuge::spawnColliders(world, level);               // on the room, the prediction and every client
     *     world.add<kuge::Animator3D>(e, kuge::Animator3D::of(level.visible));   // on a client
     *
     * By the name of a node (the case does not count):
     *  - `col_*`   its meshes are a collider of triangles (a Collider3D::triangles in world space: a roof that is a roof);
     *  - `zone_*`  the box around its meshes is a trigger (they are not drawn);
     *  - `spawn_*` and `lamp*`  a marker at the place of the node, whatever it holds (empty nodes are what these are in Blender);
     *  - any other node with meshes is what is seen.
     * A skinned mesh is seen as it is. The level is built from its file on every side, so nothing of it is replicated.
     */
    ////////////////////////////////////////////////////////////////////////////
    LevelData readLevel(const k3::AnimatedModel& scene);

    //! Makes an entity (a Transform3D and a Collider3D) for each collider of the level, with this layer and mask
    //! @return  The entities, in the order of `level.colliders`
    std::vector<kw::Entity> spawnColliders(kw::World& world, const LevelData& level, std::uint32_t layer = 1, std::uint32_t mask = 0xFFFFFFFFu);

    //! Removes the entities that have a LevelPiece that `breaks` names (their colliders go with them, and what stood on them is woken up:
    //! see Physics3D). It does nothing for a piece that is gone already, so it can be called at every tick.
    //! The game's own parts of a piece (its model) are components of the same entity, or are removed by the game.
    //! @return  How many were removed
    std::size_t applyBreaks(kw::World& world, const LevelBreaks& breaks);

}
