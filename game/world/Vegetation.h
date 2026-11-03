#pragma once

#include "physics/PhysicsWorld.h"
#include "render/Model.h"
#include "render/Renderer.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace origon {

class TextureCache;
struct MapData;

// One scattered plant/rock type. Map data: every client scatters the same instances.
struct VegetationLayer {
    std::string model;       // assets/models/vegetation/<model>.glb
    float density = 1.0f;    // Instances per 100 m^2 before clumping.
    float minScale = 0.8f;
    float maxScale = 1.2f;
    float minSlope = 0.0f;   // 1 - normal.y
    float maxSlope = 0.5f;
    float waterMargin = 0.3f;
    float clumping = 0.4f;   // 0 uniform, 1 strongly clustered.
    float avoidPaths = 0.4f; // Skip where the path paint is above this.
    float sink = 0.05f;      // Meters pushed into the ground.
    float wind = 0.0f;
    bool collision = false;
    bool alignToGround = false;
    bool groundCover = false; // Only these follow the vegetation density/distance settings.
    uint32_t seed = 1;
};

// Loads models once and keeps their physics shapes.
class ModelLibrary {
public:
    explicit ModelLibrary(TextureCache* textures) : m_textures(textures) {}
    ~ModelLibrary();
    Model* get(const std::string& name);
    ShapeHandle collisionShape(const std::string& name, PhysicsWorld& physics);

private:
    TextureCache* m_textures;
    std::map<std::string, std::unique_ptr<Model>> m_models;
    std::map<std::string, ShapeHandle> m_shapes;
};

struct VegetationSettings {
    float density = 1.0f;   // Fraction of ground cover drawn (0.2 - 1).
    float distance = 90.0f; // Ground cover draw distance (m).
    float lodBias = 1.0f;
};

class VegetationField {
public:
    // Scatters every layer and creates collision bodies (call after the terrain body exists).
    void build(const MapData& map, ModelLibrary& library, PhysicsWorld& physics);
    void clear();
    void submit(Renderer& renderer, const glm::mat4& viewProj, const glm::vec3& cameraPosition,
                const VegetationSettings& settings) const;
    size_t instanceCount() const;

private:
    struct Instance {
        glm::mat4 transform;
        glm::vec3 position;
        float scale;
        float keep; // 0..1, compared against the density setting for ground cover.
    };
    struct Layer {
        VegetationLayer config;
        Model* model = nullptr;
        std::vector<Instance> instances;
        mutable std::vector<std::vector<glm::mat4>> lodMatrices;
    };
    std::vector<Layer> m_layers;
};

} // namespace origon
