#pragma once

#include "render/Renderer.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace origon {

struct WeaponDef;

// Pose input for the blocky humanoid used for other players and corpses.
struct PlayerPose {
    glm::vec3 feet{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // Body orientation (yaw for live players).
    float pitch = 0.0f;                          // Aim pitch (arms, head, weapon).
    float walkPhase = 0.0f;
    float walkAmount = 0.0f; // 0 idle, 1 full stride.
    float eyeHeight = 1.62f;
    glm::vec3 color{0.8f};
    const WeaponDef* weapon = nullptr;
    bool emissiveHighlight = false;
};

// Draws a humanoid made of boxes. Returns the muzzle position in world space.
glm::vec3 submitPlayerModel(Renderer& renderer, const PlayerPose& pose);

// Size of the first-person/third-person weapon body for a category.
glm::vec3 weaponBodySize(const WeaponDef* weapon);

} // namespace origon
