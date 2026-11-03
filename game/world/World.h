#pragma once

#include "physics/PhysicsWorld.h"
#include "render/Renderer.h"
#include "world/MapData.h"

#include <glm/mat4x4.hpp>

#include <vector>

namespace origon {

// Builds runtime data (collision + render items) from MapData.
// Collision always uses the full map data, independent of graphics settings.
class World {
public:
    // Rebuilds every body. Dynamic props return to their map positions.
    void build(const MapData& map, PhysicsWorld& physics);
    void submit(Renderer& renderer, const PhysicsWorld& physics) const;
    // Vegetation needs a model library (null: no vegetation, e.g. headless tests).
    void setModelLibrary(ModelLibrary* library) { m_models = library; }
    void submitVegetation(Renderer& renderer, const glm::mat4& viewProj, const glm::vec3& camera,
                          const VegetationSettings& settings) const;
    const VegetationField& vegetation() const { return m_vegetation; }

    Environment environment() const;
    const MapData& map() const { return m_map; }

    static glm::mat4 blockTransform(const BlockData& block);
    static glm::vec3 toLinear(const glm::vec3& srgb);
    // Ground height (terrain only; 0 without terrain).
    float terrainHeight(float x, float z) const { return m_map.terrain.heightAt(x, z); }

private:
    MapData m_map;
    Mesh m_terrainMesh;
    Mesh m_waterMesh;
    ModelLibrary* m_models = nullptr;
    VegetationField m_vegetation;
    std::vector<BodyHandle> m_blockBodies; // One per block.
};

} // namespace origon
