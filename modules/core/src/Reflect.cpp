#include "Reflect.hpp"
#include "Physics3DComponents.hpp"
#include "Transform2D.hpp"
#include "Transform3D.hpp"
#include <cmath>
#include <format>
#include <stdexcept>

const char* kuge::typeName(FieldType type) noexcept
{
    switch (type) {
        case FieldType::Bool: return "a boolean";
        case FieldType::Int: return "a whole number";
        case FieldType::Float: return "a number";
        case FieldType::String: return "a string";
        case FieldType::Vec2: return "a pair [x, y]";
        case FieldType::Vec3: return "a triple [x, y, z]";
        case FieldType::Vec4: return "four numbers [x, y, z, w]";
        case FieldType::Enum: return "the name of one of its values";
        case FieldType::Entity: return "an entity (a number)";
    }
    return "?";
}

std::string kuge::detail::checkRange(const FieldOptions& options, double value, const std::string& name)
{
    if ((options.min && value < *options.min) || (options.max && value > *options.max)) {
        return std::format("{}: {} is outside {} to {}", name, value, options.min ? std::to_string(*options.min) : "-", options.max ? std::to_string(*options.max) : "-");
    }
    return {};
}

void kuge::ComponentRegistry::insert(Component component)
{
    if (m_byName.count(component.name)) {
        throw std::invalid_argument(std::format("the component '{}' is described twice", component.name));
    }
    for (std::size_t i = 0; i < component.fields.size(); ++i) {
        for (std::size_t j = i + 1; j < component.fields.size(); ++j) {
            if (component.fields[i].name == component.fields[j].name) {
                throw std::invalid_argument(std::format("the component '{}' has two fields named '{}'", component.name, component.fields[i].name));
            }
        }
    }
    m_byName[component.name] = m_components.size();
    m_components.push_back(std::move(component));
}

const kuge::ComponentRegistry::Component* kuge::ComponentRegistry::byName(std::string_view name) const
{
    const auto found = m_byName.find(name);

    return found == m_byName.end() ? nullptr : &m_components[found->second];
}

bool kuge::ComponentRegistry::has(kw::World& world, kw::Entity entity, std::string_view component) const
{
    const Component* found = byName(component);

    return found && found->has(world, entity);
}

bool kuge::ComponentRegistry::add(kw::World& world, kw::Entity entity, std::string_view component) const
{
    const Component* found = byName(component);

    if (!found) {
        return false;
    }
    found->add(world, entity);
    return true;
}

bool kuge::ComponentRegistry::remove(kw::World& world, kw::Entity entity, std::string_view component) const
{
    const Component* found = byName(component);

    if (!found) {
        return false;
    }
    found->remove(world, entity);
    return true;
}

std::vector<std::string> kuge::ComponentRegistry::componentsOf(kw::World& world, kw::Entity entity) const
{
    std::vector<std::string> names;

    for (const Component& component : m_components) {
        if (component.has(world, entity)) {
            names.push_back(component.name);
        }
    }
    return names;
}

std::vector<std::pair<std::string, kuge::FieldValue>> kuge::ComponentRegistry::get(kw::World& world, kw::Entity entity, std::string_view component) const
{
    std::vector<std::pair<std::string, FieldValue>> values;
    const Component* found = byName(component);
    const void* data = found ? found->get(world, entity) : nullptr;

    if (data) {
        for (const FieldInfo& field : found->fields) {
            values.emplace_back(field.name, field.read(data));
        }
    }
    return values;
}

kuge::ComponentRegistry::Result kuge::ComponentRegistry::set(kw::World& world, kw::Entity entity, std::string_view component, std::string_view fieldName,
    const FieldValue& value) const
{
    const Component* found = byName(component);

    if (!found) {
        return {false, std::format("there is no component '{}'", component)};
    }
    const FieldInfo* target = nullptr;

    for (const FieldInfo& field : found->fields) {
        if (field.name == fieldName) {
            target = &field;
        }
    }
    if (!target) {
        return {false, std::format("{} has no field '{}'", component, fieldName)};
    }
    // The value must be of the type of the field (a whole number widens to a float)
    bool fits = false;

    switch (target->type) {
        case FieldType::Bool: fits = std::holds_alternative<bool>(value); break;
        case FieldType::Int: fits = std::holds_alternative<std::int64_t>(value); break;
        case FieldType::Float: fits = std::holds_alternative<double>(value) || std::holds_alternative<std::int64_t>(value); break;
        case FieldType::String: fits = std::holds_alternative<std::string>(value); break;
        case FieldType::Vec2: fits = std::holds_alternative<Vec2>(value); break;
        case FieldType::Vec3: fits = std::holds_alternative<k3::Math::Vector3f>(value); break;
        case FieldType::Vec4: fits = std::holds_alternative<k3::Math::Vector4f>(value); break;
        case FieldType::Enum: fits = std::holds_alternative<std::string>(value); break;
        case FieldType::Entity: fits = std::holds_alternative<EntityRef>(value); break;
    }
    if (!fits) {
        return {false, std::format("{}.{} is {}", component, fieldName, typeName(target->type))};
    }
    found->add(world, entity);
    if (const std::string why = target->write(found->get(world, entity), value); !why.empty()) {
        return {false, std::format("{}.{}", component, why)};
    }
    return {};
}

