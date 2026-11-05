#pragma once

#include "sim/CharacterLook.h"

#include "physics/PhysicsWorld.h"
#include "sim/Weapons.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace origon {

constexpr float kTickRate = 60.0f;
constexpr float kTickDt = 1.0f / kTickRate;
constexpr float kEyeHeight = 1.62f;
constexpr float kCrouchEyeHeight = 1.05f;
constexpr float kProneEyeHeight = 0.36f;
constexpr float kProneEyeForward = 0.55f; // Lying down the head is ahead of the body's center.
constexpr float kLeanDistance = 0.36f;    // Sideways eye offset at full lean.
constexpr float kMaxHealth = 100.0f;

enum Button : uint32_t {
    kButtonFire = 1u << 0,
    kButtonAds = 1u << 1,
    kButtonJump = 1u << 2,
    kButtonSprint = 1u << 3,
    kButtonReload = 1u << 4,
    kButtonMelee = 1u << 5,
    kButtonCrouch = 1u << 6,
    kButtonLeanLeft = 1u << 7,
    kButtonLeanRight = 1u << 8,
    kButtonProne = 1u << 9, // Toggles lying down.
};

// Everything a player controls in one tick. This is what clients will send to the host.
struct PlayerCommand {
    float forward = 0.0f; // -1..1
    float right = 0.0f;   // -1..1
    float yaw = 0.0f;     // Radians. 0 looks toward -Z.
    float pitch = 0.0f;   // Radians. Positive looks up.
    uint32_t buttons = 0;
};

enum class KillCause { Weapon, Melee, Suicide, World };

struct BotState {
    int targetId = -1;
    float reactionTimer = 0.0f;
    float strafeTimer = 0.0f;
    float strafeDirection = 1.0f;
    glm::vec3 waypoint{0.0f};
    bool hasWaypoint = false;
    float stuckTimer = 0.0f;
    glm::vec3 lastCheckPosition{0.0f};
    float aimErrorYaw = 0.0f;
    float aimErrorPitch = 0.0f;
    bool fireToggle = false;
};

struct Player {
    int id = 0;
    std::string name;
    bool isBot = false;
    glm::vec3 color{0.8f};
    CharacterLook look; // Cosmetic only.

    // Physical state.
    std::unique_ptr<Character> character;
    glm::vec3 previousPosition{0.0f};
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool onGround = false;
    bool crouched = false;
    bool prone = false;
    float eyeHeight = kEyeHeight; // Smoothed between standing, crouched and prone.
    float lean = 0.0f;            // -1 (left) .. 1 (right), smoothed.
    float proneBlend = 0.0f;      // 0..1, smoothed: how far the body has gone down to the ground.
    glm::vec3 eyeOffset{0.0f};    // Horizontal eye offset from leaning and lying down (walls clamp it).
    float airTime = 0.0f;
    float fallSpeed = 0.0f;
    // Set when the simulation forces a view direction (spawn). The local view reads and clears it.
    bool viewReset = false;

    // Life state.
    bool alive = false;
    float health = kMaxHealth;
    float respawnTimer = 0.0f;
    float spawnProtection = 0.0f;
    float timeSinceDamage = 0.0f;

    // Weapon state.
    int tier = 0; // 0-based ladder index.
    int ammo = 0;
    float fireCooldown = 0.0f;
    float reloadTimer = 0.0f;
    float meleeCooldown = 0.0f;
    float meleeAnimation = 0.0f;
    float viewKick = 0.0f; // Recoil pitch offset in radians.
    bool aiming = false;
    uint32_t previousButtons = 0;

    // Score.
    int kills = 0;
    int deaths = 0;
    float tierReachedTime = 0.0f;

    BotState bot;

    glm::vec3 eyePosition() const { return position + glm::vec3(0.0f, eyeHeight, 0.0f) + eyeOffset; }
};

struct GameEvent {
    enum class Type {
        Kill,       // a = killer (-1 for world), b = victim, cause
        Promoted,   // a = player, tier
        Demoted,    // a = player, tier, cause (Melee = humiliated, Suicide/World = suicide)
        FinalTier,  // a = player
        Victory,    // a = winner
        TimeUp,
        Hit,        // a = attacker, b = victim, headshot
        Shot,       // a = shooter, start/end (tracer), surface: 0 nothing, 1 world, 2 player, 3 prop
        Fired,      // a = shooter, start = muzzle, category (one per trigger pull, for audio)
        DryFire,    // a = player
        Reload,     // a = player, category
        MeleeSwing, // a = player
        Jumped,     // a = player
        Landed,     // a = player, magnitude = fall speed
        Spawned,    // a = player
    };
    Type type;
    int a = -1;
    int b = -1;
    int tier = 0;
    KillCause cause = KillCause::Weapon;
    bool headshot = false;
    std::string weaponName;
    glm::vec3 start{0.0f};
    glm::vec3 end{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    int surface = 0;
    float magnitude = 0.0f;
    WeaponCategory category = WeaponCategory::Smg;
};

} // namespace origon
