#include "world/World.h"

#include "physics/PhysicsWorld.h"

#include <glm/exponential.hpp>
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
    m_blockBodies.clear();
    m_blockBodies.reserve(m_map.blocks.size());
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
        renderer.submit(item);
    }
    for (const LightData& light : m_map.lights) {
        renderer.addLight({light.position, light.radius, toLinear(light.color), light.intensity});
    }
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
