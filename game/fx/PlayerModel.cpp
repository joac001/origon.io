#include "fx/PlayerModel.h"

#include "render/Model.h"
#include "sim/Weapons.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace origon {
namespace {

const glm::vec3 kSkin(0.78f, 0.58f, 0.44f);
const glm::vec3 kPants(0.2f, 0.22f, 0.16f);
const glm::vec3 kHelmet(0.2f, 0.25f, 0.17f);
const glm::vec3 kBoots(0.1f, 0.08f, 0.06f);
const glm::vec3 kGunMetal(0.1f, 0.1f, 0.11f);

void box(Renderer& renderer, const glm::mat4& parent, const glm::vec3& center, const glm::vec3& size,
         const glm::vec3& color, float roughness, bool highlight, float metallic = 0.0f) {
    DrawItem item;
    item.mesh = &renderer.cube();
    item.model = parent * glm::translate(glm::mat4(1.0f), center) * glm::scale(glm::mat4(1.0f), size);
    item.material.albedo = color;
    item.material.roughness = roughness;
    item.material.metallic = metallic;
    if (highlight) {
        item.material.emissive = color * 0.35f;
    }
    renderer.submit(item);
}

// A limb hanging from a pivot, rotated around X.
void limb(Renderer& renderer, const glm::mat4& parent, const glm::vec3& pivot, float angle, const glm::vec3& size,
          const glm::vec3& color, bool highlight) {
    const glm::mat4 joint = parent * glm::translate(glm::mat4(1.0f), pivot) * glm::rotate(glm::mat4(1.0f), angle, glm::vec3(1, 0, 0));
    box(renderer, joint, glm::vec3(0.0f, -size.y * 0.5f, 0.0f), size, color, 0.85f, highlight);
}

} // namespace

glm::vec3 weaponBodySize(const WeaponDef* weapon) {
    if (!weapon) {
        return {0.06f, 0.09f, 0.5f};
    }
    switch (weapon->category) {
    case WeaponCategory::Pistol: return {0.045f, 0.11f, 0.22f};
    case WeaponCategory::Smg: return {0.06f, 0.09f, 0.5f};
    case WeaponCategory::AssaultRifle: return {0.065f, 0.1f, 0.65f};
    case WeaponCategory::Lmg: return {0.08f, 0.11f, 0.8f};
    case WeaponCategory::SemiAutoRifle: return {0.055f, 0.08f, 0.95f};
    case WeaponCategory::BoltRifle: return {0.055f, 0.08f, 1.0f};
    case WeaponCategory::Sniper: return {0.055f, 0.08f, 1.0f};
    case WeaponCategory::Shotgun: return {0.065f, 0.09f, 0.85f};
    case WeaponCategory::Melee: return {0.012f, 0.035f, 0.26f};
    }
    return {0.06f, 0.09f, 0.5f};
}

glm::vec3 submitPlayerModel(Renderer& renderer, const PlayerPose& pose) {
    const bool hl = pose.emissiveHighlight;
    const glm::mat4 root = glm::translate(glm::mat4(1.0f), pose.feet) * glm::mat4_cast(pose.rotation);

    // Crouching lowers the hips and bends the legs.
    const float crouch = std::clamp((1.62f - pose.eyeHeight) / (1.62f - 1.05f), 0.0f, 1.0f);
    const float hip = 0.88f - crouch * 0.42f;
    const float swing = std::sin(pose.walkPhase) * 0.65f * pose.walkAmount;
    const float legLength = 0.84f - crouch * 0.05f;

    // Legs (thigh angle forward when crouched).
    for (int side = -1; side <= 1; side += 2) {
        const float s = static_cast<float>(side);
        const float angle = swing * s + crouch * 0.9f;
        const glm::mat4 joint = root * glm::translate(glm::mat4(1.0f), glm::vec3(0.11f * s, hip, 0.0f)) *
                                glm::rotate(glm::mat4(1.0f), angle, glm::vec3(1, 0, 0));
        box(renderer, joint, glm::vec3(0.0f, -legLength * 0.5f, 0.0f), glm::vec3(0.16f, legLength, 0.18f), kPants, 0.9f, hl);
        box(renderer, joint, glm::vec3(0.0f, -legLength + 0.05f, -0.05f), glm::vec3(0.17f, 0.1f, 0.28f), kBoots, 0.7f, hl);
    }

    // Upper body leans slightly with aim pitch.
    const glm::mat4 torso = root * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, hip, 0.0f)) *
                            glm::rotate(glm::mat4(1.0f), pose.pitch * 0.25f + crouch * 0.25f, glm::vec3(1, 0, 0));
    box(renderer, torso, glm::vec3(0.0f, 0.06f, 0.0f), glm::vec3(0.38f, 0.18f, 0.22f), kPants, 0.9f, hl);
    box(renderer, torso, glm::vec3(0.0f, 0.42f, 0.0f), glm::vec3(0.46f, 0.56f, 0.26f), pose.color, 0.8f, hl);
    // Webbing / pouches.
    box(renderer, torso, glm::vec3(0.0f, 0.3f, -0.14f), glm::vec3(0.36f, 0.12f, 0.06f), kPants * 0.8f, 0.9f, hl);

    // Head follows aim pitch.
    const glm::mat4 neck = torso * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.74f, 0.0f)) *
                           glm::rotate(glm::mat4(1.0f), pose.pitch * 0.6f, glm::vec3(1, 0, 0));
    box(renderer, neck, glm::vec3(0.0f, 0.13f, 0.0f), glm::vec3(0.22f, 0.25f, 0.24f), kSkin, 0.7f, hl);
    box(renderer, neck, glm::vec3(0.0f, 0.27f, 0.0f), glm::vec3(0.3f, 0.1f, 0.32f), kHelmet, 0.6f, hl);

    // Arms and weapon aim with full pitch.
    const glm::mat4 shoulders = torso * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.62f, 0.0f)) *
                                glm::rotate(glm::mat4(1.0f), pose.pitch * 0.75f, glm::vec3(1, 0, 0));
    limb(renderer, shoulders, glm::vec3(0.27f, 0.0f, 0.0f), 1.25f, glm::vec3(0.12f, 0.5f, 0.12f), pose.color * 0.9f, hl);
    limb(renderer, shoulders, glm::vec3(-0.27f, 0.0f, 0.0f), 1.4f, glm::vec3(0.12f, 0.52f, 0.12f), pose.color * 0.9f, hl);
    const glm::vec3 gun = weaponBodySize(pose.weapon);
    const glm::vec3 gunCenter(0.08f, -0.12f, -0.35f - gun.z * 0.5f);
    if (pose.weaponModel) {
        const glm::mat4 gunMatrix = shoulders * glm::translate(glm::mat4(1.0f), glm::vec3(0.08f, -0.14f, -0.42f));
        for (const ModelPart& part : pose.weaponModel->lod(0)) {
            DrawItem item;
            item.mesh = &part.mesh;
            item.material = part.material;
            item.model = gunMatrix;
            renderer.submit(item);
        }
    } else {
        box(renderer, shoulders, gunCenter, gun, kGunMetal, 0.45f, hl, 0.8f);
    }
    box(renderer, shoulders, glm::vec3(0.18f, -0.14f, -0.42f), glm::vec3(0.1f, 0.1f, 0.1f), kSkin, 0.7f, hl);

    return glm::vec3(shoulders * glm::vec4(gunCenter + glm::vec3(0.0f, 0.0f, -gun.z * 0.5f - 0.04f), 1.0f));
}

} // namespace origon
