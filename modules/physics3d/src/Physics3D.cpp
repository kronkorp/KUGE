#include "Physics3D.hpp"
#include "Time.hpp"
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace
{

    // Jolt keeps its types and its allocator in globals: set up by the first Physics3D, undone by the last
    std::mutex joltMutex;
    int        joltUsers = 0;

    struct JoltGuard
    {
        JoltGuard(void)
        {
            const std::lock_guard lock(joltMutex);

            if (joltUsers++ == 0) {
                JPH::RegisterDefaultAllocator();
                JPH::Factory::sInstance = new JPH::Factory();
                JPH::RegisterTypes();
            }
        }

        ~JoltGuard(void)
        {
            const std::lock_guard lock(joltMutex);

            if (--joltUsers == 0) {
                JPH::UnregisterTypes();
                delete JPH::Factory::sInstance;
                JPH::Factory::sInstance = nullptr;
            }
        }
    };

    // Two layers: what does not move, and what does. (KUGE's own layers and masks are checked by the
    // contact listener and the queries: Jolt's only go to 16 bits.)
    constexpr JPH::ObjectLayer NON_MOVING = 0;
    constexpr JPH::ObjectLayer MOVING = 1;

    class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
    {
        public:
            JPH::uint GetNumBroadPhaseLayers(void) const override { return 2; }

            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
            {
                return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(layer == NON_MOVING ? 0 : 1));
            }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
            {
                return static_cast<JPH::BroadPhaseLayer::Type>(layer) == 0 ? "NON_MOVING" : "MOVING";
            }
#endif
    };

    class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
    {
        public:
            bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override
            {
                return layer == MOVING || static_cast<JPH::BroadPhaseLayer::Type>(broadPhase) == 1;
            }
    };

    class ObjectPair final : public JPH::ObjectLayerPairFilter
    {
        public:
            bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override { return a == MOVING || b == MOVING; }
    };

    JPH::Vec3 toJolt(const k3::Math::Vector3f& v) { return {v.x, v.y, v.z}; }
    k3::Math::Vector3f fromJolt(JPH::Vec3 v) { return {v.GetX(), v.GetY(), v.GetZ()}; }

    JPH::Quat toJolt(const k3::Math::Vector4f& q) { return {q.x, q.y, q.z, q.w}; }

    // The shape of a collider, stretched by the scale of the entity
    JPH::RefConst<JPH::Shape> makeShape(const kuge::Collider3D& collider, const k3::Math::Vector3f& scale)
    {
        constexpr float MINIMUM = 0.01f;
        const float sx = std::max(std::fabs(scale.x), MINIMUM);
        const float sy = std::max(std::fabs(scale.y), MINIMUM);
        const float sz = std::max(std::fabs(scale.z), MINIMUM);
        JPH::RefConst<JPH::Shape> shape;

        switch (collider.shape) {
            case kuge::Collider3D::Shape::Box: {
                const JPH::Vec3 half(std::max(collider.size.x * sx * 0.5f, MINIMUM), std::max(collider.size.y * sy * 0.5f, MINIMUM),
                    std::max(collider.size.z * sz * 0.5f, MINIMUM));

                shape = new JPH::BoxShape(half, std::min(JPH::cDefaultConvexRadius, half.ReduceMin()));
                break;
            }
            case kuge::Collider3D::Shape::Sphere:
                shape = new JPH::SphereShape(std::max(collider.size.x * std::max({sx, sy, sz}), MINIMUM));
                break;
            case kuge::Collider3D::Shape::Capsule: {
                const float radius = std::max(collider.size.x * std::max(sx, sz), MINIMUM);

                shape = new JPH::CapsuleShape(std::max(collider.size.y * sy * 0.5f - radius, MINIMUM), radius);
                break;
            }
        }
        if (collider.offset.x != 0.0f || collider.offset.y != 0.0f || collider.offset.z != 0.0f) {
            shape = JPH::RotatedTranslatedShapeSettings(JPH::Vec3(collider.offset.x * scale.x, collider.offset.y * scale.y,
                collider.offset.z * scale.z), JPH::Quat::sIdentity(), shape).Create().Get();
        }
        return shape;
    }

    using Vec3 = k3::Math::Vector3f;
    using Quat = k3::Math::Vector4f;

    Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
    float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

    Vec3 rotate(const Quat& q, const Vec3& v)
    {
        const Vec3 u{q.x, q.y, q.z};
        const Vec3 t = cross(u, v) * 2.0f;

        return v + t * q.w + cross(u, t);
    }

    Quat conjugate(const Quat& q) { return {-q.x, -q.y, -q.z, q.w}; }

    Quat multiply(const Quat& a, const Quat& b)
    {
        return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
    }

    struct PartHit
    {
        float distance;
        Vec3  normal;   // in the space of the part
    };

    // The ray (unit direction) against a sphere, from outside it
    std::optional<PartHit> raySphere(const Vec3& origin, const Vec3& direction, const Vec3& center, float radius)
    {
        const Vec3 from = origin - center;
        const float b = dot(from, direction);
        const float c = dot(from, from) - radius * radius;

        if (c <= 0.0f) {
            return std::nullopt;   // starts inside: not a hit
        }
        const float disc = b * b - c;

        if (disc < 0.0f || b > 0.0f) {
            return std::nullopt;
        }
        const float t = -b - std::sqrt(disc);
        const Vec3 normal = (from + direction * t) * (1.0f / radius);

        return PartHit{t, normal};
    }

    // Against a box centered on the origin, with its sides along the axes
    std::optional<PartHit> rayBox(const Vec3& origin, const Vec3& direction, const Vec3& half)
    {
        float near = -std::numeric_limits<float>::max();
        float far = std::numeric_limits<float>::max();
        Vec3 normal{};
        const float o[3] = {origin.x, origin.y, origin.z};
        const float d[3] = {direction.x, direction.y, direction.z};
        const float h[3] = {half.x, half.y, half.z};

        for (int axis = 0; axis < 3; ++axis) {
            if (std::fabs(d[axis]) < 1e-9f) {
                if (std::fabs(o[axis]) > h[axis]) {
                    return std::nullopt;
                }
                continue;
            }
            float t0 = (-h[axis] - o[axis]) / d[axis];
            float t1 = (h[axis] - o[axis]) / d[axis];
            float sign = -1.0f;   // entering through the face on the side the ray comes from

            if (t0 > t1) {
                std::swap(t0, t1);
                sign = 1.0f;
            }
            if (t0 > near) {
                near = t0;
                normal = {axis == 0 ? sign : 0.0f, axis == 1 ? sign : 0.0f, axis == 2 ? sign : 0.0f};
            }
            far = std::min(far, t1);
            if (near > far) {
                return std::nullopt;
            }
        }
        if (near < 0.0f) {
            return std::nullopt;   // starts inside, or the box is behind
        }
        return PartHit{near, normal};
    }

    // Against a capsule standing along y, centered on the origin
    std::optional<PartHit> rayCapsule(const Vec3& origin, const Vec3& direction, float radius, float halfSegment)
    {
        std::optional<PartHit> best;
        const auto consider = [&best](const std::optional<PartHit>& hit) {
            if (hit && (!best || hit->distance < best->distance)) {
                best = hit;
            }
        };
        const float a = direction.x * direction.x + direction.z * direction.z;
        const float b = origin.x * direction.x + origin.z * direction.z;
        const float c = origin.x * origin.x + origin.z * origin.z - radius * radius;

        // Inside the cylinder part: not a hit
        if (c <= 0.0f && std::fabs(origin.y) <= halfSegment) {
            return std::nullopt;
        }
        if (a > 1e-12f) {
            const float disc = b * b - a * c;

            if (disc >= 0.0f) {
                const float t = (-b - std::sqrt(disc)) / a;
                const float y = origin.y + direction.y * t;

                if (t >= 0.0f && std::fabs(y) <= halfSegment) {
                    consider(PartHit{t, Vec3{origin.x + direction.x * t, 0.0f, origin.z + direction.z * t} * (1.0f / radius)});
                }
            }
        }
        consider(raySphere(origin, direction, {0.0f, halfSegment, 0.0f}, radius));
        consider(raySphere(origin, direction, {0.0f, -halfSegment, 0.0f}, radius));
        return best;
    }

    // Where a part is in the world, and its size with the scale of the entity
    struct PlacedPart
    {
        Vec3 center;
        Quat rotation;
        Vec3 size;
    };

    PlacedPart place(const kuge::Transform3D& transform, const kuge::Hitboxes& boxes, const kuge::Hitbox& part)
    {
        const Vec3& scale = transform.scale;
        Vec3 local = part.offset;
        Quat spin{0.0f, 0.0f, 0.0f, 1.0f};

        if (!part.bone.empty()) {
            for (const kuge::BonePlace& bone : boxes.bones) {
                if (bone.name == part.bone) {
                    local = bone.translation + rotate(bone.rotation, part.offset);
                    spin = bone.rotation;
                    break;
                }
            }
        }
        const Vec3 scaled{local.x * scale.x, local.y * scale.y, local.z * scale.z};
        const float sx = std::fabs(scale.x), sy = std::fabs(scale.y), sz = std::fabs(scale.z);
        Vec3 size = part.size;

        switch (part.shape) {
            case kuge::Hitbox::Shape::Box:
                size = {part.size.x * sx, part.size.y * sy, part.size.z * sz};
                break;
            case kuge::Hitbox::Shape::Sphere:
                size = {part.size.x * std::max({sx, sy, sz}), 0.0f, 0.0f};
                break;
            case kuge::Hitbox::Shape::Capsule:
                size = {part.size.x * std::max(sx, sz), part.size.y * sy, 0.0f};
                break;
        }
        return {transform.translation + rotate(transform.rotation, scaled), multiply(transform.rotation, spin), size};
    }

    std::optional<PartHit> rayPart(const PlacedPart& placed, const kuge::Hitbox& part, const Vec3& origin, const Vec3& direction)
    {
        const Quat back = conjugate(placed.rotation);
        const Vec3 o = rotate(back, origin - placed.center);
        const Vec3 d = rotate(back, direction);
        std::optional<PartHit> hit;

        switch (part.shape) {
            case kuge::Hitbox::Shape::Box:
                hit = rayBox(o, d, placed.size * 0.5f);
                break;
            case kuge::Hitbox::Shape::Sphere:
                hit = raySphere(o, d, {}, std::max(placed.size.x, 0.001f));
                break;
            case kuge::Hitbox::Shape::Capsule: {
                const float radius = std::max(placed.size.x, 0.001f);

                hit = rayCapsule(o, d, radius, std::max(placed.size.y * 0.5f - radius, 0.0f));
                break;
            }
        }
        if (hit) {
            hit->normal = rotate(placed.rotation, hit->normal);
        }
        return hit;
    }

}

