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

    Environment environment() const;
    const MapData& map() const { return m_map; }

    static glm::mat4 blockTransform(const BlockData& block);
    static glm::vec3 toLinear(const glm::vec3& srgb);

private:
    MapData m_map;
    std::vector<BodyHandle> m_blockBodies; // One per block.
};

} // namespace origon
