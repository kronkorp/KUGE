#pragma once

#include "Math2D.hpp"
#include "kronkworld/Kronkworld.hpp"
#include "render/Spritesheet.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace kuge
{

    //! An animation set that cannot be read
    class AnimationError : public std::runtime_error
    {
        public:
            using std::runtime_error::runtime_error;
    };

    //! Something that happens at a frame of a clip ("hit" when the sword swings)
    struct AnimationCue
    {
        int         frame;   //!< Its place in the clip (0 is the first frame shown)
        std::string name;
    };

    //! A sequence of frames of a spritesheet
    struct AnimationClip
    {
        std::string                name;
        std::vector<int>           frames;    //!< Numbers of cells of the spritesheet
        float                      fps  = 10.0f;
        bool                       loop = true;
        std::string                next;      //!< What plays when this one ends (a clip that does not loop)
        std::vector<AnimationCue>  cues;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The animations of a character or an object
     *
     * As a file, one clip per line:
     *
     *     # clip <name> fps=<n> frames=<list> [loop=false] [next=<clip>] [cue.<frame>=<name>]
     *     clip idle   fps=6  frames=0-3
     *     clip run    fps=12 frames=4,5,6,7,6,5
     *     clip attack fps=14 frames=8-11 loop=false next=idle cue.2=hit
     *
     * "frames" is a list of cell numbers, apart by commas, where a-b is every
     * number from a to b. "next" makes a clip that does not loop go to another
     * when it ends: with it, the clips form a small state machine.
     */
    ////////////////////////////////////////////////////////////////////////////
    class AnimationSet
    {
        public:
            std::vector<AnimationClip> clips;

            //! The number of the clip, -1 if there is none of this name
            int find(std::string_view name) const noexcept;

            //! @throw AnimationError, with the number of the line that is wrong
            static AnimationSet parse(std::string_view text);

            //! @throw AnimationError
            static AnimationSet load(const std::filesystem::path& path);
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Plays the clips of a set on the Sprite of an entity (a component)
     *
     * The systems do the rest: AnimateSprites moves time forward at each tick and
     * gives the Sprite the sheet and the frame that is due.
     *
     *     world.add<kuge::Animator>(e, kuge::Animator::of(sheet, clips, "idle"));
     *     world.get<kuge::Animator>(e).play("run");   // does nothing if it is already running
     *
     *     world.add<kuge::Animator>(e, kuge::Animator::loop(sheet, 12.0f));   // every cell, no clips file
     */
    ////////////////////////////////////////////////////////////////////////////
    struct Animator
    {
        std::shared_ptr<const Spritesheet>   sheet;
        std::shared_ptr<const AnimationSet>  clips;
        int     clip     = 0;
        float   time     = 0.0f;    //!< Seconds since the clip began
        float   speed    = 1.0f;    //!< 2: twice as fast. 0: frozen. Not negative.
        bool    playing  = true;
        bool    finished = false;   //!< A clip that does not loop got to its last frame
        long    seen     = -1;      //!< The last frame counted since the clip began, for the cues

        static Animator of(std::shared_ptr<const Spritesheet> sheet, std::shared_ptr<const AnimationSet> clips,
            std::string_view first = {});

        //! Plays every cell of the sheet in order, in a loop: one clip, "loop".
        //! The cells are counted now, so the sheet needs its picture already.
        //! @throw AnimationError if the sheet has no cells, or fps is not above 0
        static Animator loop(std::shared_ptr<const Spritesheet> sheet, float fps);

        //! Starts a clip. Nothing changes if it is the one that plays already,
        //! unless restart is true.
        //! @return  false if the set has no such clip
        bool play(std::string_view name, bool restart = false);

        //! Same, by its number in the set
        bool play(int index, bool restart = false);

        //! The name of the clip that plays, "" if none
        std::string_view current(void) const noexcept;
    };

    struct AnimationEvent
    {
        kw::Entity  entity;
        std::string name;
    };

    //! What happened during the last tick (a resource of the World)
    struct AnimationEvents
    {
        std::vector<AnimationEvent> list;
    };

}