struct kuge::Physics3D::Impl final : public JPH::ContactListener
{
    // What the physics keeps of an entity that is a body
    struct Record
    {
        JPH::BodyID        id;
        Collider3D         collider;
        bool               hasBody = false;
        Body3D             body;                // as made (to see what changed)
        k3::Math::Vector3f scale{1.0f, 1.0f, 1.0f};
        Transform3D        written;             // what the Transform3D was when the physics last touched it
        k3::Math::Vector3f velocityWritten{};
        std::uint64_t      stamp = 0;           // the tick that saw it last
    };

    JoltGuard                    guard;         // first: it goes last
    Physics3DConfig              config;
    JPH::TempAllocatorImpl       temp;
    JPH::JobSystemSingleThreaded jobs;
    BroadPhaseLayers             broadPhase;
    ObjectVsBroadPhase           objectVsBroadPhase;
    ObjectPair                   objectPair;
    JPH::PhysicsSystem           system;

    std::unordered_map<kw::Entity, Record>   records;
    std::unordered_map<std::uint32_t, kw::Entity> entityOf;   // by Jolt's body id
    std::vector<kw::Entity>                  groundedNow;
    std::vector<TriggerEvent3D>              happened;
    std::uint64_t                            tick = 0;

    // A character is not a body: Jolt's CharacterVirtual moves it, and queries do not see it. (After `system`: it goes first.)
    struct CharacterRecord
    {
        std::unique_ptr<JPH::CharacterVirtual> character;
        JPH::RefConst<JPH::Shape>              standing;
        JPH::RefConst<JPH::Shape>              crouched;
        float                                  radius = 0;
        float                                  height = 0;
        float                                  crouchHeight = 0;
        float                                  maxSlope = 0;
        bool                                   crouching = false;
        Transform3D                            written;   // what the Transform3D was when we last wrote it
    };
    std::unordered_map<kw::Entity, CharacterRecord> characters;

