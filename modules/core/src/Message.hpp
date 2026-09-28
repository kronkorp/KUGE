#pragma once

#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

namespace kuge
{

    namespace detail
    {
        //! One address per type: what tells the types apart, without RTTI
        template<typename T>
        inline constexpr char messageTag = 0;
    }

    //! What SceneHandle::stop() sends: the scene that receives it is popped
    struct StopRequest {};

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  What is sent from a scene to another: a value of any type, that
     *         the receiver reads back by its type
     *
     *     handle.send(PlayerJoined{42, "Ana"});                  // sender
     *
     *     void onMessage(const kuge::Message& message) override // receiver
     *     {
     *         if (const auto* joined = message.as<PlayerJoined>()) { ... }
     *     }
     *
     * The value is moved into the message: it can be a type that cannot be
     * copied. Only the scene that receives it touches it, so nothing is shared.
     */
    ////////////////////////////////////////////////////////////////////////////
    class Message
    {
        public:
            template<typename T>
                requires (!std::same_as<std::decay_t<T>, Message>)
            explicit Message(T&& value)
                : m_holder(std::make_unique<Holder<std::decay_t<T>>>(std::forward<T>(value))),
                  m_tag(&detail::messageTag<std::decay_t<T>>)
            {
            }

            Message(Message&&) noexcept            = default;
            Message& operator=(Message&&) noexcept = default;

            template<typename T>
            bool is(void) const noexcept
            {
                return m_tag == &detail::messageTag<T>;
            }

            //! The value if the message holds a T, nullptr otherwise
            template<typename T>
            const T* as(void) const noexcept
            {
                return is<T>() ? &static_cast<const Holder<T>&>(*m_holder).value : nullptr;
            }

            //! Same, to take what the message holds (std::move(*message.as<T>()))
            template<typename T>
            T* as(void) noexcept
            {
                return is<T>() ? &static_cast<Holder<T>&>(*m_holder).value : nullptr;
            }

        private:
            struct Base
            {
                virtual ~Base(void) = default;
            };

            template<typename T>
            struct Holder final : Base
            {
                template<typename U>
                explicit Holder(U&& v) : value(std::forward<U>(v)) {}

                T value;
            };

            std::unique_ptr<Base> m_holder;
            const char*           m_tag;
    };

}
