#pragma once

#include "world/Terrain.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace origon {

struct BlockData {
    glm::vec3 position{0.0f}; // Center.
    glm::vec3 size{1.0f};     // Full extents.
    glm::vec3 rotation{0.0f}; // Euler degrees: pitch (X), yaw (Y), roll (Z).
    glm::vec3 color{0.6f};
    float roughness = 0.85f;
    float metallic = 0.0f;
    // Dynamic blocks are physics props: they fall, can be pushed and react to bullets.
    bool dynamic = false;
    float mass = 40.0f;

    glm::quat orientation() const;
};

struct SpawnData {
    glm::vec3 position{0.0f}; // Feet position.
    float yawDegrees = 0.0f;
};

struct LightData {
    glm::vec3 position{0.0f};
    glm::vec3 color{1.0f};
    float intensity = 5.0f;
    float radius = 10.0f;
};

struct EnvironmentData {
    glm::vec3 sunDirection{0.45f, 0.75f, 0.35f};
    glm::vec3 sunColor{1.0f, 0.93f, 0.82f};
    float sunIntensity = 3.2f;
    glm::vec3 skyColor{0.45f, 0.6f, 0.82f};
    glm::vec3 groundColor{0.26f, 0.24f, 0.2f};
    float ambientIntensity = 0.55f;
    glm::vec3 fogColor{0.62f, 0.7f, 0.78f};
    float fogDensity = 0.006f;
};

struct MapData {
    static constexpr int kVersion = 1;

    std::string name = "Untitled";
    EnvironmentData environment;
    TerrainData terrain;
    std::vector<BlockData> blocks;
    std::vector<SpawnData> spawns;
    std::vector<LightData> lights;

    static std::optional<MapData> load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;
    static MapData makeDefault();
};

} // namespace origon
