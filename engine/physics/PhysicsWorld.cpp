#include "physics/PhysicsWorld.h"

#include "core/Log.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cfloat>
#include <cstdarg>
#include <cstdio>
#include <thread>
#include <vector>

namespace origon {
namespace {

namespace Layers {
constexpr JPH::ObjectLayer kStatic = 0;
constexpr JPH::ObjectLayer kDynamic = 1;
constexpr JPH::ObjectLayer kDebris = 2;
constexpr JPH::ObjectLayer kCharacter = 3; // Query-only layer used by character controllers.
} // namespace Layers

namespace BroadPhaseLayers {
constexpr JPH::BroadPhaseLayer kStatic(0);
constexpr JPH::BroadPhaseLayer kMoving(1);
constexpr unsigned kCount = 2;
} // namespace BroadPhaseLayers

JPH::ObjectLayer toObjectLayer(BodyLayer layer) {
    switch (layer) {
    case BodyLayer::Static: return Layers::kStatic;
    case BodyLayer::Dynamic: return Layers::kDynamic;
    case BodyLayer::Debris: return Layers::kDebris;
    }
    return Layers::kDynamic;
}

class BroadPhaseLayerMap final : public JPH::BroadPhaseLayerInterface {
public:
    unsigned GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::kCount; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer == Layers::kStatic ? BroadPhaseLayers::kStatic : BroadPhaseLayers::kMoving;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == BroadPhaseLayers::kStatic ? "static" : "moving";
    }
#endif
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override {
        if (layer == Layers::kStatic) {
            return broadPhase == BroadPhaseLayers::kMoving;
        }
        return true;
    }
};

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        if (a == Layers::kStatic && b == Layers::kStatic) return false;
        const bool characterVsDebris = (a == Layers::kCharacter && b == Layers::kDebris) ||
                                       (a == Layers::kDebris && b == Layers::kCharacter);
        return !characterVsDebris;
    }
};

// Accepts a fixed set of object layers.
class LayerMaskFilter final : public JPH::ObjectLayerFilter {
public:
    explicit LayerMaskFilter(uint32_t mask) : m_mask(mask) {}
    bool ShouldCollide(JPH::ObjectLayer layer) const override { return (m_mask >> layer) & 1u; }

private:
    uint32_t m_mask;
};

constexpr uint32_t bit(JPH::ObjectLayer layer) { return 1u << layer; }

void joltTrace(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buffer[1024];
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log::info("Jolt: {}", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
bool joltAssert(const char* expression, const char* message, const char* file, JPH::uint line) {
    log::error("Jolt assert {}:{}: ({}) {}", file, line, expression, message ? message : "");
    return true;
}
#endif

int g_joltUsers = 0;

void acquireJolt() {
    if (g_joltUsers++ == 0) {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = joltTrace;
        JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = joltAssert;)
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }
}

void releaseJolt() {
    if (--g_joltUsers == 0) {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
}

JPH::Vec3 toJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
glm::vec3 toGlm(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
JPH::Quat toJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
glm::quat toGlm(JPH::QuatArg q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

JPH::Ref<JPH::Shape> makeBox(const glm::vec3& halfExtents) {
    const glm::vec3 half = glm::max(halfExtents, glm::vec3(0.01f));
    const float convexRadius = std::min(0.05f, std::min({half.x, half.y, half.z}) * 0.5f);
    return new JPH::BoxShape(toJolt(half), convexRadius);
}

} // namespace

struct PhysicsWorld::Impl {
    BroadPhaseLayerMap broadPhaseLayers;
    ObjectVsBroadPhaseFilter objectVsBroadPhase;
    ObjectPairFilter objectPairs;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
    std::unique_ptr<JPH::PhysicsSystem> system;
    JPH::CharacterVsCharacterCollisionSimple characterCollision;
    std::vector<JPH::BodyID> bodies;
};

PhysicsWorld::PhysicsWorld() : m_impl(std::make_unique<Impl>()) {
    acquireJolt();
    m_impl->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(32 * 1024 * 1024);
    const int threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 1, 1, 4);
    m_impl->jobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);
    m_impl->system = std::make_unique<JPH::PhysicsSystem>();
    m_impl->system->Init(16384, 0, 16384, 8192, m_impl->broadPhaseLayers, m_impl->objectVsBroadPhase,
                         m_impl->objectPairs);
    m_impl->system->SetGravity(JPH::Vec3(0.0f, -16.0f, 0.0f));
}

PhysicsWorld::~PhysicsWorld() {
    clearBodies();
    m_impl->system.reset();
    m_impl->jobSystem.reset();
    m_impl->tempAllocator.reset();
    releaseJolt();
}

void PhysicsWorld::clearBodies() {
    if (m_impl->bodies.empty()) {
        return;
    }
    JPH::BodyInterface& bodies = m_impl->system->GetBodyInterface();
    bodies.RemoveBodies(m_impl->bodies.data(), static_cast<int>(m_impl->bodies.size()));
    bodies.DestroyBodies(m_impl->bodies.data(), static_cast<int>(m_impl->bodies.size()));
    m_impl->bodies.clear();
}

BodyHandle PhysicsWorld::addStaticBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation) {
    JPH::BodyCreationSettings settings(makeBox(halfExtents), JPH::RVec3(toJolt(center)), toJolt(glm::normalize(rotation)),
                                       JPH::EMotionType::Static, Layers::kStatic);
    const JPH::BodyID id = m_impl->system->GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
    if (id.IsInvalid()) {
        log::error("Physics: body limit reached");
        return kInvalidBody;
    }
    m_impl->bodies.push_back(id);
    return id.GetIndexAndSequenceNumber();
}

