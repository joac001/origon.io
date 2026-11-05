#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace JPH {
class CharacterVirtual;
class Shape;
} // namespace JPH

namespace origon {

using BodyHandle = uint32_t;
using ShapeHandle = int;
constexpr BodyHandle kInvalidBody = 0xFFFFFFFFu;

// Collision layers.
//  Static:  map geometry.
//  Dynamic: physics props that affect gameplay (characters collide with them, bullets push them).
//  Debris:  cosmetic bodies (corpses, shells). Never collide with characters or bullets.
enum class BodyLayer { Static, Dynamic, Debris };

struct RayHit {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float distance = 0.0f;
    BodyHandle body = kInvalidBody;
    bool dynamic = false;
};

struct BodyTransform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct CharacterDesc {
    float radius = 0.4f;
    float height = 1.8f;       // Standing height, feet to top.
    float crouchHeight = 1.2f; // Crouched height.
    float proneHeight = 0.6f;  // Lying down (a low capsule around the body's center).
    float proneRadius = 0.3f;
    float maxSlopeDegrees = 50.0f;
    float stepHeight = 0.4f;
};

class PhysicsWorld;

// Kinematic FPS character (Jolt CharacterVirtual). Position is at the feet.
class Character {
public:
    ~Character();
    Character(const Character&) = delete;
    Character& operator=(const Character&) = delete;

    void update(float dt, const glm::vec3& velocity, const glm::vec3& gravity);
    void teleport(const glm::vec3& feetPosition);
    enum class Stance { Standing, Crouching, Prone };
    // Returns false when there is no room to change stance (for example standing up under a ledge).
    bool setStance(Stance stance);
    bool setCrouched(bool crouched) { return setStance(crouched ? Stance::Crouching : Stance::Standing); }
    Stance stance() const { return m_stance; }
    bool crouched() const { return m_stance == Stance::Crouching; }

    glm::vec3 position() const;
    glm::vec3 velocity() const;
    bool onGround() const;

private:
    friend class PhysicsWorld;
    Character(PhysicsWorld& world, const CharacterDesc& desc, const glm::vec3& feetPosition);

    PhysicsWorld& m_world;
    CharacterDesc m_desc;
    JPH::CharacterVirtual* m_character = nullptr;
    const JPH::Shape* m_standingShape = nullptr;
    const JPH::Shape* m_crouchingShape = nullptr;
    const JPH::Shape* m_proneShape = nullptr;
    Stance m_stance = Stance::Standing;
};

// Rigid body world. Static collision always comes from map data and never depends on graphics
// settings (competitive parity).
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // Removes every body (static, dynamic and debris).
    void clearBodies();
    BodyHandle addStaticBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation,
                            bool cylinder = false);
    BodyHandle addDynamicBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation,
                             float mass, BodyLayer layer = BodyLayer::Dynamic,
                             const glm::vec3& linearVelocity = glm::vec3(0.0f),
                             const glm::vec3& angularVelocity = glm::vec3(0.0f), bool cylinder = false);
    // cylinder: the box becomes a cylinder along its local Z (radius = smaller of X/Y half extents).
    // Static triangle mesh shapes, created once (per model) and instanced many times.
    ShapeHandle createMeshShape(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices);
    BodyHandle addStaticShape(ShapeHandle shape, const glm::vec3& position, const glm::quat& rotation, float scale);
    // Square heightfield: samples[z * sampleCount + x], first sample at origin, spacing cellSize.
    BodyHandle addHeightField(const glm::vec3& origin, float cellSize, int sampleCount, const float* samples);
    void removeBody(BodyHandle body);
    void optimize();

    // Advances dynamic bodies.
    void update(float dt);

    BodyTransform transform(BodyHandle body) const;
    // Moves a body without simulating (network replicas).
    void setTransform(BodyHandle body, const glm::vec3& position, const glm::quat& rotation);
    void applyImpulse(BodyHandle body, const glm::vec3& impulse, const glm::vec3& point);

    std::unique_ptr<Character> createCharacter(const CharacterDesc& desc, const glm::vec3& feetPosition);

    // Ray cast against static geometry, and dynamic props when includeDynamic is true.
    std::optional<RayHit> rayCast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                                  bool includeDynamic = false) const;

private:
    friend class Character;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace origon