    explicit Impl(const Physics3DConfig& settings)
        : config(settings), temp(static_cast<JPH::uint>(settings.tempMemory)), jobs(JPH::cMaxPhysicsJobs)
    {
        system.Init(settings.maxBodies, 0, std::max<JPH::uint>(settings.maxBodies * 8, 4096), settings.maxContacts, broadPhase,
            objectVsBroadPhase, objectPair);
        system.SetGravity(toJolt(settings.gravity));
        system.SetContactListener(this);
    }

    const Record* recordOf(const JPH::Body& body) const
    {
        const auto found = records.find(static_cast<kw::Entity>(body.GetUserData()));

        return found == records.end() ? nullptr : &found->second;
    }

    // -- Contacts (called by Jolt, on this thread, during Update) ---------------------------------

    JPH::ValidateResult OnContactValidate(const JPH::Body& a, const JPH::Body& b, JPH::RVec3Arg, const JPH::CollideShapeResult&) override
    {
        const Record* first = recordOf(a);
        const Record* second = recordOf(b);

        if (first && second && !compatible(first->collider, second->collider)) {
            return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
        }
        return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    void touch(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold, JPH::ContactSettings& settings, bool added)
    {
        const Record* first = recordOf(a);
        const Record* second = recordOf(b);

        if (!first || !second || !compatible(first->collider, second->collider)) {
            return;
        }
        // A character (upright) does not rub against a wall: friction is for the ground. Its velocity is set by the
        // game at each tick, so a wall that held it would hold it up in the air.
        const bool character = (first->hasBody && first->body.lockRotation) || (second->hasBody && second->body.lockRotation);

        if (character && std::fabs(manifold.mWorldSpaceNormal.GetY()) < 0.7f) {
            settings.mCombinedFriction = 0.0f;
        }
        if (a.IsSensor() || b.IsSensor()) {
            if (added) {
                const bool firstIsTrigger = a.IsSensor();

                happened.push_back({static_cast<kw::Entity>((firstIsTrigger ? a : b).GetUserData()),
                    static_cast<kw::Entity>((firstIsTrigger ? b : a).GetUserData()), true});
            }
            return;
        }
        // The normal goes from the first body to the second: a body is on the ground if what it touches is below it
        constexpr float STEEPEST = 0.7f;

        if (manifold.mWorldSpaceNormal.GetY() > STEEPEST) {
            groundedNow.push_back(static_cast<kw::Entity>(b.GetUserData()));
        } else if (manifold.mWorldSpaceNormal.GetY() < -STEEPEST) {
            groundedNow.push_back(static_cast<kw::Entity>(a.GetUserData()));
        }
    }

    void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold, JPH::ContactSettings& settings) override
    {
        touch(a, b, manifold, settings, true);
    }

