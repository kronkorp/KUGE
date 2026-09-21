#pragma once

#include "Math2D.hpp"
#include "Serializer.hpp"
#include <array>
#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace kuge::net
{

    //! A number that a name always gives (FNV-1a), the same on every machine and build:
    //! the id of a message on the wire
    constexpr std::uint32_t hashName(std::string_view name) noexcept
    {
        std::uint32_t hash = 2166136261u;

        for (const char c : name) {
            hash = (hash ^ static_cast<std::uint8_t>(c)) * 16777619u;
        }
        return hash;
    }

    //! Made by KUGE_MESSAGE
    template<typename T>
    concept NetMessage = requires { { T::kugeMessageId } -> std::convertible_to<std::uint32_t>; };

    template<typename T>
    concept Visitable = NetMessage<T>;

    template<typename T>
    void encode(ByteWriter& out, const T& value);

    template<typename T>
    void decode(ByteReader& in, T& value);

    namespace detail
    {
        template<typename T> struct IsVector : std::false_type {};
        template<typename T> struct IsVector<std::vector<T>> : std::true_type {};
        template<typename T> struct IsArray : std::false_type {};
        template<typename T, std::size_t N> struct IsArray<std::array<T, N>> : std::true_type {};
        template<typename T> struct IsOptional : std::false_type {};
        template<typename T> struct IsOptional<std::optional<T>> : std::true_type {};
    }

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  How a value goes on the wire (through the Serializer of the core:
     *         little-endian, the same on every machine)
     *
     * Numbers, bools, enums, strings, vectors, arrays, optionals, Vec2, Rect,
     * and messages (KUGE_MESSAGE) inside others. Reading is checked: a length
     * that the data cannot hold throws SerializerError instead of allocating.
     */
    ////////////////////////////////////////////////////////////////////////////
    template<typename T>
    void encode(ByteWriter& out, const T& value)
    {
        if constexpr (Scalar<T>) {
            out.write(value);
        } else if constexpr (std::is_same_v<T, std::string>) {
            out.writeString(value);
        } else if constexpr (detail::IsVector<T>::value) {
            out.write<std::uint32_t>(static_cast<std::uint32_t>(value.size()));
            for (const auto& element : value) {
                encode(out, element);
            }
        } else if constexpr (detail::IsArray<T>::value) {
            for (const auto& element : value) {
                encode(out, element);
            }
        } else if constexpr (detail::IsOptional<T>::value) {
            out.write<bool>(value.has_value());
            if (value) {
                encode(out, *value);
            }
        } else if constexpr (std::is_same_v<T, Vec2>) {
            out.write(value.x);
            out.write(value.y);
        } else if constexpr (std::is_same_v<T, Rect>) {
            out.write(value.x);
            out.write(value.y);
            out.write(value.w);
            out.write(value.h);
        } else if constexpr (Visitable<T>) {
            value.kugeVisit([&out](const auto& field) { encode(out, field); });
        } else {
            static_assert(sizeof(T) == 0, "this type cannot go on the wire: list its fields in KUGE_MESSAGE");
        }
    }

    template<typename T>
    void decode(ByteReader& in, T& value)
    {
        if constexpr (Scalar<T>) {
            value = in.read<T>();
        } else if constexpr (std::is_same_v<T, std::string>) {
            value = in.readString();
        } else if constexpr (detail::IsVector<T>::value) {
            const std::size_t count = in.read<std::uint32_t>();

            // An element takes at least a byte: more elements than bytes is a lie
            if (count > in.remaining()) {
                throw SerializerError("wire: a vector longer than the data");
            }
            value.clear();
            value.resize(count);
            for (auto& element : value) {
                decode(in, element);
            }
        } else if constexpr (detail::IsArray<T>::value) {
            for (auto& element : value) {
                decode(in, element);
            }
        } else if constexpr (detail::IsOptional<T>::value) {
            if (in.read<bool>()) {
                value.emplace();
                decode(in, *value);
            } else {
                value.reset();
            }
        } else if constexpr (std::is_same_v<T, Vec2>) {
            value.x = in.read<float>();
            value.y = in.read<float>();
        } else if constexpr (std::is_same_v<T, Rect>) {
            value.x = in.read<float>();
            value.y = in.read<float>();
            value.w = in.read<float>();
            value.h = in.read<float>();
        } else if constexpr (Visitable<T>) {
            value.kugeVisit([&in](auto& field) { decode(in, field); });
        } else {
            static_assert(sizeof(T) == 0, "this type cannot come from the wire: list its fields in KUGE_MESSAGE");
        }
    }

}

