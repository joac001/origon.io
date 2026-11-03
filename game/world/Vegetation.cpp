#include "world/Vegetation.h"

#include "core/Log.h"
#include "render/Model.h"
#include "world/MapData.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <random>

namespace origon {
namespace {

float hashNoise(int x, int z, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(z) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

float smoothNoise(float x, float z, uint32_t seed) {
    const int ix = static_cast<int>(std::floor(x));
    const int iz = static_cast<int>(std::floor(z));
    const float fx = x - static_cast<float>(ix);
    const float fz = z - static_cast<float>(iz);
    const float ux = fx * fx * (3.0f - 2.0f * fx);
    const float uz = fz * fz * (3.0f - 2.0f * fz);
    const float a = hashNoise(ix, iz, seed), b = hashNoise(ix + 1, iz, seed);
    const float c = hashNoise(ix, iz + 1, seed), d = hashNoise(ix + 1, iz + 1, seed);
    return glm::mix(glm::mix(a, b, ux), glm::mix(c, d, ux), uz);
}

bool insideBlock(const BlockData& block, const glm::vec3& p, float margin) {
    const glm::vec3 local = glm::inverse(block.orientation()) * (p - block.position);
    const glm::vec3 half = block.size * 0.5f + glm::vec3(margin, 1.0f, margin);
    return std::abs(local.x) < half.x && std::abs(local.z) < half.z && std::abs(local.y) < half.y + 2.0f;
}

} // namespace

ModelLibrary::~ModelLibrary() = default;

Model* ModelLibrary::get(const std::string& name) {
    auto it = m_models.find(name);
    if (it != m_models.end()) {
        return it->second.get();
    }
    auto model = std::make_unique<Model>();
    if (!model->load(std::filesystem::path("assets/models/vegetation") / (name + ".glb"), m_textures)) {
        m_models.emplace(name, nullptr);
        return nullptr;
    }
    Model* raw = model.get();
    m_models.emplace(name, std::move(model));
    return raw;
}

Model* ModelLibrary::getWeapon(const std::string& id) {
    const std::string key = "weapon:" + id;
    auto it = m_models.find(key);
    if (it != m_models.end()) {
        return it->second.get();
    }
    auto model = std::make_unique<Model>();
    if (!model->load(std::filesystem::path("assets/models/weapons") / (id + ".glb"), m_textures)) {
        m_models.emplace(key, nullptr);
        return nullptr;
    }
    Model* raw = model.get();
    m_models.emplace(key, std::move(model));
    return raw;
}

ShapeHandle ModelLibrary::collisionShape(const std::string& name, PhysicsWorld& physics) {
    auto it = m_shapes.find(name);
    if (it != m_shapes.end()) {
        return it->second;
    }
    ShapeHandle handle = -1;
    if (Model* model = get(name); model && !model->collisionIndices().empty()) {
        handle = physics.createMeshShape(model->collisionVertices(), model->collisionIndices());
    }
    m_shapes.emplace(name, handle);
    return handle;
}

void VegetationField::clear() { m_layers.clear(); }

size_t VegetationField::instanceCount() const {
    size_t count = 0;
    for (const Layer& layer : m_layers) count += layer.instances.size();
    return count;
}

void VegetationField::build(const MapData& map, ModelLibrary& library, PhysicsWorld& physics) {
    m_layers.clear();
    const TerrainData& terrain = map.terrain;
    if (!terrain.valid()) {
        return;
    }
    const glm::vec2 origin = terrain.origin();
    for (size_t li = 0; li < map.vegetation.size(); ++li) {
        const VegetationLayer& config = map.vegetation[li];
        Layer layer;
        layer.config = config;
        layer.model = library.get(config.model);
        if (!layer.model) {
            continue;
        }
        const ShapeHandle shape = config.collision ? library.collisionShape(config.model, physics) : -1;
        std::mt19937 rng(config.seed * 7919u + static_cast<uint32_t>(li));
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        const float cell = std::sqrt(100.0f / std::max(config.density, 0.001f));
        const int cells = static_cast<int>(terrain.size / cell);
        const float footprint = layer.model->radius();

        for (int gz = 0; gz < cells; ++gz) {
            for (int gx = 0; gx < cells; ++gx) {
                const float jx = unit(rng), jz = unit(rng), keep = unit(rng), scaleRand = unit(rng), yawRand = unit(rng);
                const float x = origin.x + (static_cast<float>(gx) + jx) * cell;
                const float z = origin.y + (static_cast<float>(gz) + jz) * cell;
                // Clumping: low-frequency noise thins out areas.
                const float clump = smoothNoise(x * 0.04f, z * 0.04f, config.seed + 31u);
                if (keep > glm::mix(1.0f, glm::smoothstep(0.25f, 0.75f, clump) * 1.4f, config.clumping)) continue;
                const float h = terrain.heightAt(x, z);
                const glm::vec3 normal = terrain.normalAt(x, z);
                const float slope = 1.0f - normal.y;
                if (slope < config.minSlope || slope > config.maxSlope) continue;
                if (terrain.waterEnabled && h < terrain.waterLevel + config.waterMargin) continue;
                if (terrain.paintAt(x, z) > config.avoidPaths) continue;
                const float scale = glm::mix(config.minScale, config.maxScale, scaleRand);
                const glm::vec3 p(x, h, z);
                bool blocked = false;
                for (const BlockData& block : map.blocks) {
                    if (!block.dynamic && insideBlock(block, p, footprint * scale * 0.6f + 0.3f)) {
                        blocked = true;
                        break;
                    }
                }
                if (config.collision) {
                    for (const SpawnData& spawn : map.spawns) {
                        if (glm::length(glm::vec2(spawn.position.x - x, spawn.position.z - z)) < 2.5f + footprint * scale * 0.5f) {
                            blocked = true;
                        }
                    }
                }
                if (blocked) continue;

                const float yaw = yawRand * 6.2831853f;
                glm::quat rotation = glm::angleAxis(yaw, glm::vec3(0, 1, 0));
                if (config.alignToGround) {
                    const glm::vec3 up(0, 1, 0);
                    const glm::vec3 axis = glm::cross(up, normal);
                    if (glm::length(axis) > 1e-4f) {
                        rotation = glm::angleAxis(std::acos(glm::clamp(normal.y, -1.0f, 1.0f)), glm::normalize(axis)) * rotation;
                    }
                } else {
                    // Slight random lean for trees.
                    rotation = glm::angleAxis((unit(rng) - 0.5f) * 0.06f, glm::vec3(1, 0, 0)) * rotation;
                }
                const glm::vec3 position(x, h - config.sink * scale, z);
                Instance instance;
                instance.transform = glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation) *
                                     glm::scale(glm::mat4(1.0f), glm::vec3(scale));
                instance.position = position;
                instance.scale = scale;
                instance.keep = unit(rng);
                layer.instances.push_back(instance);
                if (shape >= 0) {
                    physics.addStaticShape(shape, position, rotation, scale);
                }
            }
        }
        layer.lodMatrices.resize(Model::kLodCount);
        log::info("Vegetation: {} x{}", config.model, layer.instances.size());
        m_layers.push_back(std::move(layer));
    }
    physics.optimize();
}

void VegetationField::submit(Renderer& renderer, const glm::mat4& viewProj, const glm::vec3& cameraPosition,
                             const VegetationSettings& settings) const {
    // Frustum planes (Gribb-Hartmann).
    glm::vec4 planes[6];
    const glm::mat4 m = glm::transpose(viewProj);
    planes[0] = m[3] + m[0];
    planes[1] = m[3] - m[0];
    planes[2] = m[3] + m[1];
    planes[3] = m[3] - m[1];
    planes[4] = m[3] + m[2];
    planes[5] = m[3] - m[2];
    for (glm::vec4& plane : planes) plane /= glm::length(glm::vec3(plane));

    for (const Layer& layer : m_layers) {
        const Model& model = *layer.model;
        for (auto& list : layer.lodMatrices) list.clear();
        const float height = model.height();
        const float lod0 = std::max(18.0f, height * 2.2f) * settings.lodBias;
        const float lod1 = std::max(45.0f, height * 5.5f) * settings.lodBias;
        const float maxDistance = layer.config.groundCover ? settings.distance : 500.0f;
        const float maxDistance2 = maxDistance * maxDistance;
        for (const Instance& instance : layer.instances) {
            if (layer.config.groundCover && instance.keep > settings.density) continue;
            const glm::vec3 center = instance.position + glm::vec3(0.0f, height * 0.5f * instance.scale, 0.0f);
            const glm::vec3 delta = center - cameraPosition;
            const float distance2 = glm::dot(delta, delta);
            if (distance2 > maxDistance2) continue;
            const float radius = std::max(height * 0.5f, model.radius()) * instance.scale;
            bool visible = true;
            for (const glm::vec4& plane : planes) {
                if (glm::dot(glm::vec3(plane), center) + plane.w < -radius) {
                    visible = false;
                    break;
                }
            }
            if (!visible) continue;
            const float distance = std::sqrt(distance2);
            const int lod = distance < lod0 ? 0 : (distance < lod1 ? 1 : 2);
            layer.lodMatrices[static_cast<size_t>(std::min(lod, model.lodCount() - 1))].push_back(instance.transform);
        }
        for (int lod = 0; lod < model.lodCount(); ++lod) {
            const auto& matrices = layer.lodMatrices[static_cast<size_t>(lod)];
            if (matrices.empty()) continue;
            for (const ModelPart& part : model.lod(lod)) {
                DrawItem item;
                item.mesh = &part.mesh;
                item.material = part.material;
                item.material.windStrength = layer.config.wind;
                item.material.windHeight = height;
                item.instances = matrices.data();
                item.instanceCount = static_cast<int>(matrices.size());
                item.castShadow = !layer.config.groundCover || lod == 0;
                renderer.submit(item);
            }
        }
    }
}

} // namespace origon
