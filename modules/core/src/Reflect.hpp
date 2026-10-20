#pragma once

#include "Json.hpp"
#include "Math2D.hpp"
#include "kronkworld/Kronkworld.hpp"
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>
#include <kronk3d/math/Vector.hpp>

namespace kuge
{

    enum class FieldType : std::uint8_t { Bool, Int, Float, String, Vec2, Vec3, Vec4, Enum, Entity };

    //! The name of a type, for a message ("a number", "a whole number")
    const char* typeName(FieldType type) noexcept;

    //! An entity as a field (to tell it from a number)
    struct EntityRef
    {
        kw::Entity id = 0;

        bool operator==(const EntityRef&) const = default;
    };

    //! A value of a field, whatever the component: what a script, an editor or a file reads and writes by name.
    //! An enum is its name (a string), a Vec3/Vec4 is a vector of the engine's math.
    using FieldValue = std::variant<bool, std::int64_t, double, std::string, Vec2, k3::Math::Vector3f, k3::Math::Vector4f, EntityRef>;

    //! More to say about a field than its type
    struct FieldOptions
    {
        std::optional<double>     min;      //!< Numbers: refused below it
        std::optional<double>     max;      //!< Numbers: refused above it
        std::string               help;
        std::vector<std::string>  names;    //!< An enum: the names of its values, from 0 up (it must be said: C++ cannot)
    };

    //! One field of a component, seen from outside: no template left
    struct FieldInfo
    {
        std::string   name;
        FieldType     type = FieldType::Int;
        FieldOptions  options;
        //! Reads it from the component
        std::function<FieldValue(const void* component)>                         read;
        //! Writes it. @return  "" if it went, else why not (a value of another type, outside the limits...). `value` was checked for its type
        std::function<std::string(void* component, const FieldValue& value)>     write;
    };

    namespace detail
    {
        template<typename M> struct IsVec3 : std::false_type {};
        template<> struct IsVec3<k3::Math::Vector3f> : std::true_type {};
        template<typename M> struct IsVec4 : std::false_type {};
        template<> struct IsVec4<k3::Math::Vector4f> : std::true_type {};

        template<typename M>
        constexpr FieldType typeOf(void)
        {
            if constexpr (std::is_same_v<M, bool>) {
                return FieldType::Bool;
            } else if constexpr (std::is_enum_v<M>) {
                return FieldType::Enum;
            } else if constexpr (std::is_integral_v<M>) {
                return FieldType::Int;
            } else if constexpr (std::is_floating_point_v<M>) {
                return FieldType::Float;
            } else if constexpr (std::is_same_v<M, std::string>) {
                return FieldType::String;
            } else if constexpr (std::is_same_v<M, Vec2>) {
                return FieldType::Vec2;
            } else if constexpr (IsVec3<M>::value) {
                return FieldType::Vec3;
            } else if constexpr (IsVec4<M>::value) {
                return FieldType::Vec4;
            } else {
                static_assert(std::is_same_v<M, void>, "a field is a bool, an integer, a float, a std::string, an enum, a Vec2, a Vec3 or a Vec4 (use entityField() for an entity)");
            }
        }

        std::string checkRange(const FieldOptions& options, double value, const std::string& name);
    }

    //! A field of C by its member: `field<Body>("gravityScale", &Body::gravityScale, {.min = 0.0})`
    template<typename C, typename B, typename M>
    FieldInfo field(std::string name, M B::* member, FieldOptions options = {})
    {
        static_assert(std::is_base_of_v<B, C>, "the member must be one of the component");
        constexpr FieldType type = detail::typeOf<M>();
        FieldInfo info;

        info.name = std::move(name);
        info.type = type;
        info.options = std::move(options);
        info.read = [member, names = info.options.names](const void* component) -> FieldValue {
            const M& value = static_cast<const C*>(component)->*member;

            if constexpr (type == FieldType::Bool || type == FieldType::String || type == FieldType::Vec2 || type == FieldType::Vec3 || type == FieldType::Vec4) {
                return value;
            } else if constexpr (type == FieldType::Int) {
                return static_cast<std::int64_t>(value);
            } else if constexpr (type == FieldType::Float) {
                return static_cast<double>(value);
            } else {
                const auto index = static_cast<std::size_t>(static_cast<std::int64_t>(value));

                return index < names.size() ? names[index] : std::to_string(index);
            }
        };
        info.write = [member, options = info.options, name = info.name](void* component, const FieldValue& value) -> std::string {
            M& target = static_cast<C*>(component)->*member;

            if constexpr (type == FieldType::Bool || type == FieldType::String || type == FieldType::Vec2) {
                target = std::get<M>(value);
            } else if constexpr (type == FieldType::Vec3 || type == FieldType::Vec4) {
                target = std::get<M>(value);
            } else if constexpr (type == FieldType::Int) {
                const std::int64_t number = std::get<std::int64_t>(value);

                if (auto why = detail::checkRange(options, static_cast<double>(number), name); !why.empty()) {
                    return why;
                }
                if (number < static_cast<std::int64_t>(std::numeric_limits<M>::min()) || number > static_cast<std::int64_t>(std::numeric_limits<M>::max())) {
                    return name + ": " + std::to_string(number) + " does not fit";
                }
                target = static_cast<M>(number);
            } else if constexpr (type == FieldType::Float) {
                const double number = std::holds_alternative<double>(value) ? std::get<double>(value) : static_cast<double>(std::get<std::int64_t>(value));

                if (auto why = detail::checkRange(options, number, name); !why.empty()) {
                    return why;
                }
                target = static_cast<M>(number);
            } else {
                const std::string& word = std::get<std::string>(value);

                for (std::size_t i = 0; i < options.names.size(); ++i) {
                    if (options.names[i] == word) {
                        target = static_cast<M>(i);
                        return {};
                    }
                }
                return name + ": '" + word + "' is not one of its values";
            }
            return {};
        };
        return info;
    }

