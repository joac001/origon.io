// Generates built-in maps from code so they stay reproducible.
// Usage: origon_mapgen bosque [output.json]
#include "core/FileSystem.h"
#include "core/Log.h"
#include "world/MapData.h"

#include <glm/gtc/constants.hpp>
#include <glm/geometric.hpp>

#include <cmath>
#include <cstdio>
#include <string>

using namespace origon;

namespace {

// Creek centerline: a meandering curve across the map along X.
float creekZ(float x) { return 12.0f * std::sin(x * 0.045f) + 6.0f * std::sin(x * 0.11f + 1.3f) - 8.0f; }

BlockData block(glm::vec3 position, glm::vec3 size, glm::vec3 color, glm::vec3 rotation = {}, float roughness = 0.85f) {
    BlockData b;
    b.position = position;
    b.size = size;
    b.color = color;
    b.rotation = rotation;
    b.roughness = roughness;
    return b;
}

void carveCreek(TerrainData& t, float bankWidth, float depth) {
    const glm::vec2 o = t.origin();
    const float cell = t.cellSize();
    for (int z = 0; z < t.resolution; ++z) {
        for (int x = 0; x < t.resolution; ++x) {
            const float wx = o.x + static_cast<float>(x) * cell;
            const float wz = o.y + static_cast<float>(z) * cell;
            const float d = std::abs(wz - creekZ(wx));
            float& h = t.heights[static_cast<size_t>(z * t.resolution + x)];
            // Gentle valley toward the creek, then a channel.
            const float valley = std::exp(-d * d / (2.0f * 30.0f * 30.0f));
            h = h * (1.0f - 0.6f * valley) - valley * 1.5f;
            if (d < bankWidth) {
                const float u = d / bankWidth;
                h = std::min(h, -depth * (1.0f - u * u) + u * u * h);
            }
        }
    }
}

void flattenArea(TerrainData& t, glm::vec2 center, float radius, float height) {
    const glm::vec2 o = t.origin();
    const float cell = t.cellSize();
    for (int z = 0; z < t.resolution; ++z) {
        for (int x = 0; x < t.resolution; ++x) {
            const glm::vec2 p(o.x + static_cast<float>(x) * cell, o.y + static_cast<float>(z) * cell);
            const float d = glm::length(p - center) / radius;
            if (d >= 1.0f) continue;
            const float w = 1.0f - std::pow(d, 4.0f);
            float& h = t.heights[static_cast<size_t>(z * t.resolution + x)];
            h = h + (height - h) * w;
        }
    }
}

void paintPath(TerrainData& t, glm::vec2 a, glm::vec2 b, float width) {
    const glm::vec2 o = t.origin();
    const float cell = t.cellSize();
    for (int z = 0; z < t.resolution; ++z) {
        for (int x = 0; x < t.resolution; ++x) {
            const glm::vec2 p(o.x + static_cast<float>(x) * cell, o.y + static_cast<float>(z) * cell);
            const glm::vec2 ab = b - a;
            const float s = glm::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
            const float d = glm::length(p - (a + ab * s)) / width;
            if (d >= 1.0f) continue;
            uint8_t& value = t.paint[static_cast<size_t>(z * t.resolution + x)];
            value = static_cast<uint8_t>(std::max<float>(value, 255.0f * (1.0f - d * d)));
        }
    }
}

MapData makeForest() {
    MapData map;
    map.name = "Bosque";
    EnvironmentData& e = map.environment;
    e.sunDirection = {-0.55f, 0.55f, 0.35f};
    e.sunColor = {1.0f, 0.87f, 0.68f};
    e.sunIntensity = 3.4f;
    e.skyColor = {0.5f, 0.62f, 0.78f};
    e.groundColor = {0.2f, 0.22f, 0.14f};
    e.ambientIntensity = 0.55f;
    e.fogColor = {0.55f, 0.63f, 0.66f};
    e.fogDensity = 0.0055f;

    TerrainData& t = map.terrain;
    t.enabled = true;
    t.size = 160.0f;
    t.resolution = 256;
    t.seed = 7;
    t.amplitude = 9.0f;
    t.frequency = 0.016f;
    t.octaves = 5;
    t.generate();
    carveCreek(t, 5.5f, 1.6f);
    t.waterEnabled = true;
    t.waterLevel = -1.15f;

    // Cabin clearing north of the creek, and a camp clearing south.
    const glm::vec2 cabin(-24.0f, 26.0f);
    const glm::vec2 camp(30.0f, -38.0f);
    flattenArea(t, cabin, 14.0f, t.heightAt(cabin.x, cabin.y));
    flattenArea(t, camp, 11.0f, t.heightAt(camp.x, camp.y));
    const float cabinY = t.heightAt(cabin.x, cabin.y);
    const float campY = t.heightAt(camp.x, camp.y);
    paintPath(t, cabin, glm::vec2(4.0f, creekZ(4.0f) + 6.0f), 2.2f);
    paintPath(t, glm::vec2(4.0f, creekZ(4.0f) - 6.0f), camp, 2.2f);
    paintPath(t, cabin, glm::vec2(-60.0f, 40.0f), 1.8f);

    const glm::vec3 wood(0.42f, 0.28f, 0.17f);
    const glm::vec3 darkWood(0.3f, 0.2f, 0.12f);
    const glm::vec3 stone(0.42f, 0.41f, 0.38f);

    // Ruined log cabin: stacked logs on three walls, a broken fourth wall, a stone chimney.
    for (int i = 0; i < 6; ++i) {
        const float y = cabinY + 0.2f + static_cast<float>(i) * 0.38f;
        map.blocks.push_back(block({cabin.x, y, cabin.y - 4.0f}, {9.0f, 0.38f, 0.4f}, i % 2 ? wood : darkWood));
        map.blocks.push_back(block({cabin.x - 4.5f, y, cabin.y}, {0.4f, 0.38f, 8.4f}, i % 2 ? darkWood : wood));
        if (i < 4) map.blocks.push_back(block({cabin.x + 4.5f, y, cabin.y + 1.8f}, {0.4f, 0.38f, 4.8f}, i % 2 ? wood : darkWood));
        if (i < 2) map.blocks.push_back(block({cabin.x - 1.0f, y, cabin.y + 4.0f}, {6.5f, 0.38f, 0.4f}, i % 2 ? darkWood : wood));
    }
    map.blocks.push_back(block({cabin.x - 3.6f, cabinY + 2.6f, cabin.y - 3.0f}, {1.4f, 5.2f, 1.4f}, stone));
    // Collapsed roof slab.
    map.blocks.push_back(block({cabin.x - 1.0f, cabinY + 1.6f, cabin.y - 1.2f}, {7.0f, 0.2f, 5.0f}, darkWood, {-24.0f, 8.0f, 4.0f}));
    map.blocks.push_back(block({cabin.x, cabinY + 0.05f, cabin.y}, {8.6f, 0.1f, 7.6f}, darkWood));

    // Wooden bridge over the creek.
    const float bridgeX = 4.0f;
    const float bridgeZ = creekZ(bridgeX);
    const float bankA = t.heightAt(bridgeX, bridgeZ + 7.0f);
    const float bankB = t.heightAt(bridgeX, bridgeZ - 7.0f);
    const float deckY = std::max(bankA, bankB) + 0.25f;
    for (int i = 0; i < 14; ++i) {
        const float z = bridgeZ - 6.5f + static_cast<float>(i);
        map.blocks.push_back(block({bridgeX, deckY, z}, {3.0f, 0.12f, 0.9f}, i % 3 ? wood : darkWood, {0.0f, (i % 2 ? 1.5f : -1.0f), 0.0f}));
    }
    for (int side = -1; side <= 1; side += 2) {
        map.blocks.push_back(block({bridgeX + 1.45f * static_cast<float>(side), deckY + 0.55f, bridgeZ}, {0.12f, 0.12f, 14.0f}, darkWood));
        for (int i = 0; i < 4; ++i) {
            const float z = bridgeZ - 6.0f + static_cast<float>(i) * 4.0f;
            map.blocks.push_back(block({bridgeX + 1.45f * static_cast<float>(side), deckY - 0.6f, z}, {0.2f, 2.4f, 0.2f}, darkWood));
        }
    }

    // Fallen logs as cover.
    struct Log { glm::vec2 p; float yaw; float length; };
    const Log logs[] = {{{-8, 10}, 30, 9}, {{18, 14}, -60, 7}, {{-40, -10}, 80, 10}, {{40, 6}, 10, 8},
                        {{-12, -40}, -20, 9}, {{10, -22}, 70, 6}, {{-52, 20}, 45, 8}, {{55, -20}, -35, 9}};
    for (const Log& l : logs) {
        const float y = t.heightAt(l.p.x, l.p.y) + 0.35f;
        map.blocks.push_back(block({l.p.x, y, l.p.y}, {l.length, 0.7f, 0.7f}, {0.3f, 0.22f, 0.15f}, {0.0f, l.yaw, 2.0f}));
    }

    // Camp: crates, tent frame, firewood and a lantern light.
    map.blocks.push_back(block({camp.x - 3.0f, campY + 0.5f, camp.y + 2.0f}, {1.0f, 1.0f, 1.0f}, wood));
    map.blocks.push_back(block({camp.x - 1.8f, campY + 0.4f, camp.y + 2.4f}, {0.8f, 0.8f, 0.8f}, darkWood, {0.0f, 25.0f, 0.0f}));
    map.blocks.push_back(block({camp.x + 2.0f, campY + 1.1f, camp.y - 1.0f}, {4.0f, 0.08f, 3.0f}, {0.35f, 0.38f, 0.24f}, {35.0f, 10.0f, 0.0f}, 0.95f));
    map.blocks.push_back(block({camp.x + 2.0f, campY + 1.1f, camp.y - 3.4f}, {4.0f, 0.08f, 3.0f}, {0.35f, 0.38f, 0.24f}, {-35.0f, 10.0f, 0.0f}, 0.95f));
    for (int i = 0; i < 3; ++i) {
        BlockData crate = block({camp.x - 6.0f + static_cast<float>(i) * 1.1f, campY + 0.4f, camp.y - 4.0f}, {0.8f, 0.8f, 0.8f}, wood);
        crate.dynamic = true;
        crate.mass = 25.0f;
        map.blocks.push_back(crate);
    }
    map.lights.push_back({{camp.x, campY + 0.6f, camp.y + 0.5f}, {1.0f, 0.55f, 0.25f}, 8.0f, 9.0f});
    map.lights.push_back({{cabin.x - 2.0f, cabinY + 1.2f, cabin.y - 2.0f}, {1.0f, 0.7f, 0.4f}, 4.0f, 7.0f});

    // Twelve spawns spread around the map (lifted onto the ground at load time).
    for (int i = 0; i < 12; ++i) {
        const float angle = glm::two_pi<float>() * static_cast<float>(i) / 12.0f + 0.2f;
        const float radius = i % 2 ? 48.0f : 30.0f;
        glm::vec3 p(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
        if (std::abs(p.z - creekZ(p.x)) < 8.0f) p.z += 12.0f;
        p.y = t.heightAt(p.x, p.z) + 0.05f;
        map.spawns.push_back({p, glm::degrees(std::atan2(p.x, p.z))});
    }
    return map;
}

} // namespace

int main(int argc, char** argv) {
    const std::string which = argc > 1 ? argv[1] : "bosque";
    const std::string output = argc > 2 ? argv[2] : (fs::resolve("maps") / (which + ".json")).string();
    if (which != "bosque") {
        log::error("Unknown map '{}'", which);
        return 1;
    }
    const MapData map = makeForest();
    return map.save(output) ? 0 : 1;
}