BodyHandle PhysicsWorld::addDynamicBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation,
                                       float mass, BodyLayer layer, const glm::vec3& linearVelocity,
                                       const glm::vec3& angularVelocity) {
    JPH::BodyCreationSettings settings(makeBox(halfExtents), JPH::RVec3(toJolt(center)), toJolt(glm::normalize(rotation)),
                                       JPH::EMotionType::Dynamic, toObjectLayer(layer));
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = std::max(mass, 0.1f);
    settings.mLinearVelocity = toJolt(linearVelocity);
    settings.mAngularVelocity = toJolt(angularVelocity);
    settings.mFriction = 0.7f;
    settings.mRestitution = 0.05f;
    settings.mLinearDamping = 0.1f;
    settings.mAngularDamping = 0.3f;
    const JPH::BodyID id = m_impl->system->GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::Activate);
    if (id.IsInvalid()) {
        log::error("Physics: body limit reached");
        return kInvalidBody;
    }
    m_impl->bodies.push_back(id);
    return id.GetIndexAndSequenceNumber();
}

void PhysicsWorld::removeBody(BodyHandle body) {
    const JPH::BodyID id(body);
    auto it = std::find(m_impl->bodies.begin(), m_impl->bodies.end(), id);
    if (it == m_impl->bodies.end()) {
        return;
    }
    JPH::BodyInterface& bodies = m_impl->system->GetBodyInterface();
    bodies.RemoveBody(id);
    bodies.DestroyBody(id);
    m_impl->bodies.erase(it);
}

void PhysicsWorld::optimize() { m_impl->system->OptimizeBroadPhase(); }

void PhysicsWorld::update(float dt) {
    m_impl->system->Update(dt, 1, m_impl->tempAllocator.get(), m_impl->jobSystem.get());
}

BodyTransform PhysicsWorld::transform(BodyHandle body) const {
    BodyTransform result;
    JPH::RVec3 position;
    JPH::Quat rotation;
    m_impl->system->GetBodyInterface().GetPositionAndRotation(JPH::BodyID(body), position, rotation);
    result.position = toGlm(JPH::Vec3(position));
    result.rotation = toGlm(rotation);
    return result;
}

void PhysicsWorld::applyImpulse(BodyHandle body, const glm::vec3& impulse, const glm::vec3& point) {
    JPH::BodyInterface& bodies = m_impl->system->GetBodyInterface();
    const JPH::BodyID id(body);
    if (bodies.IsAdded(id) && bodies.GetMotionType(id) == JPH::EMotionType::Dynamic) {
        bodies.AddImpulse(id, toJolt(impulse), JPH::RVec3(toJolt(point)));
    }
}

std::unique_ptr<Character> PhysicsWorld::createCharacter(const CharacterDesc& desc, const glm::vec3& feetPosition) {
    return std::unique_ptr<Character>(new Character(*this, desc, feetPosition));
}