    void OnContactPersisted(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold, JPH::ContactSettings& settings) override
    {
        touch(a, b, manifold, settings, false);
    }

    void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
    {
        const auto first = entityOf.find(pair.GetBody1ID().GetIndexAndSequenceNumber());
        const auto second = entityOf.find(pair.GetBody2ID().GetIndexAndSequenceNumber());

        if (first == entityOf.end() || second == entityOf.end()) {
            return;   // one of them was removed: nobody is left to be told
        }
        const bool firstIsTrigger = records.at(first->second).collider.trigger;
        const bool secondIsTrigger = records.at(second->second).collider.trigger;

        if (firstIsTrigger != secondIsTrigger) {
            happened.push_back({firstIsTrigger ? first->second : second->second, firstIsTrigger ? second->second : first->second, false});
        }
    }

    // -- The bodies ------------------------------------------------------------------------------

    static bool sameMaking(const Record& record, const Collider3D& collider, const Body3D* body, const k3::Math::Vector3f& scale)
    {
        return record.collider == collider && record.hasBody == (body != nullptr) && sameVector(record.scale, scale)
            && (!body || (record.body.type == body->type && record.body.mass == body->mass
                && record.body.gravityScale == body->gravityScale && record.body.lockRotation == body->lockRotation
                && record.body.linearDamping == body->linearDamping && record.body.angularDamping == body->angularDamping));
    }

    void make(Record& record, kw::Entity entity, const Transform3D& transform, const Collider3D& collider, const Body3D* body)
    {
        JPH::BodyInterface& bodies = system.GetBodyInterface();
        Body3D::Type type = body ? body->type : Body3D::Type::Static;

        if (collider.trigger && type == Body3D::Type::Dynamic) {
            type = Body3D::Type::Kinematic;   // a zone does not need to fall
        }
        const JPH::EMotionType motion = type == Body3D::Type::Static ? JPH::EMotionType::Static
            : type == Body3D::Type::Kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic;
        JPH::BodyCreationSettings settings(makeShape(collider, transform.scale), toJolt(transform.translation), toJolt(transform.rotation),
            motion, motion == JPH::EMotionType::Static ? NON_MOVING : MOVING);

        settings.mUserData = entity;
        settings.mIsSensor = collider.trigger;
        settings.mFriction = collider.friction;
        settings.mRestitution = collider.restitution;
        if (body) {
            settings.mLinearVelocity = toJolt(body->velocity);
            settings.mGravityFactor = body->gravityScale;
            settings.mLinearDamping = body->linearDamping;
            settings.mAngularDamping = body->angularDamping;
            if (body->lockRotation) {
                settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
            }
            if (body->mass > 0.0f && motion == JPH::EMotionType::Dynamic) {
                settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                settings.mMassPropertiesOverride.mMass = body->mass;
            }
        }
        record.id = bodies.CreateAndAddBody(settings, JPH::EActivation::Activate);
        record.collider = collider;
        record.hasBody = body != nullptr;
        record.body = body ? *body : Body3D{};
        record.scale = transform.scale;
        record.written = transform;
        record.velocityWritten = body ? body->velocity : k3::Math::Vector3f{};
        entityOf[record.id.GetIndexAndSequenceNumber()] = entity;
    }

