#include "Physics3D.hpp"
#include "Time.hpp"
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#include <algorithm>
#include <cmath>
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

    void touch(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold, bool added)
    {
        const Record* first = recordOf(a);
        const Record* second = recordOf(b);

        if (!first || !second || !compatible(first->collider, second->collider)) {
            return;
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

    void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold, JPH::ContactSettings&) override
    {
        touch(a, b, manifold, true);
    }

    void OnContactPersisted(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold, JPH::ContactSettings&) override
    {
        touch(a, b, manifold, false);
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
                && record.body.gravityScale == body->gravityScale && record.body.lockRotation == body->lockRotation));
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

    // -- Questions -------------------------------------------------------------------------------

    class Filter final : public JPH::BodyFilter
    {
        public:
            Filter(const Impl& impl, std::uint32_t mask, bool triggers) : m_impl(impl), m_mask(mask), m_triggers(triggers) {}

            bool ShouldCollide(const JPH::BodyID& id) const override
            {
                const auto entity = m_impl.entityOf.find(id.GetIndexAndSequenceNumber());

                if (entity == m_impl.entityOf.end()) {
                    return false;
                }
                const Collider3D& collider = m_impl.records.at(entity->second).collider;

                return (m_triggers || !collider.trigger) && (collider.layer & m_mask) != 0;
            }

        private:
            const Impl&   m_impl;
            std::uint32_t m_mask;
            bool          m_triggers;
    };
};

kuge::Physics3D::Physics3D(Physics3DConfig config) : m_impl(std::make_unique<Impl>(config)) {}

kuge::Physics3D::~Physics3D(void) = default;

void kuge::Physics3D::step(kw::World& world, float dt)
{
    m_impl->step(world, dt, m_events);
}

std::size_t kuge::Physics3D::bodyCount(void) const noexcept
{
    return m_impl->system.GetNumBodies();
}

std::optional<kuge::RaycastHit3D> kuge::Physics3D::raycast(k3::Math::Vector3f origin, k3::Math::Vector3f direction,
    float maxDistance, std::uint32_t mask, bool triggers) const
{
    const JPH::Vec3 along = toJolt(direction).NormalizedOr(JPH::Vec3::sAxisZ());
    const JPH::RRayCast ray(toJolt(origin), along * maxDistance);
    JPH::RayCastResult hit;

    if (!m_impl->system.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, Impl::Filter(*m_impl, mask, triggers))) {
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