std::optional<RayHit> PhysicsWorld::rayCast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                                            bool includeDynamic) const {
    const glm::vec3 dir = glm::normalize(direction);
    const JPH::RRayCast ray{JPH::RVec3(toJolt(origin)), toJolt(dir * maxDistance)};
    JPH::RayCastResult result;
    const LayerMaskFilter objectFilter(includeDynamic ? bit(Layers::kStatic) | bit(Layers::kDynamic) : bit(Layers::kStatic));
    if (!m_impl->system->GetNarrowPhaseQuery().CastRay(ray, result, {}, objectFilter)) {
        return std::nullopt;
    }
    RayHit hit;
    hit.distance = result.mFraction * maxDistance;
    hit.position = origin + dir * hit.distance;
    hit.body = result.mBodyID.GetIndexAndSequenceNumber();
    JPH::BodyLockRead lock(m_impl->system->GetBodyLockInterface(), result.mBodyID);
    if (lock.Succeeded()) {
        const JPH::Body& body = lock.GetBody();
        hit.normal = toGlm(body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, JPH::RVec3(toJolt(hit.position))));
        hit.dynamic = body.GetObjectLayer() == Layers::kDynamic;
    }
    return hit;
}

// --- Character ------------------------------------------------------------------------

namespace {

const JPH::Shape* makeCharacterShape(float height, float radius) {
    const float cylinderHalf = std::max(0.05f, height * 0.5f - radius);
    JPH::RefConst<JPH::Shape> shape = JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0.0f, height * 0.5f, 0.0f),
                                                                          JPH::Quat::sIdentity(),
                                                                          new JPH::CapsuleShape(cylinderHalf, radius))
                                          .Create()
                                          .Get();
    shape->AddRef();
    return shape.GetPtr();
}

} // namespace

Character::Character(PhysicsWorld& world, const CharacterDesc& desc, const glm::vec3& feetPosition)
    : m_world(world), m_desc(desc) {
    m_standingShape = makeCharacterShape(desc.height, desc.radius);
    m_crouchingShape = makeCharacterShape(desc.crouchHeight, desc.radius);

    JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
    settings->mShape = m_standingShape;
    settings->mMaxSlopeAngle = JPH::DegreesToRadians(desc.maxSlopeDegrees);
    settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -desc.radius);
    settings->mCharacterPadding = 0.02f;
    settings->mPenetrationRecoverySpeed = 1.0f;
    settings->mMass = 80.0f;
    settings->mMaxStrength = 250.0f; // Push force on dynamic props (N).

    m_character = new JPH::CharacterVirtual(settings, JPH::RVec3(toJolt(feetPosition)), JPH::Quat::sIdentity(), 0,
                                            world.m_impl->system.get());
    m_character->AddRef();
    world.m_impl->characterCollision.Add(m_character);
    m_character->SetCharacterVsCharacterCollision(&world.m_impl->characterCollision);
}

Character::~Character() {
    m_world.m_impl->characterCollision.Remove(m_character);
    m_character->Release();
    m_standingShape->Release();
    m_crouchingShape->Release();
}

void Character::update(float dt, const glm::vec3& velocity, const glm::vec3& gravity) {
    m_character->SetLinearVelocity(toJolt(velocity));
    JPH::CharacterVirtual::ExtendedUpdateSettings settings;
    settings.mWalkStairsStepUp = JPH::Vec3(0.0f, m_desc.stepHeight, 0.0f);
    settings.mStickToFloorStepDown = JPH::Vec3(0.0f, -0.5f, 0.0f);
    const LayerMaskFilter objectFilter(bit(Layers::kStatic) | bit(Layers::kDynamic));
    m_character->ExtendedUpdate(dt, toJolt(gravity), settings, {}, objectFilter, {}, {}, *m_world.m_impl->tempAllocator);
}

bool Character::setCrouched(bool crouched) {
    if (crouched == m_crouched) {
        return true;
    }
    const LayerMaskFilter objectFilter(bit(Layers::kStatic) | bit(Layers::kDynamic));
    const JPH::Shape* shape = crouched ? m_crouchingShape : m_standingShape;
    // Standing up must not penetrate geometry; crouching always succeeds.
    const float maxPenetration = crouched ? FLT_MAX : 1.5f * m_character->GetCharacterPadding();
    if (!m_character->SetShape(shape, maxPenetration, {}, objectFilter, {}, {}, *m_world.m_impl->tempAllocator)) {
        return false;
    }
    m_crouched = crouched;
    return true;
}

void Character::teleport(const glm::vec3& feetPosition) {
    m_character->SetPosition(JPH::RVec3(toJolt(feetPosition)));
    m_character->SetLinearVelocity(JPH::Vec3::sZero());
}

glm::vec3 Character::position() const { return toGlm(JPH::Vec3(m_character->GetPosition())); }

glm::vec3 Character::velocity() const { return toGlm(m_character->GetLinearVelocity()); }

bool Character::onGround() const {
    return m_character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
}

} // namespace origon