    void drop(Record& record)
    {
        JPH::BodyInterface& bodies = system.GetBodyInterface();

        entityOf.erase(record.id.GetIndexAndSequenceNumber());
        bodies.RemoveBody(record.id);
        bodies.DestroyBody(record.id);
    }

    void step(kw::World& world, float dt, std::vector<TriggerEvent3D>& events)
    {
        JPH::BodyInterface& bodies = system.GetBodyInterface();
        std::vector<kw::Entity> entities;

        ++tick;
        for (kw::Entity entity : world.view<Transform3D, Collider3D>()) {
            entities.push_back(entity);
        }
        std::sort(entities.begin(), entities.end());
        // Into Jolt: new bodies, changed shapes, and what the game moved since the last tick
        for (kw::Entity entity : entities) {
            const Transform3D& transform = world.get<Transform3D>(entity);
            const Collider3D& collider = world.get<Collider3D>(entity);
            const Body3D* body = world.has<Body3D>(entity) ? &world.get<Body3D>(entity) : nullptr;
            auto found = records.find(entity);

            if (found != records.end() && !sameMaking(found->second, collider, body, transform.scale)) {
                drop(found->second);
                records.erase(found);
                found = records.end();
            }
            if (found == records.end()) {
                Record& record = records[entity];

                make(record, entity, transform, collider, body);
                record.stamp = tick;
                continue;
            }
            Record& record = found->second;

            record.stamp = tick;
            if (!(transform == record.written)) {
                bodies.SetPositionAndRotation(record.id, toJolt(transform.translation), toJolt(transform.rotation), JPH::EActivation::Activate);
                record.written = transform;
            }
            if (body && record.body.type != Body3D::Type::Static && !sameVector(body->velocity, record.velocityWritten)) {
                bodies.SetLinearVelocity(record.id, toJolt(body->velocity));
                bodies.ActivateBody(record.id);
                record.velocityWritten = body->velocity;
            }
        }
        // Out of Jolt: the bodies of entities that are gone (or lost their collider)
        std::vector<kw::Entity> gone;

        for (const auto& [entity, record] : records) {
            if (record.stamp != tick) {
                gone.push_back(entity);
            }
        }
        std::sort(gone.begin(), gone.end());
        for (kw::Entity entity : gone) {
            drop(records.at(entity));
            records.erase(entity);
        }
        // Characters whose entity is gone (or is not one any more)
        if (!characters.empty()) {
            std::vector<kw::Entity> walking;

            for (kw::Entity entity : world.view<Character3D>()) {
                walking.push_back(entity);
            }
            std::erase_if(characters, [&](const auto& item) {
                return std::find(walking.begin(), walking.end(), item.first) == walking.end();
            });
        }
        groundedNow.clear();
        happened.clear();
        system.Update(dt, 1, &temp, &jobs);
        // Back to the entities: where Jolt left the bodies that are awake
        std::sort(groundedNow.begin(), groundedNow.end());
        for (kw::Entity entity : entities) {
            Record& record = records.at(entity);

            if (!record.hasBody || record.body.type == Body3D::Type::Static || !bodies.IsActive(record.id)) {
                continue;   // (asleep: nothing moved, and it stays as it was, standing on what it stood on)
            }
            JPH::RVec3 position;
            JPH::Quat rotation;
            Transform3D& transform = world.get<Transform3D>(entity);
            Body3D& body = world.get<Body3D>(entity);

            bodies.GetPositionAndRotation(record.id, position, rotation);
            transform.translation = fromJolt(position);
            transform.rotation = {rotation.GetX(), rotation.GetY(), rotation.GetZ(), rotation.GetW()};
            record.written = transform;
            body.velocity = fromJolt(bodies.GetLinearVelocity(record.id));
            record.velocityWritten = body.velocity;
            body.grounded = std::binary_search(groundedNow.begin(), groundedNow.end(), entity);
        }
        std::sort(happened.begin(), happened.end(), [](const TriggerEvent3D& a, const TriggerEvent3D& b) {
            if (a.trigger != b.trigger) {
                return a.trigger < b.trigger;
            }
            return a.other != b.other ? a.other < b.other : a.entered < b.entered;
        });
        events = happened;
    }