    //! A field that holds an entity (a target, a parent)
    template<typename C, typename B>
    FieldInfo entityField(std::string name, kw::Entity B::* member, FieldOptions options = {})
    {
        FieldInfo info;

        info.name = std::move(name);
        info.type = FieldType::Entity;
        info.options = std::move(options);
        info.read = [member](const void* component) -> FieldValue { return EntityRef{static_cast<const C*>(component)->*member}; };
        info.write = [member](void* component, const FieldValue& value) -> std::string {
            static_cast<C*>(component)->*member = std::get<EntityRef>(value).id;
            return {};
        };
        return info;
    }

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The components of a game, readable and writable by name: what scripts, an editor, a prefab and a timeline need
     *
     *     kuge::ComponentRegistry registry;
     *     registry.describe<Health>("Health", {
     *         kuge::field<Health>("points", &Health::points, {.min = 0, .max = 1000}),
     *         kuge::field<Health>("regen", &Health::regen),
     *     });
     *     kuge::describeCore(registry);                         // Transform2D, Transform3D, Collider3D, Body3D...
     *
     *     registry.add(world, e, "Health");                     // a component of that type, with its default values
     *     auto result = registry.set(world, e, "Health", "points", std::int64_t(50));      // {ok, message}: says which field is wrong and why
     *     registry.fromJson(world, e, "Health", Json::parse(R"({"points": 80})"));
     *     Json entity = registry.entityToJson(world, e);        // {"Health": {...}, "Transform2D": {...}}: a prefab, a scene file
     *
     * The registry is an object of the game (not a global: a program and a library never disagree about it). Only aggregates are described, field
     * by field, with no macro and no code generator. A component is described once for every use; the replication registry still lists the
     * same components for the network (making it read these descriptions is a later step).
     */
    ////////////////////////////////////////////////////////////////////////////
    class ComponentRegistry
    {
        public:
            struct Result
            {
                bool        ok = true;
                std::string message;   //!< Why not, naming the component and the field
            };

            //! What a component looks like from outside
            struct Component
            {
                std::string             name;
                std::vector<FieldInfo>  fields;
                std::function<bool(kw::World&, kw::Entity)>  has;
                std::function<void(kw::World&, kw::Entity)>  add;       //!< A default one (nothing if it is there already)
                std::function<void(kw::World&, kw::Entity)>  remove;
                std::function<void*(kw::World&, kw::Entity)> get;       //!< Null if the entity has none
            };

            //! @throw std::invalid_argument if the name is taken, or two fields have the same name
            template<typename C>
            void describe(std::string name, std::vector<FieldInfo> fields)
            {
                Component component;

                component.name = name;
                component.fields = std::move(fields);
                component.has = [](kw::World& world, kw::Entity e) { return world.has<C>(e); };
                component.add = [](kw::World& world, kw::Entity e) { if (!world.has<C>(e)) { world.add<C>(e, C{}); } };
                component.remove = [](kw::World& world, kw::Entity e) { if (world.has<C>(e)) { world.remove<C>(e); } };
                component.get = [](kw::World& world, kw::Entity e) -> void* { return world.has<C>(e) ? &world.get<C>(e) : nullptr; };
                insert(std::move(component));
            }

            const Component*               byName(std::string_view name) const;
            const std::vector<Component>&  all(void) const noexcept { return m_components; }

            bool   has(kw::World& world, kw::Entity entity, std::string_view component) const;
            //! @return  false if the component is unknown
            bool   add(kw::World& world, kw::Entity entity, std::string_view component) const;
            bool   remove(kw::World& world, kw::Entity entity, std::string_view component) const;

            //! The names of the described components that this entity has, in registration order
            std::vector<std::string> componentsOf(kw::World& world, kw::Entity entity) const;

            //! The fields of a component of this entity, with their values (empty if it has none)
            std::vector<std::pair<std::string, FieldValue>> get(kw::World& world, kw::Entity entity, std::string_view component) const;

            //! Sets one field. The component is added if the entity has none. Says why not: an unknown component or field, a value of the
            //! wrong type, a number outside its limits.
            Result set(kw::World& world, kw::Entity entity, std::string_view component, std::string_view field, const FieldValue& value) const;

            // -- JSON: the format of a prefab and of a scene file ---------------------------------------------
            //! {"points": 80, "regen": 0.5}; a Vec2 is [x, y], a Vec3 [x, y, z], a Vec4 [x, y, z, w], an enum its name, an entity a number
            Json toJson(kw::World& world, kw::Entity entity, std::string_view component) const;

            //! Reads fields into the component (added if missing). Fields that are not in the JSON keep their value; nothing is changed if any
            //! field is wrong. A field that is not described, or of the wrong type, is named in the message.
            Result fromJson(kw::World& world, kw::Entity entity, std::string_view component, const Json& json) const;

            //! {"Health": {...}, "Transform2D": {...}}: every described component the entity has
            Json entityToJson(kw::World& world, kw::Entity entity) const;
            //! The inverse, on an entity that exists. Nothing is changed if anything is wrong.
            Result entityFromJson(kw::World& world, kw::Entity entity, const Json& json) const;

        private:
            void insert(Component component);

            static Json valueToJson(const FieldValue& value);
            static std::optional<FieldValue> valueFromJson(const FieldInfo& field, const Json& json, std::string& why);

            std::vector<Component>              m_components;
            std::map<std::string, std::size_t, std::less<>> m_byName;
    };

    //! Describes the components of the core: Transform2D, Transform3D, Collider3D, Body3D, Character3D
    void describeCore(ComponentRegistry& registry);

}