kuge::Json kuge::ComponentRegistry::valueToJson(const FieldValue& value)
{
    struct Visitor
    {
        Json operator()(bool v) const { return Json(v); }
        Json operator()(std::int64_t v) const { return Json(v); }
        Json operator()(double v) const { return Json(v); }
        Json operator()(const std::string& v) const { return Json(v); }
        Json operator()(const Vec2& v) const { return Json::array().push(v.x).push(v.y); }
        Json operator()(const k3::Math::Vector3f& v) const { return Json::array().push(v.x).push(v.y).push(v.z); }
        Json operator()(const k3::Math::Vector4f& v) const { return Json::array().push(v.x).push(v.y).push(v.z).push(v.w); }
        Json operator()(const EntityRef& v) const { return Json(static_cast<std::uint64_t>(v.id)); }
    };

    return std::visit(Visitor{}, value);
}

std::optional<kuge::FieldValue> kuge::ComponentRegistry::valueFromJson(const FieldInfo& field, const Json& json, std::string& why)
{
    const auto numbers = [&](std::size_t count) -> std::optional<std::vector<float>> {
        if (!json.isArray() || json.size() != count) {
            why = std::format("{} is {}", field.name, typeName(field.type));
            return std::nullopt;
        }
        std::vector<float> values;

        for (const Json& item : json.items()) {
            if (!item.isNumber()) {
                why = std::format("{} is {}", field.name, typeName(field.type));
                return std::nullopt;
            }
            values.push_back(static_cast<float>(item.number()));
        }
        return values;
    };

    switch (field.type) {
        case FieldType::Bool:
            if (json.isBool()) {
                return FieldValue(json.boolean());
            }
            break;
        case FieldType::Int:
            if (json.isNumber() && json.number() == std::floor(json.number())) {
                return FieldValue(json.integer());
            }
            break;
        case FieldType::Float:
            if (json.isNumber()) {
                return FieldValue(json.number());
            }
            break;
        case FieldType::String:
        case FieldType::Enum:
            if (json.isString()) {
                return FieldValue(json.string());
            }
            break;
        case FieldType::Entity:
            if (json.isNumber() && json.number() >= 0 && json.number() == std::floor(json.number())) {
                return FieldValue(EntityRef{static_cast<kw::Entity>(json.integer())});
            }
            break;
        case FieldType::Vec2:
            if (const auto v = numbers(2)) {
                return FieldValue(Vec2{(*v)[0], (*v)[1]});
            }
            return std::nullopt;
        case FieldType::Vec3:
            if (const auto v = numbers(3)) {
                return FieldValue(k3::Math::Vector3f{(*v)[0], (*v)[1], (*v)[2]});
            }
            return std::nullopt;
        case FieldType::Vec4:
            if (const auto v = numbers(4)) {
                return FieldValue(k3::Math::Vector4f{(*v)[0], (*v)[1], (*v)[2], (*v)[3]});
            }
            return std::nullopt;
    }
    why = std::format("{} is {}", field.name, typeName(field.type));
    return std::nullopt;
}

kuge::Json kuge::ComponentRegistry::toJson(kw::World& world, kw::Entity entity, std::string_view component) const
{
    Json out = Json::object();

    for (const auto& [name, value] : get(world, entity, component)) {
        out.set(name, valueToJson(value));
    }
    return out;
}

kuge::ComponentRegistry::Result kuge::ComponentRegistry::fromJson(kw::World& world, kw::Entity entity, std::string_view component, const Json& json) const
{
    const Component* found = byName(component);

    if (!found) {
        return {false, std::format("there is no component '{}'", component)};
    }
    if (!json.isObject()) {
        return {false, std::format("{} must be an object of fields", component)};
    }
    // Everything is checked before anything changes
    std::vector<std::pair<const FieldInfo*, FieldValue>> checked;

    for (const auto& [name, member] : json.members()) {
        const FieldInfo* target = nullptr;

        for (const FieldInfo& field : found->fields) {
            if (field.name == name) {
                target = &field;
            }
        }
        if (!target) {
            return {false, std::format("{} has no field '{}'", component, name)};
        }
        std::string why;
        auto value = valueFromJson(*target, member, why);

        if (!value) {
            return {false, std::format("{}.{}", component, why)};
        }
        checked.emplace_back(target, std::move(*value));
    }
    // A trial on a copy of the component's state is not possible without a copy constructor in the registry: the values are checked for their
    // limits by writing them in a scratch component made the same way
    const bool hadIt = found->has(world, entity);

    found->add(world, entity);
    void* data = found->get(world, entity);
    std::vector<std::pair<const FieldInfo*, FieldValue>> before;

    for (const auto& [field, value] : checked) {
        before.emplace_back(field, field->read(data));
    }
    for (const auto& [field, value] : checked) {
        if (std::string why = field->write(data, value); !why.empty()) {
            for (const auto& [undo, old] : before) {   // put back what was written
                undo->write(data, old);
            }
            if (!hadIt) {
                found->remove(world, entity);
            }
            return {false, std::format("{}.{}", component, why)};
        }
    }
    return {};
}