    // -- Characters --------------------------------------------------------------------------------

    // A capsule standing on the origin (the feet), `height` tall
    static JPH::RefConst<JPH::Shape> characterShape(float radius, float height)
    {
        constexpr float MINIMUM = 0.01f;
        const float r = std::max(radius, MINIMUM);
        const float h = std::max(height, 2.0f * r + MINIMUM);
        JPH::RefConst<JPH::Shape> capsule = new JPH::CapsuleShape(h * 0.5f - r, r);

        return JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0.0f, h * 0.5f, 0.0f), JPH::Quat::sIdentity(), capsule).Create().Get();
    }

    CharacterRecord& characterOf(kw::Entity entity, const Transform3D& transform, const Character3D& settings)
    {
        auto found = characters.find(entity);

        if (found != characters.end() && (found->second.radius != settings.radius || found->second.height != settings.height
                || found->second.crouchHeight != settings.crouchHeight || found->second.maxSlope != settings.maxSlope)) {
            characters.erase(found);
            found = characters.end();
        }
        if (found != characters.end()) {
            return found->second;
        }
        CharacterRecord record;
        JPH::CharacterVirtualSettings made;

        record.radius = settings.radius;
        record.height = settings.height;
        record.crouchHeight = settings.crouchHeight;
        record.maxSlope = settings.maxSlope;
        record.standing = characterShape(settings.radius, settings.height);
        record.crouched = characterShape(settings.radius, settings.crouchHeight);
        made.mShape = record.standing;
        made.mUp = JPH::Vec3::sAxisY();
        made.mMaxSlopeAngle = JPH::DegreesToRadians(settings.maxSlope);
        made.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -std::max(settings.radius, 0.01f));   // only the bottom of the capsule supports it
        record.character = std::make_unique<JPH::CharacterVirtual>(&made, toJolt(transform.translation), JPH::Quat::sIdentity(), 0, &system);
        record.written = transform;
        return characters.emplace(entity, std::move(record)).first->second;
    }

    void moveCharacter(kw::World& world, kw::Entity entity, const CharacterMove& move, float dt)
    {
        Transform3D& transform = world.get<Transform3D>(entity);
        Character3D& settings = world.get<Character3D>(entity);
        CharacterRecord& record = characterOf(entity, transform, settings);
        JPH::CharacterVirtual& character = *record.character;
        const Filter filter(*this, settings.mask, false);
        const JPH::DefaultBroadPhaseLayerFilter broad = system.GetDefaultBroadPhaseLayerFilter(MOVING);
        const JPH::DefaultObjectLayerFilter layers = system.GetDefaultLayerFilter(MOVING);
        const JPH::BodyFilter& bodies = filter;

        if (!(transform == record.written)) {   // the game moved it (a teleport, a correction): from there
            character.SetPosition(toJolt(transform.translation));
            character.RefreshContacts(broad, layers, bodies, {}, temp);
        }
        // Stance: down at once, up only where there is room
        if (move.crouch != record.crouching) {
            const bool toCrouch = move.crouch;

            if (character.SetShape(toCrouch ? record.crouched.GetPtr() : record.standing.GetPtr(),
                    toCrouch ? FLT_MAX : 1.5f * character.GetCharacterPadding(), broad, layers, bodies, {}, temp)) {
                record.crouching = toCrouch;
            }
        }
        // The velocity: what the ground gives and the wish on it, or what falls and the wish in the air
        const JPH::Vec3 up = JPH::Vec3::sAxisY();
        const JPH::Vec3 fall(0.0f, character.GetLinearVelocity().GetY(), 0.0f);
        const JPH::Vec3 ground = character.GetGroundVelocity();
        const bool towardsGround = fall.GetY() - ground.GetY() < 0.1f;
        const bool standing = character.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround
            && !character.IsSlopeTooSteep(character.GetGroundNormal());
        const JPH::Vec3 gravity = system.GetGravity() * settings.gravityScale;
        JPH::Vec3 velocity = standing && towardsGround ? ground : fall;

        if (standing && towardsGround && move.jump) {
            velocity += settings.jumpSpeed * up;
        }
        velocity += gravity * dt + JPH::Vec3(move.wish.x, 0.0f, move.wish.z);
        character.SetLinearVelocity(velocity);

        JPH::CharacterVirtual::ExtendedUpdateSettings walking;

        walking.mStickToFloorStepDown = JPH::Vec3(0.0f, -std::max(settings.stepHeight, 0.01f), 0.0f);
        walking.mWalkStairsStepUp = JPH::Vec3(0.0f, settings.stepHeight, 0.0f);
        character.ExtendedUpdate(dt, gravity, walking, broad, layers, bodies, {}, temp);

        transform.translation = fromJolt(character.GetPosition());
        record.written = transform;
        settings.velocity = fromJolt(character.GetLinearVelocity());
        settings.onGround = character.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
        settings.crouching = record.crouching;
    }

    // -- Questions -------------------------------------------------------------------------------

    class Filter final : public JPH::BodyFilter
    {
        public:
            Filter(const Impl& impl, std::uint32_t mask, bool triggers, const kuge::EntityFilter* ignore = nullptr)
                : m_impl(impl), m_mask(mask), m_triggers(triggers), m_ignore(ignore) {}

            bool ShouldCollide(const JPH::BodyID& id) const override
            {
                const auto entity = m_impl.entityOf.find(id.GetIndexAndSequenceNumber());

                if (entity == m_impl.entityOf.end()) {
                    return false;
                }
                const Collider3D& collider = m_impl.records.at(entity->second).collider;

                if (m_ignore && *m_ignore && (*m_ignore)(entity->second)) {
                    return false;
                }
                return (m_triggers || !collider.trigger) && (collider.layer & m_mask) != 0;
            }

        private:
            const Impl&                  m_impl;
            std::uint32_t                m_mask;
            bool                         m_triggers;
            const kuge::EntityFilter*    m_ignore;
    };
};

