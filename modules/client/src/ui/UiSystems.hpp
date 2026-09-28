#pragma once

#include "kronkworld/Kronkworld.hpp"
#include <vector>

namespace kuge
{

    //! Frame, Late: works out the rectangle of every node
    class UiLayout : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override;

        private:
            std::vector<kw::Entity> m_nodes;   //!< Kept between frames
    };

    //! Fixed, Input: focus, the mouse, and the buttons that are pressed
    class UiInteract : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override;
    };

    //! Frame, Render: draws panels, buttons and text
    class UiRender : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override;
    };

}