kuge::Json kuge::ComponentRegistry::entityToJson(kw::World& world, kw::Entity entity) const
{
    Json out = Json::object();

    for (const std::string& name : componentsOf(world, entity)) {
        out.set(name, toJson(world, entity, name));
    }
    return out;
}

kuge::ComponentRegistry::Result kuge::ComponentRegistry::entityFromJson(kw::World& world, kw::Entity entity, const Json& json) const
{
    if (!json.isObject()) {
        return {false, "an entity must be an object of components"};
    }
    // The components are checked first (a component that is not described, or not an object), then read
    for (const auto& [name, member] : json.members()) {
        if (!byName(name)) {
            return {false, std::format("there is no component '{}'", name)};
        }
        if (!member.isObject()) {
            return {false, std::format("{} must be an object of fields", name)};
        }
    }
    std::vector<std::string> added;

    for (const auto& [name, member] : json.members()) {
        const bool hadIt = has(world, entity, name);
        const Result result = fromJson(world, entity, name, member);

        if (!result.ok) {
            for (const std::string& undo : added) {   // the components this call added go again
                remove(world, entity, undo);
            }
            return result;
        }
        if (!hadIt) {
            added.push_back(name);
        }
    }
    return {};
}

void kuge::describeCore(ComponentRegistry& registry)
{
    registry.describe<Transform2D>("Transform2D", {
        field<Transform2D>("position", &Transform2D::position),
        field<Transform2D>("rotation", &Transform2D::rotation, {.help = "Degrees, clockwise"}),
        field<Transform2D>("scale", &Transform2D::scale),
    });
    registry.describe<Transform3D>("Transform3D", {
        field<Transform3D>("translation", &Transform3D::translation, {.help = "Meters, y up"}),
        field<Transform3D>("rotation", &Transform3D::rotation, {.help = "A unit quaternion x, y, z, w"}),
        field<Transform3D>("scale", &Transform3D::scale),
    });
    registry.describe<Collider3D>("Collider3D", {
        field<Collider3D>("shape", &Collider3D::shape, {.names = {"Box", "Sphere", "Capsule"}}),
        field<Collider3D>("size", &Collider3D::size),
        field<Collider3D>("offset", &Collider3D::offset),
        field<Collider3D>("layer", &Collider3D::layer),
        field<Collider3D>("mask", &Collider3D::mask),
        field<Collider3D>("trigger", &Collider3D::trigger),
        field<Collider3D>("friction", &Collider3D::friction, {.min = 0.0}),
        field<Collider3D>("restitution", &Collider3D::restitution, {.min = 0.0, .max = 1.0}),
    });
    registry.describe<Body3D>("Body3D", {
        field<Body3D>("type", &Body3D::type, {.names = {"Static", "Kinematic", "Dynamic"}}),
        field<Body3D>("velocity", &Body3D::velocity),
        field<Body3D>("gravityScale", &Body3D::gravityScale),
        field<Body3D>("mass", &Body3D::mass, {.min = 0.0, .help = "Kilograms; 0: from the size"}),
        field<Body3D>("linearDamping", &Body3D::linearDamping, {.min = 0.0}),
        field<Body3D>("angularDamping", &Body3D::angularDamping, {.min = 0.0}),
        field<Body3D>("lockRotation", &Body3D::lockRotation),
    });
    registry.describe<Character3D>("Character3D", {
        field<Character3D>("radius", &Character3D::radius, {.min = 0.01}),
        field<Character3D>("height", &Character3D::height, {.min = 0.1}),
        field<Character3D>("crouchHeight", &Character3D::crouchHeight, {.min = 0.1}),
        field<Character3D>("stepHeight", &Character3D::stepHeight, {.min = 0.0}),
        field<Character3D>("maxSlope", &Character3D::maxSlope, {.min = 0.0, .max = 89.0, .help = "Degrees"}),
        field<Character3D>("jumpSpeed", &Character3D::jumpSpeed, {.min = 0.0}),
        field<Character3D>("gravityScale", &Character3D::gravityScale),
    });
}