// -- KUGE_MESSAGE ------------------------------------------------------------------------------
// Up to 24 fields. (The classic trick to apply a macro to each argument.)
#define KUGE_NET_CAT_(a, b) a##b
#define KUGE_NET_CAT(a, b) KUGE_NET_CAT_(a, b)
#define KUGE_NET_COUNT_(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16,_17,_18,_19,_20,_21,_22,_23,_24,N,...) N
#define KUGE_NET_COUNT(...) KUGE_NET_COUNT_(__VA_ARGS__,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0)
#define KUGE_NET_FE_1(m, x) m(x)
#define KUGE_NET_FE_2(m, x, ...) m(x) KUGE_NET_FE_1(m, __VA_ARGS__)
#define KUGE_NET_FE_3(m, x, ...) m(x) KUGE_NET_FE_2(m, __VA_ARGS__)
#define KUGE_NET_FE_4(m, x, ...) m(x) KUGE_NET_FE_3(m, __VA_ARGS__)
#define KUGE_NET_FE_5(m, x, ...) m(x) KUGE_NET_FE_4(m, __VA_ARGS__)
#define KUGE_NET_FE_6(m, x, ...) m(x) KUGE_NET_FE_5(m, __VA_ARGS__)
#define KUGE_NET_FE_7(m, x, ...) m(x) KUGE_NET_FE_6(m, __VA_ARGS__)
#define KUGE_NET_FE_8(m, x, ...) m(x) KUGE_NET_FE_7(m, __VA_ARGS__)
#define KUGE_NET_FE_9(m, x, ...) m(x) KUGE_NET_FE_8(m, __VA_ARGS__)
#define KUGE_NET_FE_10(m, x, ...) m(x) KUGE_NET_FE_9(m, __VA_ARGS__)
#define KUGE_NET_FE_11(m, x, ...) m(x) KUGE_NET_FE_10(m, __VA_ARGS__)
#define KUGE_NET_FE_12(m, x, ...) m(x) KUGE_NET_FE_11(m, __VA_ARGS__)
#define KUGE_NET_FE_13(m, x, ...) m(x) KUGE_NET_FE_12(m, __VA_ARGS__)
#define KUGE_NET_FE_14(m, x, ...) m(x) KUGE_NET_FE_13(m, __VA_ARGS__)
#define KUGE_NET_FE_15(m, x, ...) m(x) KUGE_NET_FE_14(m, __VA_ARGS__)
#define KUGE_NET_FE_16(m, x, ...) m(x) KUGE_NET_FE_15(m, __VA_ARGS__)
#define KUGE_NET_FE_17(m, x, ...) m(x) KUGE_NET_FE_16(m, __VA_ARGS__)
#define KUGE_NET_FE_18(m, x, ...) m(x) KUGE_NET_FE_17(m, __VA_ARGS__)
#define KUGE_NET_FE_19(m, x, ...) m(x) KUGE_NET_FE_18(m, __VA_ARGS__)
#define KUGE_NET_FE_20(m, x, ...) m(x) KUGE_NET_FE_19(m, __VA_ARGS__)
#define KUGE_NET_FE_21(m, x, ...) m(x) KUGE_NET_FE_20(m, __VA_ARGS__)
#define KUGE_NET_FE_22(m, x, ...) m(x) KUGE_NET_FE_21(m, __VA_ARGS__)
#define KUGE_NET_FE_23(m, x, ...) m(x) KUGE_NET_FE_22(m, __VA_ARGS__)
#define KUGE_NET_FE_24(m, x, ...) m(x) KUGE_NET_FE_23(m, __VA_ARGS__)
#define KUGE_NET_FOR_EACH(m, ...) KUGE_NET_CAT(KUGE_NET_FE_, KUGE_NET_COUNT(__VA_ARGS__))(m, __VA_ARGS__)
#define KUGE_NET_VISIT(field) visitor(field);

//! Makes a struct a message: gives it an id (from its name) and says which fields go on the wire,
//! in this order. Put it in the struct.
//!
//!     struct PlayerMoved {
//!         KUGE_MESSAGE(PlayerMoved, id, position, heading)
//!         std::uint32_t id = 0;
//!         kuge::Vec2    position;
//!         float         heading = 0;
//!     };
//!
//!     endpoint.send(peer, PlayerMoved{...}, kuge::net::Channel::Unreliable);
//!     endpoint.on<PlayerMoved>([](kuge::net::ConnectionId from, const PlayerMoved& moved) { ... });
//!
//! Both sides must have the same name and fields (the id is a hash of the name).
#define KUGE_MESSAGE(Type, ...) \
    static constexpr std::uint32_t kugeMessageId = ::kuge::net::hashName(#Type); \
    static constexpr const char* kugeMessageName = #Type; \
    template<typename Visitor> void kugeVisit([[maybe_unused]] Visitor&& visitor) { __VA_OPT__(KUGE_NET_FOR_EACH(KUGE_NET_VISIT, __VA_ARGS__)) } \
    template<typename Visitor> void kugeVisit([[maybe_unused]] Visitor&& visitor) const { __VA_OPT__(KUGE_NET_FOR_EACH(KUGE_NET_VISIT, __VA_ARGS__)) }