kuge::Physics3D::Physics3D(Physics3DConfig config) : m_impl(std::make_unique<Impl>(config)) {}

kuge::Physics3D::~Physics3D(void) = default;

void kuge::Physics3D::step(kw::World& world, float dt)
{
    m_impl->step(world, dt, m_events);
}

void kuge::Physics3D::moveCharacter(kw::World& world, kw::Entity entity, const CharacterMove& move, float dt)
{
    m_impl->moveCharacter(world, entity, move, dt);
}

std::size_t kuge::Physics3D::bodyCount(void) const noexcept
{
    return m_impl->system.GetNumBodies();
}

std::optional<kuge::RaycastHit3D> kuge::Physics3D::raycast(k3::Math::Vector3f origin, k3::Math::Vector3f direction,
    float maxDistance, std::uint32_t mask, bool triggers, const EntityFilter& ignore) const
{
    const JPH::Vec3 along = toJolt(direction).NormalizedOr(JPH::Vec3::sAxisZ());
    const JPH::RRayCast ray(toJolt(origin), along * maxDistance);
    JPH::RayCastResult hit;

    if (!m_impl->system.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, Impl::Filter(*m_impl, mask, triggers, &ignore))) {
        return std::nullopt;
    }
    RaycastHit3D result;
    const JPH::Vec3 point = toJolt(origin) + along * (maxDistance * hit.mFraction);
    const JPH::BodyLockRead lock(m_impl->system.GetBodyLockInterface(), hit.mBodyID);

    result.entity = m_impl->entityOf.at(hit.mBodyID.GetIndexAndSequenceNumber());
    result.point = fromJolt(point);
    result.distance = maxDistance * hit.mFraction;
    if (lock.Succeeded()) {
        result.normal = fromJolt(lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point));
    }
    return result;
}

