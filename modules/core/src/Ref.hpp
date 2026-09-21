#pragma once

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A resource of a World that points to an object owned elsewhere
     *
     * The services of the engine (window, renderer, inputs...) live in its
     * modules, and every scene needs them: each World gets a Ref to them.
     *
     *     auto& renderer = *world.getResource<kuge::Ref<Renderer2D>>();
     */
    ////////////////////////////////////////////////////////////////////////////
    template<typename T>
    class Ref
    {
        public:
            explicit Ref(T& target) noexcept : m_target(&target) {}

            T& get(void) const noexcept { return *m_target; }
            T& operator*(void) const noexcept { return *m_target; }
            T* operator->(void) const noexcept { return m_target; }

        private:
            T* m_target;
    };

}
