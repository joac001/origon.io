#include "world/World.h"

#include "physics/PhysicsWorld.h"
#include "render/Texture.h"

#include <glm/exponential.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace origon {
// Map files store colors in sRGB (what the editor color pickers show). Lighting needs linear.
glm::vec3 World::toLinear(const glm::vec3& srgb) { return glm::pow(glm::max(srgb, glm::vec3(0.0f)), glm::vec3(2.2f)); }

glm::mat4 World::blockTransform(const BlockData& block) {
    return glm::translate(glm::mat4(1.0f), block.position) * glm::mat4_cast(block.orientation()) *
           glm::scale(glm::mat4(1.0f), block.size);
}

void World::build(const MapData& map, PhysicsWorld& physics) {
    m_map = map;
    physics.clearBodies();

    TerrainData& terrain = m_map.terrain;
    if (terrain.enabled) {
        terrain.ensureStorage();
        if (terrain.valid()) {
            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;
            terrain.buildVertices(vertices, indices);
            m_terrainMesh = Mesh(vertices, indices);
            const glm::vec2 o = terrain.origin();
            physics.addHeightField(glm::vec3(o.x, 0.0f, o.y), terrain.cellSize(), terrain.resolution, terrain.heights.data());
            // Spawns placed under the ground are lifted onto it.
            for (SpawnData& spawn : m_map.spawns) {
                const float ground = terrain.heightAt(spawn.position.x, spawn.position.z) + 0.05f;
                spawn.position.y = std::max(spawn.position.y, ground);
            }
        }
        if (terrain.waterEnabled) {
            const float half = terrain.size * 0.5f;
            const float y = terrain.waterLevel;
            const std::vector<Vertex> water = {{{-half, y, -half}, {0, 1, 0}, {0, 0}},
                                               {{-half, y, half}, {0, 1, 0}, {0, 1}},
                                               {{half, y, half}, {0, 1, 0}, {1, 1}},
                                               {{half, y, -half}, {0, 1, 0}, {1, 0}}};
            m_waterMesh = Mesh(water, {0, 1, 2, 0, 2, 3});
        } else {
            m_waterMesh = Mesh();
        }
    } else {
        m_terrainMesh = Mesh();
        m_waterMesh = Mesh();
    }

    m_blockBodies.clear();
    m_blockBodies.reserve(m_map.blocks.size());
    m_vegetation.clear();
    if (m_models && terrain.valid()) {
        m_vegetation.build(m_map, *m_models, physics);
    }
    for (const BlockData& block : m_map.blocks) {
        const BodyHandle body = block.dynamic
                                    ? physics.addDynamicBox(block.position, block.size * 0.5f, block.orientation(), block.mass)
                                    : physics.addStaticBox(block.position, block.size * 0.5f, block.orientation());
        m_blockBodies.push_back(body);
    }
    physics.optimize();
}

void World::submit(Renderer& renderer, const PhysicsWorld& physics) const {
    for (size_t i = 0; i < m_map.blocks.size(); ++i) {
        const BlockData& block = m_map.blocks[i];
        DrawItem item;
        item.mesh = &renderer.cube();
        if (block.dynamic && i < m_blockBodies.size() && m_blockBodies[i] != kInvalidBody) {
            const BodyTransform t = physics.transform(m_blockBodies[i]);
            item.model = glm::translate(glm::mat4(1.0f), t.position) * glm::mat4_cast(t.rotation) *
                         glm::scale(glm::mat4(1.0f), block.size);
        } else {
            item.model = blockTransform(block);
        }
        item.material.albedo = toLinear(block.color);
        item.material.roughness = block.roughness;
        item.material.metallic = block.metallic;
        item.material.gridSize = 1.0f;
        if (!block.texture.empty() && m_textures) {
            const std::filesystem::path folder = std::filesystem::path("assets/textures") / block.texture;
            item.material.shading = 3;
            item.material.gridSize = 0.0f;
            item.material.albedoMap = m_textures->load(folder / "albedo.jpg", true).id;
            item.material.normalMap = m_textures->load(folder / "normal.jpg", false).id;
            item.material.armMap = m_textures->load(folder / "arm.jpg", false).id;
            item.material.albedo = toLinear(block.color) * 2.0f; // Color acts as a tint around mid grey.
        }
        renderer.submit(item);
    }
    if (m_terrainMesh.valid()) {
        DrawItem item;
        item.mesh = &m_terrainMesh;
        item.material.shading = 1;
        item.material.roughness = 0.95f;
        item.material.waterLevel = m_map.terrain.waterEnabled ? m_map.terrain.waterLevel : -1000.0f;
        if (m_textures) {
            static const char* layers[] = {"aerial_grass_rock", "forest_ground_04", "forest_leaves_02", "brown_mud_leaves_01",
                                           "rock_face_03", "rocks_ground_05"};
            std::vector<std::filesystem::path> albedo, normal, arm;
            for (const char* id : layers) {
                const std::filesystem::path folder = std::filesystem::path("assets/textures") / id;
                albedo.push_back(folder / "albedo.jpg");
                normal.push_back(folder / "normal.jpg");
                arm.push_back(folder / "arm.jpg");
            }
            item.material.terrainAlbedo = m_textures->loadArray(albedo, true).id;
            item.material.terrainNormal = m_textures->loadArray(normal, false).id;
            item.material.terrainArm = m_textures->loadArray(arm, false).id;
        }
        renderer.submit(item);
    }
    if (m_waterMesh.valid()) {
        DrawItem item;
        item.mesh = &m_waterMesh;
        item.material.shading = 2;
        item.material.albedo = glm::vec3(0.02f, 0.05f, 0.05f);
        item.material.roughness = 0.08f;
        item.castShadow = false;
        renderer.submitTransparent(item);
    }
    for (const LightData& light : m_map.lights) {
        renderer.addLight({light.position, light.radius, toLinear(light.color), light.intensity});
    }
}

void World::submitVegetation(Renderer& renderer, const glm::mat4& viewProj, const glm::vec3& camera,
                             const VegetationSettings& settings) const {
    m_vegetation.submit(renderer, viewProj, camera, settings);
}

std::vector<BodyHandle> World::dynamicBodies() const {
    std::vector<BodyHandle> bodies;
    for (size_t i = 0; i < m_map.blocks.size() && i < m_blockBodies.size(); ++i) {
        if (m_map.blocks[i].dynamic) bodies.push_back(m_blockBodies[i]);
    }
    return bodies;
}

Environment World::environment() const {
    const EnvironmentData& e = m_map.environment;
    Environment env;
    env.sunDirection = glm::length(e.sunDirection) > 0.0f ? glm::normalize(e.sunDirection) : glm::vec3(0, 1, 0);
    env.sunColor = toLinear(e.sunColor);
    env.sunIntensity = e.sunIntensity;
    env.skyColor = toLinear(e.skyColor);
    env.groundColor = toLinear(e.groundColor);
    env.ambientIntensity = e.ambientIntensity;
    env.fogColor = toLinear(e.fogColor);
    env.fogDensity = e.fogDensity;
    return env;
}

} // namespace origon