std::optional<kuge::RaycastHit3D> kuge::Physics3D::sweepCapsule(k3::Math::Vector3f origin, k3::Math::Vector3f direction,
    float maxDistance, float radius, float height, std::uint32_t mask, bool triggers, const EntityFilter& ignore) const
{
    constexpr float MINIMUM = 0.01f;
    const float r = std::max(radius, MINIMUM);
    const JPH::CapsuleShape capsule(std::max(height * 0.5f - r, MINIMUM), r);
    const JPH::Vec3 along = toJolt(direction).NormalizedOr(JPH::Vec3::sAxisZ());
    const JPH::RShapeCast cast = JPH::RShapeCast::sFromWorldTransform(&capsule, JPH::Vec3::sReplicate(1.0f),
        JPH::RMat44::sTranslation(toJolt(origin)), along * maxDistance);
    JPH::ShapeCastSettings settings;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;

    settings.mReturnDeepestPoint = true;
    settings.mBackFaceModeTriangles = JPH::EBackFaceMode::IgnoreBackFaces;
    settings.mBackFaceModeConvex = JPH::EBackFaceMode::IgnoreBackFaces;
    m_impl->system.GetNarrowPhaseQuery().CastShape(cast, settings, toJolt(origin), collector, {}, {},
        Impl::Filter(*m_impl, mask, triggers, &ignore));
    if (!collector.HadHit()) {
        return std::nullopt;
    }
    RaycastHit3D result;

    result.entity = m_impl->entityOf.at(collector.mHit.mBodyID2.GetIndexAndSequenceNumber());
    result.distance = maxDistance * collector.mHit.mFraction;
    result.point = fromJolt(collector.mHit.mContactPointOn2);
    result.normal = fromJolt((-collector.mHit.mPenetrationAxis).NormalizedOr(JPH::Vec3::sAxisY()));
    return result;
}

std::optional<kuge::HitboxHit3D> kuge::Physics3D::raycastHitboxes(kw::World& world, k3::Math::Vector3f origin, k3::Math::Vector3f direction,
    float maxDistance, std::uint32_t mask, const EntityFilter& ignore) const
{
    const float length = std::sqrt(dot(direction, direction));

    if (!(length > 0.0f) || !(maxDistance > 0.0f)) {
        return std::nullopt;
    }
    const Vec3 along = direction * (1.0f / length);
    // Walls and bodies, except what is hit through its hitboxes
    const EntityFilter skip = [&](kw::Entity entity) {
        return world.has<kuge::Hitboxes>(entity) || (ignore && ignore(entity));
    };
    std::optional<HitboxHit3D> best;

    if (const auto wall = raycast(origin, along, maxDistance, mask, false, skip)) {
        best = HitboxHit3D{*wall};
    }
    std::vector<kw::Entity> entities;

    for (kw::Entity entity : world.view<kuge::Hitboxes, kuge::Transform3D>()) {
        entities.push_back(entity);
    }
    std::sort(entities.begin(), entities.end());
    for (kw::Entity entity : entities) {
        if (ignore && ignore(entity)) {
            continue;
        }
        const kuge::Hitboxes& boxes = world.get<kuge::Hitboxes>(entity);
        const kuge::Transform3D& transform = world.get<kuge::Transform3D>(entity);

        for (std::size_t i = 0; i < boxes.parts.size(); ++i) {
            const kuge::Hitbox& part = boxes.parts[i];
            const auto hit = rayPart(place(transform, boxes, part), part, origin, along);

            if (hit && hit->distance < maxDistance && (!best || hit->distance < best->distance)) {
                HitboxHit3D shot;

                shot.entity = entity;
                shot.distance = hit->distance;
                shot.point = origin + along * hit->distance;
                shot.normal = hit->normal;
                shot.hitbox = static_cast<int>(i);
                shot.weight = part.weight;
                best = shot;
            }
        }
    }
    return best;
}

std::vector<kw::Entity> kuge::Physics3D::overlapSphere(k3::Math::Vector3f center, float radius, std::uint32_t mask,
    bool triggers) const
{
    const JPH::SphereShape sphere(std::max(radius, 0.001f));
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    std::vector<kw::Entity> found;

    m_impl->system.GetNarrowPhaseQuery().CollideShape(&sphere, JPH::Vec3::sReplicate(1.0f), JPH::RMat44::sTranslation(toJolt(center)),
        JPH::CollideShapeSettings(), toJolt(center), collector, {}, {}, Impl::Filter(*m_impl, mask, triggers));
    for (const JPH::CollideShapeResult& hit : collector.mHits) {
        found.push_back(m_impl->entityOf.at(hit.mBodyID2.GetIndexAndSequenceNumber()));
    }
    std::sort(found.begin(), found.end());
    found.erase(std::unique(found.begin(), found.end()), found.end());
    return found;
}

bool kuge::Physics3DStep::handle(kw::World& world)
{
    world.getResource<Physics3D>().step(world, static_cast<float>(world.getResource<Time>().dt));
    return true;
}

void kuge::installPhysics3D(SceneSetup scene, Physics3DConfig config)
{
    scene.world().addResource<Physics3D>(config);
    scene.addSystem(kw::Schedule::Fixed, stage::Physics, std::make_unique<Physics3DStep>());
}
