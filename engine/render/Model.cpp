#include "render/Model.h"

#include "core/FileSystem.h"
#include "core/Log.h"
#include "render/Texture.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <glm/gtc/type_ptr.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>

#include <algorithm>
#include <cmath>
#include <map>

namespace origon {
namespace {

Material resolveMaterial(const cgltf_material* source, TextureCache* textures, bool& foliage) {
    Material material;
    material.roughness = 0.8f;
    foliage = false;
    if (!source) {
        return material;
    }
    if (source->has_pbr_metallic_roughness) {
        const float* c = source->pbr_metallic_roughness.base_color_factor;
        material.albedo = glm::vec3(c[0], c[1], c[2]);
        material.roughness = source->pbr_metallic_roughness.roughness_factor;
        material.metallic = source->pbr_metallic_roughness.metallic_factor;
    }
    const std::string name = source->name ? source->name : "";
    if (!textures) {
        foliage = name.rfind("foliage_", 0) == 0;
        return material;
    }
    auto startsWith = [&](const char* prefix) { return name.rfind(prefix, 0) == 0; };
    if (startsWith("foliage_")) {
        const std::string id = name.substr(8);
        material.albedoMap = textures->load(std::filesystem::path("assets/textures/foliage") / (id + ".png"), true).id;
        material.albedo = glm::vec3(1.0f);
        material.alphaTest = true;
        material.roughness = 0.75f;
        material.metallic = 0.0f;
        foliage = true;
    } else if (startsWith("bark_") || startsWith("rock_") || startsWith("pbr_")) {
        // pbr_<id>[@variant]: the base color is kept as a tint (one texture set, many paint colors).
        std::string id = name.substr(name.find('_') + 1);
        const bool tinted = startsWith("pbr_");
        if (const size_t at = id.find('@'); at != std::string::npos) id = id.substr(0, at);
        if (const size_t dot = id.find('.'); dot != std::string::npos) id = id.substr(0, dot); // Blender duplicates (.001).
        const std::filesystem::path folder = std::filesystem::path("assets/textures") / id;
        material.albedoMap = textures->load(folder / "albedo.jpg", true).id;
        material.normalMap = textures->load(folder / "normal.jpg", false).id;
        material.armMap = textures->load(folder / "arm.jpg", false).id;
        if (!tinted) material.albedo = glm::vec3(1.0f);
        material.roughness = 0.8f;
        material.metallic = 0.0f;
    }
    return material;
}

int lodFromName(const std::string& name) {
    const size_t pos = name.rfind("_LOD");
    if (pos == std::string::npos || pos + 4 >= name.size()) return -1;
    return std::clamp(name[pos + 4] - '0', 0, Model::kLodCount - 1);
}

bool isCollision(const std::string& name) {
    return name.size() >= 4 && name.compare(name.size() - 4, 4, "_COL") == 0;
}

} // namespace

const std::vector<ModelPart>& Model::lod(int level) const {
    return m_lods[static_cast<size_t>(std::clamp(level, 0, std::max(m_lodCount - 1, 0)))];
}

bool Model::load(const std::filesystem::path& path, TextureCache* textures) {
    const std::string resolved = fs::resolve(path).string();
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, resolved.c_str(), &data) != cgltf_result_success) {
        log::error("Model: cannot parse {}", resolved);
        return false;
    }
    if (cgltf_load_buffers(&options, data, resolved.c_str()) != cgltf_result_success) {
        log::error("Model: cannot load buffers for {}", resolved);
        cgltf_free(data);
        return false;
    }

    // Geometry grouped per LOD and material.
    struct Batch {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        Material material;
        std::string materialName;
    };
    std::array<std::map<const cgltf_material*, Batch>, kLodCount> batches;
    glm::vec3 minBounds(1e9f), maxBounds(-1e9f);

    for (size_t n = 0; n < data->nodes_count; ++n) {
        const cgltf_node& node = data->nodes[n];
        if (!node.mesh) continue;
        const std::string name = node.name ? node.name : "";
        const bool collision = isCollision(name);
        const int lod = collision ? -1 : std::max(lodFromName(name), 0);
        float world[16];
        cgltf_node_transform_world(&node, world);
        const glm::mat4 transform = glm::make_mat4(world);
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));

        for (size_t p = 0; p < node.mesh->primitives_count; ++p) {
            const cgltf_primitive& prim = node.mesh->primitives[p];
            if (prim.type != cgltf_primitive_type_triangles) continue;
            const cgltf_accessor* positions = nullptr;
            const cgltf_accessor* normals = nullptr;
            const cgltf_accessor* uvs = nullptr;
            for (size_t a = 0; a < prim.attributes_count; ++a) {
                const cgltf_attribute& attr = prim.attributes[a];
                if (attr.type == cgltf_attribute_type_position) positions = attr.data;
                if (attr.type == cgltf_attribute_type_normal) normals = attr.data;
                if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) uvs = attr.data;
            }
            if (!positions) continue;

            std::vector<glm::vec3> pos(positions->count);
            for (size_t i = 0; i < positions->count; ++i) {
                float v[3] = {};
                cgltf_accessor_read_float(positions, i, v, 3);
                pos[i] = glm::vec3(transform * glm::vec4(v[0], v[1], v[2], 1.0f));
            }
            std::vector<uint32_t> indices;
            if (prim.indices) {
                indices.resize(prim.indices->count);
                for (size_t i = 0; i < prim.indices->count; ++i) {
                    indices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, i));
                }
            } else {
                indices.resize(pos.size());
                for (size_t i = 0; i < pos.size(); ++i) indices[i] = static_cast<uint32_t>(i);
            }

            if (collision) {
                const auto base = static_cast<uint32_t>(m_collisionVertices.size());
                m_collisionVertices.insert(m_collisionVertices.end(), pos.begin(), pos.end());
                for (uint32_t index : indices) m_collisionIndices.push_back(base + index);
                continue;
            }

            Batch& batch = batches[static_cast<size_t>(lod)][prim.material];
            if (batch.vertices.empty()) {
                bool foliage = false;
                batch.material = resolveMaterial(prim.material, textures, foliage);
                batch.materialName = prim.material && prim.material->name ? prim.material->name : "";
                m_hasFoliage |= foliage;
            }
            const auto base = static_cast<uint32_t>(batch.vertices.size());
            for (size_t i = 0; i < pos.size(); ++i) {
                Vertex vertex{};
                vertex.position = pos[i];
                if (normals) {
                    float v[3] = {};
                    cgltf_accessor_read_float(normals, i, v, 3);
                    vertex.normal = glm::normalize(normalMatrix * glm::vec3(v[0], v[1], v[2]));
                } else {
                    vertex.normal = glm::vec3(0, 1, 0);
                }
                if (uvs) {
                    float v[2] = {};
                    cgltf_accessor_read_float(uvs, i, v, 2);
                    vertex.uv = glm::vec2(v[0], v[1]);
                }
                batch.vertices.push_back(vertex);
                if (lod == 0) {
                    minBounds = glm::min(minBounds, pos[i]);
                    maxBounds = glm::max(maxBounds, pos[i]);
                }
            }
            for (uint32_t index : indices) batch.indices.push_back(base + index);
        }
    }
    cgltf_free(data);

    m_lodCount = 0;
    for (int lod = 0; lod < kLodCount; ++lod) {
        for (auto& [key, batch] : batches[static_cast<size_t>(lod)]) {
            m_lods[static_cast<size_t>(lod)].push_back({textures ? Mesh(batch.vertices, batch.indices) : Mesh(), batch.material, batch.materialName});
        }
        if (!m_lods[static_cast<size_t>(lod)].empty()) m_lodCount = lod + 1;
    }
    if (m_lodCount == 0) {
        log::error("Model: no LOD geometry in {}", resolved);
        return false;
    }
    m_boundsMin = minBounds;
    m_boundsMax = maxBounds;
    m_height = std::max(maxBounds.y, 0.1f);
    m_radius = std::max({std::abs(minBounds.x), std::abs(maxBounds.x), std::abs(minBounds.z), std::abs(maxBounds.z), 0.1f});
    return true;
}

} // namespace origon
