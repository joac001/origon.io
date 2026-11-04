#pragma once

#include "render/Mesh.h"
#include "render/Renderer.h"

#include <glm/vec3.hpp>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace origon {

class TextureCache;

struct ModelPart {
    Mesh mesh;
    Material material;
    std::string materialName;
};

// A model from the asset pipeline: nodes named <name>_LOD0..2 and an optional <name>_COL.
// Materials resolve their textures by name: bark_<id>/rock_<id>/pbr_<id> use
// assets/textures/<id>/{albedo,normal,arm}.jpg, foliage_<name> uses assets/textures/foliage/<name>.png.
class Model {
public:
    static constexpr int kLodCount = 4; // LOD3: impostor (crossed quads) on trees.

    // textures == nullptr loads geometry for collision only (no GL, for servers and tests).
    bool load(const std::filesystem::path& path, TextureCache* textures);

    const std::vector<ModelPart>& lod(int level) const;
    std::vector<ModelPart>& mutableLod(int level) { return m_lods[static_cast<size_t>(level)]; }
    int lodCount() const { return m_lodCount; }
    // Collision triangles (from _COL, else empty).
    const std::vector<glm::vec3>& collisionVertices() const { return m_collisionVertices; }
    const std::vector<uint32_t>& collisionIndices() const { return m_collisionIndices; }
    float height() const { return m_height; }
    const glm::vec3& boundsMin() const { return m_boundsMin; }
    const glm::vec3& boundsMax() const { return m_boundsMax; }
    float radius() const { return m_radius; }
    bool hasFoliage() const { return m_hasFoliage; }

private:
    std::array<std::vector<ModelPart>, kLodCount> m_lods;
    int m_lodCount = 0;
    std::vector<glm::vec3> m_collisionVertices;
    std::vector<uint32_t> m_collisionIndices;
    float m_height = 1.0f;
    glm::vec3 m_boundsMin{0.0f};
    glm::vec3 m_boundsMax{0.0f};
    float m_radius = 1.0f;
    bool m_hasFoliage = false;
};

} // namespace origon
