#pragma once

#include "kronkworld/Kronkworld.hpp"
#include <vector>

namespace kuge
{

    //! Fixed, stage Late: moves the Animators forward by one tick, gives their
    //! Sprite the sheet and the frame that is due, and fills AnimationEvents
    //! with the cues that were crossed. Entities are handled by increasing
    //! number, so that the events always come in the same order.
    class AnimateSprites : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override;

        private:
            std::vector<kw::Entity> m_entities;   //!< Kept between ticks
    };

}
