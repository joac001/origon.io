// Map generators: the built-in maps (reproducible from code) and procedural maps from a seed.
#include "world/MapGenerator.h"

#include <glm/gtc/constants.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace origon {
namespace {

// Creek centerline: a meandering curve across the map along X.
float creekZ(float x) { return 12.0f * std::sin(x * 0.045f) + 6.0f * std::sin(x * 0.11f + 1.3f) - 8.0f; }

BlockData block(glm::vec3 position, glm::vec3 size, glm::vec3 color, glm::vec3 rotation = {}, float roughness = 0.85f) {
    BlockData b;
    // Structures use scanned textures; the color is a tint.
    b.texture = color.r > color.b + 0.05f ? "wood_planks_dirt" : "mossy_rock";
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

void paintDisc(TerrainData& t, glm::vec2 center, float radius) {
    const glm::vec2 o = t.origin();
    const float cell = t.cellSize();
    for (int z = 0; z < t.resolution; ++z) {
        for (int x = 0; x < t.resolution; ++x) {
            const glm::vec2 p(o.x + static_cast<float>(x) * cell, o.y + static_cast<float>(z) * cell);
            const float d = glm::length(p - center) / radius;
            if (d >= 1.0f) continue;
            uint8_t& value = t.paint[static_cast<size_t>(z * t.resolution + x)];
            value = static_cast<uint8_t>(std::max<float>(value, 255.0f * std::min(1.0f, 1.6f * (1.0f - d * d))));
        }
    }
}

VegetationLayer* addLayer(MapData& map, const char* model, float density, float minScale, float maxScale, float maxSlope,
                          bool collision, float wind, uint32_t seed) {
    VegetationLayer v;
    v.model = model;
    v.density = density;
    v.minScale = minScale;
    v.maxScale = maxScale;
    v.maxSlope = maxSlope;
    v.collision = collision;
    v.wind = wind;
    v.seed = seed;
    map.vegetation.push_back(v);
    return &map.vegetation.back();
}

// Twelve spawns around the map. Each searches outward from its preferred spot for dry, flat ground
// away from structures. `dry` adds map-specific rules (creeks, lakes).
template <typename DryFn>
void placeSpawns(MapData& map, float innerRadius, float outerRadius, DryFn dry) {
    const TerrainData& t = map.terrain;
    for (int i = 0; i < 12; ++i) {
        const float angle = glm::two_pi<float>() * static_cast<float>(i) / 12.0f + 0.2f;
        const float radius = i % 2 ? outerRadius : innerRadius;
        glm::vec3 best(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
        for (int attempt = 0; attempt < 64; ++attempt) {
            const float a = angle + static_cast<float>(attempt) * 0.37f;
            const float r = radius + static_cast<float>(attempt % 8) * 2.5f - 8.0f;
            glm::vec3 p(std::cos(a) * r, 0.0f, std::sin(a) * r);
            const float h = t.heightAt(p.x, p.z);
            const bool wet = t.waterEnabled && h < t.waterLevel + 0.8f;
            const bool flat = t.normalAt(p.x, p.z).y > 0.92f;
            bool clear = true;
            for (const BlockData& b : map.blocks) {
                if (glm::length(glm::vec2(b.position.x - p.x, b.position.z - p.z)) < glm::length(glm::vec2(b.size.x, b.size.z)) * 0.5f + 2.5f) {
                    clear = false;
                }
            }
            if (!wet && dry(p) && flat && clear) {
                best = p;
                break;
            }
        }
        best.y = t.heightAt(best.x, best.z) + 0.05f;
        // Face the map center.
        map.spawns.push_back({best, glm::degrees(std::atan2(best.x, best.z))});
    }
}

MapData makeForest() {
    MapData map;
    map.name = "Bosque";
    EnvironmentData& e = map.environment;
    e.sunDirection = {-0.55f, 0.55f, 0.35f};
    e.sunColor = {1.0f, 0.87f, 0.68f};
    e.sunIntensity = 3.1f;
    e.skyColor = {0.52f, 0.62f, 0.72f};
    e.groundColor = {0.2f, 0.24f, 0.12f};
    e.ambientIntensity = 0.42f;
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

    // Vegetation: giant redwoods, mixed conifers, maples near water, rocks, logs and understory.
    auto layer = [&](const char* model, float density, float minScale, float maxScale, float maxSlope, bool collision, float wind,
                     uint32_t seed) { return addLayer(map, model, density, minScale, maxScale, maxSlope, collision, wind, seed); };
    layer("redwood", 0.05f, 0.85f, 1.15f, 0.25f, true, 0.15f, 1)->clumping = 0.2f;
    layer("pine_a", 0.45f, 0.75f, 1.25f, 0.35f, true, 0.3f, 2);
    layer("pine_b", 0.45f, 0.75f, 1.2f, 0.35f, true, 0.3f, 3);
    layer("fir_a", 0.35f, 0.8f, 1.3f, 0.4f, true, 0.35f, 4);
    VegetationLayer* maple = layer("maple", 0.18f, 0.8f, 1.2f, 0.3f, true, 0.4f, 5);
    maple->clumping = 0.7f;
    VegetationLayer* rocks = layer("rock_a", 0.35f, 0.6f, 1.6f, 0.9f, true, 0.0f, 6);
    rocks->alignToGround = true;
    rocks->sink = 0.25f;
    rocks->waterMargin = -2.0f;
    VegetationLayer* pebbles = layer("rock_b", 0.6f, 0.4f, 1.2f, 0.9f, true, 0.0f, 7);
    pebbles->alignToGround = true;
    pebbles->sink = 0.2f;
    pebbles->waterMargin = -2.0f;
    VegetationLayer* boulders = layer("boulder", 0.04f, 0.8f, 1.3f, 0.6f, true, 0.0f, 8);
    boulders->alignToGround = true;
    boulders->sink = 0.6f;
    layer("log", 0.04f, 0.8f, 1.2f, 0.25f, true, 0.0f, 9)->alignToGround = true;
    layer("stump", 0.08f, 0.8f, 1.3f, 0.35f, true, 0.0f, 10)->sink = 0.1f;
    layer("bush", 0.9f, 0.7f, 1.3f, 0.45f, false, 0.25f, 11)->clumping = 0.6f;
    VegetationLayer* ferns = layer("fern", 12.0f, 0.7f, 1.3f, 0.5f, false, 0.35f, 12);
    ferns->clumping = 0.7f;
    VegetationLayer* grass = layer("grass", 140.0f, 0.6f, 1.1f, 0.45f, false, 0.5f, 13);
    grass->groundCover = true;
    grass->clumping = 0.5f;

    placeSpawns(map, 32.0f, 50.0f, [](const glm::vec3& p) { return std::abs(p.z - creekZ(p.x)) > 9.0f; });
    return map;
}


// Aserradero: a logging camp in a mountain bowl beside a lake, late-afternoon light.
// Smaller than the forest (faster rounds): a central mill yard with buildings, a watchtower
// overlooking it, a dock on the lake, and wooded ridges around the rim.
MapData makeSawmill() {
    MapData map;
    map.name = "Aserradero";
    EnvironmentData& e = map.environment;
    e.sunDirection = {0.72f, 0.3f, -0.45f};
    e.sunColor = {1.0f, 0.74f, 0.48f};
    e.sunIntensity = 2.9f;
    e.skyColor = {0.6f, 0.62f, 0.68f};
    e.groundColor = {0.24f, 0.22f, 0.14f};
    e.ambientIntensity = 0.38f;
    e.fogColor = {0.72f, 0.64f, 0.56f};
    e.fogDensity = 0.0032f;

    TerrainData& t = map.terrain;
    t.enabled = true;
    t.size = 140.0f;
    t.resolution = 256;
    t.seed = 21;
    t.amplitude = 6.0f;
    t.frequency = 0.022f;
    t.octaves = 5;
    t.generate();

    // Bowl: ridges rise toward the rim, a lake fills the north-east.
    const glm::vec2 lake(32.0f, 30.0f);
    const float lakeRadius = 21.0f;
    {
        const glm::vec2 o = t.origin();
        const float cell = t.cellSize();
        for (int z = 0; z < t.resolution; ++z) {
            for (int x = 0; x < t.resolution; ++x) {
                const glm::vec2 p(o.x + static_cast<float>(x) * cell, o.y + static_cast<float>(z) * cell);
                float& h = t.heights[static_cast<size_t>(z * t.resolution + x)];
                const float rim = glm::smoothstep(40.0f, 68.0f, std::max(std::abs(p.x), std::abs(p.y)));
                h += rim * 11.0f;
                // Lake shore: an irregular radius so the water edge is not a circle.
                const float angle = std::atan2(p.y - lake.y, p.x - lake.x);
                const float radius = lakeRadius * (1.0f + 0.18f * std::sin(angle * 3.0f + 0.7f) + 0.08f * std::sin(angle * 7.0f));
                const float d = glm::length(p - lake) / radius;
                if (d < 1.6f) {
                    const float bowl = 1.0f - glm::smoothstep(0.0f, 1.6f, d);
                    h = glm::mix(h, -3.2f, bowl * bowl * (3.0f - 2.0f * bowl));
                }
            }
        }
    }
    t.waterEnabled = true;
    t.waterLevel = -0.9f;

    const glm::vec2 yard(-10.0f, -6.0f);
    flattenArea(t, yard, 24.0f, 0.6f);
    const float yardY = t.heightAt(yard.x, yard.y);
    paintDisc(t, yard, 21.0f);
    // Logging road from the south-west edge into the yard, a track to the dock and one to the tower.
    paintPath(t, glm::vec2(-66.0f, -50.0f), yard, 3.2f);
    paintPath(t, yard, glm::vec2(16.0f, 14.0f), 2.0f);
    const glm::vec2 tower(18.0f, -30.0f);
    flattenArea(t, tower, 6.0f, t.heightAt(tower.x, tower.y));
    paintPath(t, yard, tower, 1.6f);
    paintDisc(t, tower, 5.0f);

    const glm::vec3 wood(0.44f, 0.3f, 0.18f);
    const glm::vec3 darkWood(0.3f, 0.2f, 0.12f);
    const glm::vec3 freshWood(0.62f, 0.46f, 0.3f);
    const glm::vec3 stone(0.45f, 0.43f, 0.4f);
    // Log: a cylinder block (axis along its local Z) with scanned bark.
    auto roundLog = [&](glm::vec3 center, float length, float diameter, float yaw, glm::vec3 color) {
        BlockData b = block(center, {diameter, diameter, length}, color, {0.0f, yaw, 0.0f});
        b.cylinder = true;
        b.texture = "bark_brown_02";
        map.blocks.push_back(b);
    };
    auto metal = [](glm::vec3 position, glm::vec3 size, glm::vec3 color, glm::vec3 rotation = {}) {
        BlockData b = block(position, size, color, rotation, 0.6f);
        b.texture = "metal_plate";
        b.metallic = 0.6f;
        return b;
    };

    // Mill shed: open-sided, corrugated roof on posts, a saw bench with a log on it.
    const glm::vec2 mill = yard + glm::vec2(-6.0f, 7.0f);
    for (int i = 0; i < 4; ++i) {
        for (int side = -1; side <= 1; side += 2) {
            const float x = mill.x - 7.5f + static_cast<float>(i) * 5.0f;
            map.blocks.push_back(block({x, yardY + 1.8f, mill.y + 3.2f * static_cast<float>(side)}, {0.3f, 3.6f, 0.3f}, darkWood));
        }
    }
    map.blocks.push_back(metal({mill.x, yardY + 3.85f, mill.y - 1.7f}, {16.5f, 0.08f, 3.8f}, {0.5f, 0.42f, 0.36f}, {-14.0f, 0.0f, 0.0f}));
    map.blocks.push_back(metal({mill.x, yardY + 3.85f, mill.y + 1.7f}, {16.5f, 0.08f, 3.8f}, {0.5f, 0.42f, 0.36f}, {14.0f, 0.0f, 0.0f}));
    map.blocks.push_back(block({mill.x, yardY + 0.45f, mill.y}, {11.0f, 0.9f, 1.4f}, darkWood));        // Saw bench.
    roundLog({mill.x - 1.0f, yardY + 1.25f, mill.y}, 8.0f, 0.75f, 90.0f, wood); // Log on the bench.
    map.blocks.push_back(metal({mill.x + 2.5f, yardY + 1.4f, mill.y + 0.9f}, {1.6f, 1.6f, 0.06f}, {0.55f, 0.55f, 0.58f})); // Saw blade guard.
    map.blocks.push_back(metal({mill.x + 6.0f, yardY + 0.8f, mill.y - 1.8f}, {2.0f, 1.6f, 1.4f}, {0.35f, 0.3f, 0.26f}));  // Engine.

    // Bunkhouse: four walls with a door and windows, a flat roof.
    const glm::vec2 bunk = yard + glm::vec2(10.0f, -9.0f);
    const float wallH = 2.8f;
    auto wall = [&](glm::vec2 a, glm::vec2 b, float gapStart, float gapEnd, float gapBottom, float gapTop) {
        // A wall from a to b (axis aligned) with one opening between gapStart..gapEnd (meters along the wall).
        const glm::vec2 dir = glm::normalize(b - a);
        const float length = glm::length(b - a);
        const bool alongX = std::abs(dir.x) > 0.5f;
        auto piece = [&](float s0, float s1, float y0, float y1) {
            if (s1 - s0 < 0.05f || y1 - y0 < 0.05f) return;
            const glm::vec2 c = a + dir * ((s0 + s1) * 0.5f);
            const glm::vec3 size = alongX ? glm::vec3(s1 - s0, y1 - y0, 0.25f) : glm::vec3(0.25f, y1 - y0, s1 - s0);
            map.blocks.push_back(block({c.x, yardY + (y0 + y1) * 0.5f, c.y}, size, wood));
        };
        piece(0.0f, gapStart, 0.0f, wallH);
        piece(gapEnd, length, 0.0f, wallH);
        piece(gapStart, gapEnd, 0.0f, gapBottom);
        piece(gapStart, gapEnd, gapTop, wallH);
    };
    const glm::vec2 b0 = bunk + glm::vec2(-5.0f, -3.5f), b1 = bunk + glm::vec2(5.0f, -3.5f);
    const glm::vec2 b2 = bunk + glm::vec2(5.0f, 3.5f), b3 = bunk + glm::vec2(-5.0f, 3.5f);
    wall(b0, b1, 3.5f, 5.0f, 1.0f, 2.0f);      // Back wall: window.
    wall(b3, b2, 4.2f, 5.6f, 0.0f, 2.2f);      // Front wall: door toward the yard.
    wall(b0 + glm::vec2(0.0f, 0.12f), b3 - glm::vec2(0.0f, 0.12f), 2.6f, 4.2f, 1.0f, 2.0f); // West: window.
    wall(b1 + glm::vec2(0.0f, 0.12f), b2 - glm::vec2(0.0f, 0.12f), 2.6f, 4.0f, 0.0f, 2.2f); // East: back door.
    map.blocks.push_back(block({bunk.x, yardY + wallH + 0.1f, bunk.y}, {10.6f, 0.2f, 7.6f}, darkWood));
    map.blocks.push_back(block({bunk.x, yardY + 0.05f, bunk.y}, {9.8f, 0.1f, 6.8f}, darkWood));
    map.blocks.push_back(block({bunk.x - 3.6f, yardY + 0.5f, bunk.y - 2.2f}, {2.0f, 1.0f, 0.9f}, darkWood)); // Bunk bed.
    map.blocks.push_back(block({bunk.x + 1.0f, yardY + 0.4f, bunk.y - 0.2f}, {1.4f, 0.8f, 0.9f}, wood));     // Table.
    map.blocks.push_back(block({bunk.x + 4.0f, yardY + 1.3f, bunk.y - 2.9f}, {0.9f, 2.6f, 0.9f}, stone));   // Stove chimney.

    // Watchtower: platform at 4.2 m on four posts, a ramp up the south side, low railings.
    const float towerY = t.heightAt(tower.x, tower.y);
    const float deck = towerY + 4.2f;
    for (int i = 0; i < 4; ++i) {
        const float sx = i % 2 ? 1.6f : -1.6f;
        const float sz = i / 2 ? 1.6f : -1.6f;
        map.blocks.push_back(block({tower.x + sx, towerY + 2.1f, tower.y + sz}, {0.3f, 4.2f, 0.3f}, darkWood));
    }
    map.blocks.push_back(block({tower.x, deck, tower.y}, {3.8f, 0.2f, 3.8f}, wood));
    for (int side = 0; side < 3; ++side) {
        const glm::vec3 offsets[3] = {{0.0f, 0.0f, -1.85f}, {-1.85f, 0.0f, 0.0f}, {1.85f, 0.0f, 0.0f}};
        const glm::vec3 sizes[3] = {{3.8f, 1.0f, 0.12f}, {0.12f, 1.0f, 3.8f}, {0.12f, 1.0f, 3.8f}};
        map.blocks.push_back(block(glm::vec3(tower.x, deck + 0.6f, tower.y) + offsets[side], sizes[side], darkWood));
    }
    map.blocks.push_back(metal({tower.x, deck + 2.3f, tower.y}, {4.2f, 0.08f, 4.2f}, {0.45f, 0.38f, 0.3f}));
    for (int i = 0; i < 4; ++i) {
        const float sx = i % 2 ? 1.6f : -1.6f;
        const float sz = i / 2 ? 1.6f : -1.6f;
        map.blocks.push_back(block({tower.x + sx, deck + 1.2f, tower.y + sz}, {0.15f, 2.2f, 0.15f}, darkWood));
    }
    {
        // Ramp: 8.6 m long at ~29 degrees, from the ground up to the deck's south edge.
        const float length = 8.6f;
        const float angle = std::asin(4.2f / length);
        const float run = std::cos(angle) * length;
        map.blocks.push_back(block({tower.x, towerY + 2.1f, tower.y + 1.9f + run * 0.5f}, {1.3f, 0.15f, length}, wood,
                                   {glm::degrees(angle), 0.0f, 0.0f}));
    }

    // Log piles and lumber stacks: the main cover in the yard.
    auto logPile = [&](glm::vec2 p, float yaw, int rows) {
        const glm::vec2 right(std::cos(glm::radians(yaw)), -std::sin(glm::radians(yaw)));
        for (int row = 0; row < rows; ++row) {
            const int count = 4 - row;
            for (int i = 0; i < count; ++i) {
                const float offset = (static_cast<float>(i) - static_cast<float>(count - 1) * 0.5f) * 0.64f;
                const glm::vec2 c = p + right * offset;
                const float length = 6.6f + static_cast<float>((i * 7 + row * 3) % 5) * 0.2f;
                roundLog({c.x, yardY + 0.31f + static_cast<float>(row) * 0.54f, c.y}, length, 0.62f, yaw,
                         (i + row) % 2 ? wood : darkWood);
            }
        }
    };
    logPile(yard + glm::vec2(-14.0f, -6.0f), 10.0f, 3);
    logPile(yard + glm::vec2(4.0f, 6.0f), 80.0f, 3);
    logPile(yard + glm::vec2(-3.0f, -14.0f), -30.0f, 2);
    auto lumber = [&](glm::vec2 p, float yaw, float height) {
        map.blocks.push_back(block({p.x, yardY + height * 0.5f, p.y}, {2.2f, height, 4.0f}, freshWood, {0.0f, yaw, 0.0f}));
        map.blocks.push_back(block({p.x, yardY + height + 0.06f, p.y}, {2.4f, 0.12f, 4.2f}, darkWood, {0.0f, yaw, 2.0f}));
    };
    lumber(yard + glm::vec2(2.0f, -4.0f), 0.0f, 1.3f);
    lumber(yard + glm::vec2(-8.0f, -12.0f), 90.0f, 1.7f);
    lumber(yard + glm::vec2(12.0f, 4.0f), 20.0f, 1.1f);

    // Logging truck parked on the road: cab, flat bed, wheels and a load of logs.
    {
        const glm::vec2 p = yard + glm::vec2(-18.0f, -16.0f);
        const float yaw = 52.0f;
        const glm::vec2 fwd(std::sin(glm::radians(yaw)), std::cos(glm::radians(yaw)));
        const glm::vec3 paint(0.28f, 0.33f, 0.24f);
        auto at = [&](float along) { return p + fwd * along; };
        map.blocks.push_back(metal({at(3.2f).x, yardY + 1.45f, at(3.2f).y}, {2.3f, 1.7f, 2.0f}, paint, {0.0f, yaw, 0.0f}));
        map.blocks.push_back(metal({at(4.6f).x, yardY + 1.0f, at(4.6f).y}, {2.1f, 0.9f, 1.0f}, paint, {0.0f, yaw, 0.0f}));
        map.blocks.push_back(metal({at(-0.8f).x, yardY + 0.9f, at(-0.8f).y}, {2.4f, 0.25f, 6.0f}, {0.2f, 0.18f, 0.16f}, {0.0f, yaw, 0.0f}));
        for (float along : {-2.6f, -0.4f, 3.6f}) {
            map.blocks.push_back(metal({at(along).x, yardY + 0.45f, at(along).y}, {2.5f, 0.9f, 0.9f}, {0.08f, 0.08f, 0.08f}, {0.0f, yaw, 0.0f}));
        }
        for (int i = 0; i < 3; ++i) {
            const glm::vec2 right(fwd.y, -fwd.x);
            const glm::vec2 c = at(-0.8f) + right * ((static_cast<float>(i) - 1.0f) * 0.7f);
            roundLog({c.x, yardY + 1.38f, c.y}, 6.4f, 0.66f, yaw, i % 2 ? wood : darkWood);
        }
    }

    // Dock on the lake.
    {
        const glm::vec2 shore(16.0f, 14.0f);
        const glm::vec2 dir = glm::normalize(lake - shore);
        const float yaw = glm::degrees(std::atan2(dir.x, dir.y));
        const float deckY = t.waterLevel + 0.55f;
        for (int i = 0; i < 12; ++i) {
            const glm::vec2 c = shore + dir * (static_cast<float>(i) * 0.95f);
            map.blocks.push_back(block({c.x, std::max(deckY, t.heightAt(c.x, c.y) + 0.08f), c.y}, {2.4f, 0.12f, 0.85f},
                                       i % 3 ? wood : darkWood, {0.0f, yaw, i % 2 ? 1.0f : -1.0f}));
        }
        for (int i = 2; i < 12; i += 3) {
            for (int side = -1; side <= 1; side += 2) {
                const glm::vec2 right(dir.y, -dir.x);
                const glm::vec2 c = shore + dir * (static_cast<float>(i) * 0.95f) + right * (1.15f * static_cast<float>(side));
                map.blocks.push_back(block({c.x, deckY - 0.6f, c.y}, {0.2f, 1.6f, 0.2f}, darkWood));
            }
        }
        // A rowboat tied at the end (dynamic: it can be shoved).
        const glm::vec2 boat = shore + dir * 12.5f + glm::vec2(dir.y, -dir.x) * 2.2f;
        BlockData hull = block({boat.x, t.waterLevel + 0.25f, boat.y}, {1.2f, 0.45f, 3.4f}, darkWood, {0.0f, yaw + 8.0f, 0.0f});
        map.blocks.push_back(hull);
    }

    // Fence fragments along the road and scattered dynamic crates and barrels.
    {
        // Split-rail fence beside the road: posts every 3 m, two rails between neighbors, one gap.
        const glm::vec2 from(-58.0f, -40.0f), to = yard + glm::vec2(-22.0f, -9.0f);
        const int posts = static_cast<int>(glm::length(to - from) / 3.0f);
        glm::vec3 previous{0.0f};
        for (int i = 0; i <= posts; ++i) {
            const glm::vec2 a = glm::mix(from, to, static_cast<float>(i) / static_cast<float>(posts));
            const glm::vec3 post(a.x, t.heightAt(a.x, a.y), a.y);
            map.blocks.push_back(block(post + glm::vec3(0.0f, 0.55f, 0.0f), {0.14f, 1.1f, 0.14f}, darkWood));
            const bool gap = i == posts / 2;
            if (i > 0 && !gap) {
                const glm::vec3 mid = (previous + post) * 0.5f;
                const glm::vec3 delta = post - previous;
                const float yaw = glm::degrees(std::atan2(delta.x, delta.z));
                const float pitch = -glm::degrees(std::atan2(delta.y, glm::length(glm::vec2(delta.x, delta.z))));
                for (float h : {0.45f, 0.9f}) {
                    map.blocks.push_back(block(mid + glm::vec3(0.0f, h, 0.0f), {0.08f, 0.1f, glm::length(delta) + 0.1f}, wood,
                                               {pitch, yaw, 0.0f}));
                }
            }
            previous = post;
        }
    }
    const glm::vec2 crates[] = {{-2, 2}, {-1, 3.1f}, {7, -1}, {-12, 3}, {14, -2}, {-6, -9}};
    for (size_t i = 0; i < std::size(crates); ++i) {
        const glm::vec2 c = yard + crates[i];
        BlockData crate = i % 2 ? metal({c.x, yardY + 0.5f, c.y}, {0.7f, 1.0f, 0.7f}, {0.42f, 0.2f, 0.12f})
                                : block({c.x, yardY + 0.4f, c.y}, {0.8f, 0.8f, 0.8f}, wood, {0.0f, static_cast<float>(i) * 17.0f, 0.0f});
        crate.dynamic = true;
        crate.mass = i % 2 ? 30.0f : 25.0f;
        map.blocks.push_back(crate);
    }

    map.lights.push_back({{mill.x + 1.0f, yardY + 3.2f, mill.y}, {1.0f, 0.65f, 0.35f}, 6.0f, 10.0f});
    map.lights.push_back({{bunk.x, yardY + 2.2f, bunk.y}, {1.0f, 0.62f, 0.32f}, 5.0f, 7.0f});
    map.lights.push_back({{tower.x, deck + 1.6f, tower.y}, {1.0f, 0.7f, 0.4f}, 4.0f, 6.0f});

    // Vegetation: dense conifers on the rim, fewer around the yard (stumps where trees were cut).
    addLayer(map, "redwood", 0.02f, 0.8f, 1.05f, 0.25f, true, 0.15f, 31)->clumping = 0.3f;
    addLayer(map, "pine_a", 0.6f, 0.75f, 1.3f, 0.45f, true, 0.3f, 32);
    addLayer(map, "pine_b", 0.5f, 0.75f, 1.25f, 0.45f, true, 0.3f, 33);
    addLayer(map, "fir_a", 0.55f, 0.8f, 1.35f, 0.5f, true, 0.35f, 34)->clumping = 0.6f;
    addLayer(map, "maple", 0.08f, 0.8f, 1.1f, 0.3f, true, 0.4f, 35)->clumping = 0.8f;
    VegetationLayer* stumps = addLayer(map, "stump", 0.35f, 0.8f, 1.4f, 0.4f, true, 0.0f, 36);
    stumps->sink = 0.1f;
    stumps->avoidPaths = 0.9f;
    VegetationLayer* rocks = addLayer(map, "rock_a", 0.3f, 0.6f, 1.5f, 0.9f, true, 0.0f, 37);
    rocks->alignToGround = true;
    rocks->sink = 0.25f;
    rocks->waterMargin = -2.0f;
    VegetationLayer* pebbles = addLayer(map, "rock_b", 0.5f, 0.4f, 1.2f, 0.9f, true, 0.0f, 38);
    pebbles->alignToGround = true;
    pebbles->sink = 0.2f;
    pebbles->waterMargin = -2.5f;
    VegetationLayer* boulders = addLayer(map, "boulder", 0.06f, 0.8f, 1.4f, 0.7f, true, 0.0f, 39);
    boulders->alignToGround = true;
    boulders->sink = 0.6f;
    addLayer(map, "log", 0.05f, 0.8f, 1.2f, 0.25f, true, 0.0f, 40)->alignToGround = true;
    addLayer(map, "bush", 0.7f, 0.7f, 1.3f, 0.5f, false, 0.25f, 41)->clumping = 0.6f;
    addLayer(map, "fern", 9.0f, 0.7f, 1.2f, 0.55f, false, 0.35f, 42)->clumping = 0.75f;
    VegetationLayer* grass = addLayer(map, "grass", 120.0f, 0.6f, 1.1f, 0.5f, false, 0.5f, 43);
    grass->groundCover = true;
    grass->clumping = 0.5f;

    placeSpawns(map, 24.0f, 42.0f, [&](const glm::vec3& p) { return glm::length(glm::vec2(p.x, p.z) - lake) > lakeRadius * 1.35f; });
    return map;
}

// --- Procedural maps -------------------------------------------------------------------------------

// Portable RNG (xorshift32): std:: distributions differ between standard libraries, and a seed must
// give the same map everywhere.
class Rng {
public:
    explicit Rng(uint32_t seed) : m_state(seed * 2654435761u ^ 0x9E3779B9u) {
        if (m_state == 0) m_state = 1;
        for (int i = 0; i < 8; ++i) next();
    }
    uint32_t next() {
        m_state ^= m_state << 13;
        m_state ^= m_state >> 17;
        m_state ^= m_state << 5;
        return m_state;
    }
    float unit() { return static_cast<float>(next() >> 8) / 16777216.0f; }
    float range(float a, float b) { return a + (b - a) * unit(); }
    int pick(int n) { return static_cast<int>(next() % static_cast<uint32_t>(n)); }

private:
    uint32_t m_state;
};

const glm::vec3 kWood(0.44f, 0.3f, 0.18f);
const glm::vec3 kDarkWood(0.3f, 0.2f, 0.12f);
const glm::vec3 kStone(0.45f, 0.43f, 0.4f);

BlockData metalBlock(glm::vec3 position, glm::vec3 size, glm::vec3 color, glm::vec3 rotation = {}) {
    BlockData b = block(position, size, color, rotation, 0.6f);
    b.texture = "metal_plate";
    b.metallic = 0.6f;
    return b;
}

BlockData logBlock(glm::vec3 center, float length, float diameter, float yaw, glm::vec3 color, float pitch = 0.0f) {
    BlockData b = block(center, {diameter, diameter, length}, color, {pitch, yaw, 0.0f});
    b.cylinder = true;
    b.texture = "bark_brown_02";
    return b;
}

// Builds a structure in local coordinates (origin on the ground at its center, facing +Z) and puts
// it in the world: rotated by yaw around Y, lifted to the ground height.
struct Placer {
    MapData& map;
    glm::vec2 center;
    float ground;
    float yaw; // Radians.

    glm::vec3 toWorld(glm::vec3 local) const {
        const float c = std::cos(yaw), s = std::sin(yaw);
        return {center.x + local.x * c + local.z * s, ground + local.y, center.y - local.x * s + local.z * c};
    }
    void add(BlockData b) const {
        b.position = toWorld(b.position);
        b.rotation.y += glm::degrees(yaw);
        map.blocks.push_back(b);
    }
    void light(glm::vec3 local, glm::vec3 color, float intensity, float radius) const {
        map.lights.push_back({toWorld(local), color, intensity, radius});
    }
};

void buildCabin(const Placer& p) {
    // Log cabin: round logs stacked on three walls, a broken fourth wall, stone chimney, fallen roof.
    for (int i = 0; i < 6; ++i) {
        const float y = 0.22f + static_cast<float>(i) * 0.4f;
        const glm::vec3 c = i % 2 ? kWood : kDarkWood;
        p.add(logBlock({0.0f, y, -4.0f}, 9.4f, 0.42f, 90.0f, c));
        p.add(logBlock({-4.5f, y + 0.2f, 0.0f}, 8.6f, 0.42f, 0.0f, c));
        if (i < 4) p.add(logBlock({4.5f, y + 0.2f, 1.8f}, 5.0f, 0.42f, 0.0f, c));
        if (i < 2) p.add(logBlock({-1.0f, y, 4.0f}, 6.8f, 0.42f, 90.0f, c));
    }
    p.add(block({-3.6f, 2.6f, -3.0f}, {1.4f, 5.2f, 1.4f}, kStone));
    p.add(block({-1.0f, 1.6f, -1.2f}, {7.0f, 0.2f, 5.0f}, kDarkWood, {-24.0f, 8.0f, 4.0f}));
    p.add(block({0.0f, 0.05f, 0.0f}, {8.6f, 0.1f, 7.6f}, kDarkWood));
    p.light({-2.0f, 1.2f, -2.0f}, {1.0f, 0.7f, 0.4f}, 4.0f, 7.0f);
}

void buildCamp(const Placer& p) {
    p.add(block({-3.0f, 0.5f, 2.0f}, {1.0f, 1.0f, 1.0f}, kWood));
    p.add(block({-1.8f, 0.4f, 2.4f}, {0.8f, 0.8f, 0.8f}, kDarkWood, {0.0f, 25.0f, 0.0f}));
    p.add(block({2.0f, 1.1f, -1.0f}, {4.0f, 0.08f, 3.0f}, {0.35f, 0.38f, 0.24f}, {35.0f, 10.0f, 0.0f}, 0.95f));
    p.add(block({2.0f, 1.1f, -3.4f}, {4.0f, 0.08f, 3.0f}, {0.35f, 0.38f, 0.24f}, {-35.0f, 10.0f, 0.0f}, 0.95f));
    for (int i = 0; i < 4; ++i) p.add(logBlock({-0.2f, 0.12f, 0.4f}, 1.0f, 0.16f, static_cast<float>(i) * 45.0f, kDarkWood));
    for (int i = 0; i < 3; ++i) {
        BlockData crate = block({-5.0f + static_cast<float>(i) * 1.1f, 0.4f, -4.0f}, {0.8f, 0.8f, 0.8f}, kWood);
        crate.dynamic = true;
        crate.mass = 25.0f;
        p.add(crate);
    }
    p.light({-0.2f, 0.6f, 0.4f}, {1.0f, 0.55f, 0.25f}, 8.0f, 9.0f);
}

void buildWatchtower(const Placer& p) {
    const float deck = 4.2f;
    for (int i = 0; i < 4; ++i) {
        const float sx = i % 2 ? 1.6f : -1.6f, sz = i / 2 ? 1.6f : -1.6f;
        p.add(logBlock({sx, deck * 0.5f, sz}, deck, 0.3f, 0.0f, kDarkWood, 90.0f));
        p.add(block({sx, deck + 1.2f, sz}, {0.15f, 2.2f, 0.15f}, kDarkWood));
    }
    p.add(block({0.0f, deck, 0.0f}, {3.8f, 0.2f, 3.8f}, kWood));
    p.add(block({0.0f, deck + 0.6f, -1.85f}, {3.8f, 1.0f, 0.12f}, kDarkWood));
    p.add(block({-1.85f, deck + 0.6f, 0.0f}, {0.12f, 1.0f, 3.8f}, kDarkWood));
    p.add(block({1.85f, deck + 0.6f, 0.0f}, {0.12f, 1.0f, 3.8f}, kDarkWood));
    p.add(metalBlock({0.0f, deck + 2.3f, 0.0f}, {4.2f, 0.08f, 4.2f}, {0.45f, 0.38f, 0.3f}));
    const float length = 8.6f;
    const float angle = std::asin(deck / length);
    p.add(block({0.0f, deck * 0.5f, 1.9f + std::cos(angle) * length * 0.5f}, {1.3f, 0.15f, length}, kWood,
                {glm::degrees(angle), 0.0f, 0.0f}));
    p.light({0.0f, deck + 1.6f, 0.0f}, {1.0f, 0.7f, 0.4f}, 4.0f, 6.0f);
}

void buildMillShed(const Placer& p) {
    for (int i = 0; i < 4; ++i) {
        for (int side = -1; side <= 1; side += 2) {
            p.add(logBlock({-7.5f + static_cast<float>(i) * 5.0f, 1.8f, 3.2f * static_cast<float>(side)}, 3.6f, 0.3f, 0.0f,
                           kDarkWood, 90.0f));
        }
    }
    p.add(metalBlock({0.0f, 3.85f, -1.7f}, {16.5f, 0.08f, 3.8f}, {0.5f, 0.42f, 0.36f}, {-14.0f, 0.0f, 0.0f}));
    p.add(metalBlock({0.0f, 3.85f, 1.7f}, {16.5f, 0.08f, 3.8f}, {0.5f, 0.42f, 0.36f}, {14.0f, 0.0f, 0.0f}));
    p.add(block({0.0f, 0.45f, 0.0f}, {11.0f, 0.9f, 1.4f}, kDarkWood));
    p.add(logBlock({-1.0f, 1.25f, 0.0f}, 8.0f, 0.75f, 90.0f, kWood));
    p.add(metalBlock({6.0f, 0.8f, -1.8f}, {2.0f, 1.6f, 1.4f}, {0.35f, 0.3f, 0.26f}));
    p.light({1.0f, 3.2f, 0.0f}, {1.0f, 0.65f, 0.35f}, 6.0f, 10.0f);
}

void buildBunkhouse(const Placer& p) {
    const float wallH = 2.8f;
    auto wall = [&](glm::vec2 a, glm::vec2 b, float gapStart, float gapEnd, float gapBottom, float gapTop) {
        const glm::vec2 dir = glm::normalize(b - a);
        const float length = glm::length(b - a);
        const bool alongX = std::abs(dir.x) > 0.5f;
        auto piece = [&](float s0, float s1, float y0, float y1) {
            if (s1 - s0 < 0.05f || y1 - y0 < 0.05f) return;
            const glm::vec2 c = a + dir * ((s0 + s1) * 0.5f);
            const glm::vec3 size = alongX ? glm::vec3(s1 - s0, y1 - y0, 0.25f) : glm::vec3(0.25f, y1 - y0, s1 - s0);
            p.add(block({c.x, (y0 + y1) * 0.5f, c.y}, size, kWood));
        };
        piece(0.0f, gapStart, 0.0f, wallH);
        piece(gapEnd, length, 0.0f, wallH);
        piece(gapStart, gapEnd, 0.0f, gapBottom);
        piece(gapStart, gapEnd, gapTop, wallH);
    };
    wall({-5.0f, -3.5f}, {5.0f, -3.5f}, 3.5f, 5.0f, 1.0f, 2.0f);
    wall({-5.0f, 3.5f}, {5.0f, 3.5f}, 4.2f, 5.6f, 0.0f, 2.2f);
    wall({-5.0f, -3.38f}, {-5.0f, 3.38f}, 2.6f, 4.2f, 1.0f, 2.0f);
    wall({5.0f, -3.38f}, {5.0f, 3.38f}, 2.6f, 4.0f, 0.0f, 2.2f);
    p.add(block({0.0f, wallH + 0.1f, 0.0f}, {10.6f, 0.2f, 7.6f}, kDarkWood));
    p.add(block({0.0f, 0.05f, 0.0f}, {9.8f, 0.1f, 6.8f}, kDarkWood));
    p.add(block({-3.6f, 0.5f, -2.2f}, {2.0f, 1.0f, 0.9f}, kDarkWood));
    p.add(block({1.0f, 0.4f, -0.2f}, {1.4f, 0.8f, 0.9f}, kWood));
    p.light({0.0f, 2.2f, 0.0f}, {1.0f, 0.62f, 0.32f}, 5.0f, 7.0f);
}

void buildLogYard(const Placer& p, Rng& rng) {
    for (int pile = 0; pile < 2; ++pile) {
        const glm::vec2 base(pile == 0 ? -3.0f : 3.5f, pile == 0 ? -2.0f : 2.5f);
        const float yaw = rng.range(-20.0f, 20.0f) + (pile == 0 ? 0.0f : 90.0f);
        const glm::vec2 right(std::cos(glm::radians(yaw)), -std::sin(glm::radians(yaw)));
        const int rows = 2 + rng.pick(2);
        for (int row = 0; row < rows; ++row) {
            const int count = 4 - row;
            for (int i = 0; i < count; ++i) {
                const glm::vec2 c = base + right * ((static_cast<float>(i) - static_cast<float>(count - 1) * 0.5f) * 0.64f);
                p.add(logBlock({c.x, 0.31f + static_cast<float>(row) * 0.54f, c.y}, rng.range(6.2f, 7.4f), 0.62f, yaw,
                               (i + row) % 2 ? kWood : kDarkWood));
            }
        }
    }
    p.add(block({0.0f, 0.65f, 5.0f}, {2.2f, 1.3f, 4.0f}, {0.62f, 0.46f, 0.3f}));
}

void buildTruck(const Placer& p) {
    const glm::vec3 paint(0.28f, 0.33f, 0.24f);
    p.add(metalBlock({0.0f, 1.45f, 3.2f}, {2.3f, 1.7f, 2.0f}, paint));
    p.add(metalBlock({0.0f, 1.0f, 4.6f}, {2.1f, 0.9f, 1.0f}, paint));
    p.add(metalBlock({0.0f, 0.9f, -0.8f}, {2.4f, 0.25f, 6.0f}, {0.2f, 0.18f, 0.16f}));
    for (float along : {-2.6f, -0.4f, 3.6f}) {
        p.add(logBlock({-1.15f, 0.45f, along}, 0.4f, 0.9f, 90.0f, {0.08f, 0.08f, 0.08f}));
        p.add(logBlock({1.15f, 0.45f, along}, 0.4f, 0.9f, 90.0f, {0.08f, 0.08f, 0.08f}));
    }
    for (int i = 0; i < 3; ++i) p.add(logBlock({(static_cast<float>(i) - 1.0f) * 0.7f, 1.38f, -0.8f}, 6.4f, 0.66f, 0.0f, i % 2 ? kWood : kDarkWood));
}

void buildRuins(const Placer& p, Rng& rng) {
    // Broken stone walls of an old farmhouse: low cover with gaps.
    BlockData b;
    for (int side = 0; side < 4; ++side) {
        const float a = static_cast<float>(side) * glm::half_pi<float>();
        const glm::vec2 dir(std::cos(a), std::sin(a));
        const glm::vec2 along(-dir.y, dir.x);
        for (int k = -2; k <= 2; ++k) {
            if (rng.unit() < 0.3f) continue;
            const float h = rng.range(0.6f, 2.4f);
            const glm::vec2 c = dir * 4.0f + along * (static_cast<float>(k) * 1.6f);
            b = block({c.x, h * 0.5f, c.y}, {1.6f, h, 0.5f}, kStone, {0.0f, -glm::degrees(a) - 90.0f, 0.0f});
            b.texture = "mossy_rock";
            p.add(b);
        }
    }
}

struct Water {
    int kind = 0; // 0 none, 1 creek, 2 lake
    // Creek: centerline n = offset(s) in a frame rotated by angle.
    float angle = 0.0f, a1 = 0.0f, f1 = 0.0f, p1 = 0.0f, a2 = 0.0f, f2 = 0.0f, p2 = 0.0f, shift = 0.0f;
    glm::vec2 lake{0.0f};
    float lakeRadius = 0.0f;

    glm::vec2 axis() const { return {std::cos(angle), std::sin(angle)}; }
    glm::vec2 normal() const { return {-std::sin(angle), std::cos(angle)}; }
    float creekOffset(float s) const { return a1 * std::sin(s * f1 + p1) + a2 * std::sin(s * f2 + p2) + shift; }
    float creekDistance(glm::vec2 p) const {
        const float s = glm::dot(p, axis());
        return std::abs(glm::dot(p, normal()) - creekOffset(s));
    }
    glm::vec2 creekPoint(float s) const { return axis() * s + normal() * creekOffset(s); }
    bool wet(glm::vec2 p, float margin) const {
        if (kind == 1) return creekDistance(p) < 7.0f + margin;
        if (kind == 2) return glm::length(p - lake) < lakeRadius * 1.15f + margin;
        return false;
    }
};

MapData makeProcedural(uint32_t seed) {
    Rng rng(seed);
    MapData map;
    map.name = "Procedural #" + std::to_string(seed);

    // Light and weather: morning, late afternoon or overcast.
    EnvironmentData& e = map.environment;
    switch (rng.pick(3)) {
    case 0:
        e.sunDirection = {-0.55f, 0.55f, 0.35f}; e.sunColor = {1.0f, 0.87f, 0.68f}; e.sunIntensity = 3.1f;
        e.skyColor = {0.52f, 0.62f, 0.72f}; e.groundColor = {0.2f, 0.24f, 0.12f}; e.ambientIntensity = 0.42f;
        e.fogColor = {0.55f, 0.63f, 0.66f}; e.fogDensity = 0.0055f;
        break;
    case 1:
        e.sunDirection = {0.72f, 0.3f, -0.45f}; e.sunColor = {1.0f, 0.74f, 0.48f}; e.sunIntensity = 2.9f;
        e.skyColor = {0.6f, 0.62f, 0.68f}; e.groundColor = {0.24f, 0.22f, 0.14f}; e.ambientIntensity = 0.38f;
        e.fogColor = {0.72f, 0.64f, 0.56f}; e.fogDensity = 0.0035f;
        break;
    default:
        e.sunDirection = {0.25f, 0.8f, 0.3f}; e.sunColor = {0.92f, 0.94f, 0.98f}; e.sunIntensity = 1.7f;
        e.skyColor = {0.56f, 0.6f, 0.65f}; e.groundColor = {0.22f, 0.24f, 0.18f}; e.ambientIntensity = 0.6f;
        e.fogColor = {0.6f, 0.64f, 0.66f}; e.fogDensity = 0.0075f;
        break;
    }

    TerrainData& t = map.terrain;
    t.enabled = true;
    t.size = std::array<float, 3>{140.0f, 160.0f, 180.0f}[static_cast<size_t>(rng.pick(3))];
    t.resolution = 256;
    t.seed = rng.next() % 100000u;
    t.amplitude = rng.range(5.0f, 11.0f);
    t.frequency = rng.range(0.014f, 0.024f);
    t.octaves = 5;
    t.generate();
    const float half = t.size * 0.5f;

    // Rim: hills toward the border keep the action inside.
    const float rim = rng.range(0.0f, 9.0f);
    Water water;
    water.kind = rng.pick(3);
    if (water.kind == 1) {
        water.angle = rng.range(0.0f, glm::pi<float>());
        water.a1 = rng.range(6.0f, 13.0f); water.f1 = rng.range(0.03f, 0.05f); water.p1 = rng.range(0.0f, 6.28f);
        water.a2 = rng.range(2.0f, 6.0f); water.f2 = rng.range(0.08f, 0.13f); water.p2 = rng.range(0.0f, 6.28f);
        water.shift = rng.range(-12.0f, 12.0f);
    } else if (water.kind == 2) {
        const float a = rng.range(0.0f, glm::two_pi<float>());
        water.lake = glm::vec2(std::cos(a), std::sin(a)) * t.size * rng.range(0.15f, 0.25f);
        water.lakeRadius = rng.range(15.0f, 22.0f);
    }
    {
        const glm::vec2 o = t.origin();
        const float cell = t.cellSize();
        for (int z = 0; z < t.resolution; ++z) {
            for (int x = 0; x < t.resolution; ++x) {
                const glm::vec2 p(o.x + static_cast<float>(x) * cell, o.y + static_cast<float>(z) * cell);
                float& h = t.heights[static_cast<size_t>(z * t.resolution + x)];
                h += glm::smoothstep(half * 0.62f, half * 0.95f, std::max(std::abs(p.x), std::abs(p.y))) * rim;
                if (water.kind == 1) {
                    const float d = water.creekDistance(p);
                    const float valley = std::exp(-d * d / (2.0f * 30.0f * 30.0f));
                    h = h * (1.0f - 0.6f * valley) - valley * 1.5f;
                    if (d < 5.5f) {
                        const float u = d / 5.5f;
                        h = std::min(h, -1.6f * (1.0f - u * u) + u * u * h);
                    }
                } else if (water.kind == 2) {
                    const float angle = std::atan2(p.y - water.lake.y, p.x - water.lake.x);
                    const float radius = water.lakeRadius * (1.0f + 0.18f * std::sin(angle * 3.0f + 0.7f) + 0.08f * std::sin(angle * 7.0f));
                    const float d = glm::length(p - water.lake) / radius;
                    if (d < 1.6f) {
                        const float bowl = 1.0f - glm::smoothstep(0.0f, 1.6f, d);
                        h = glm::mix(h, -3.2f, bowl * bowl * (3.0f - 2.0f * bowl));
                    }
                }
            }
        }
    }
    t.waterEnabled = water.kind != 0;
    t.waterLevel = water.kind == 1 ? -1.15f : -0.9f;

    // Structures: 4-6 from the catalog, each on the flattest dry site found, apart from the others.
    enum Kind { Cabin, Camp, Tower, Mill, Bunk, LogYard, Truck, Ruins, KindCount };
    const float radii[KindCount] = {7.0f, 6.0f, 7.0f, 10.0f, 7.5f, 7.0f, 5.0f, 6.0f};
    std::vector<int> catalog = {Cabin, Camp, Tower, Mill, Bunk, LogYard, Truck, Ruins};
    for (size_t i = catalog.size(); i > 1; --i) std::swap(catalog[i - 1], catalog[static_cast<size_t>(rng.pick(static_cast<int>(i)))]);
    const int count = 4 + rng.pick(3);
    struct Site {
        glm::vec2 center;
        float radius;
        int kind;
    };
    std::vector<Site> sites;
    for (int k = 0; k < count && k < static_cast<int>(catalog.size()); ++k) {
        const int kind = catalog[static_cast<size_t>(k)];
        const float radius = radii[kind];
        float bestScore = 1e9f;
        glm::vec2 best(0.0f);
        for (int attempt = 0; attempt < 60; ++attempt) {
            const glm::vec2 c(rng.range(-half + 24.0f, half - 24.0f), rng.range(-half + 24.0f, half - 24.0f));
            if (water.wet(c, radius + 6.0f)) continue;
            bool clear = true;
            for (const Site& s : sites) clear = clear && glm::length(s.center - c) > s.radius + radius + 14.0f;
            if (!clear) continue;
            // Flat sites with little height difference are cheaper to level and read better.
            float lo = 1e9f, hi = -1e9f;
            for (int j = 0; j < 12; ++j) {
                const float a = glm::two_pi<float>() * static_cast<float>(j) / 12.0f;
                const float h = t.heightAt(c.x + std::cos(a) * radius, c.y + std::sin(a) * radius);
                lo = std::min(lo, h);
                hi = std::max(hi, h);
            }
            const float score = (hi - lo) + glm::length(c) * 0.01f;
            if (score < bestScore) {
                bestScore = score;
                best = c;
            }
        }
        if (bestScore < 1e8f) sites.push_back({best, radius, kind});
    }
    for (const Site& s : sites) {
        flattenArea(t, s.center, s.radius + 4.0f, t.heightAt(s.center.x, s.center.y));
        paintDisc(t, s.center, s.radius + 1.5f);
    }

    // Trails between neighbouring sites (around the map), and a bridge where a trail meets the creek.
    std::vector<Site> ring = sites;
    std::sort(ring.begin(), ring.end(), [](const Site& a, const Site& b) {
        return std::atan2(a.center.y, a.center.x) < std::atan2(b.center.y, b.center.x);
    });
    for (size_t i = 0; i < ring.size(); ++i) {
        paintPath(t, ring[i].center, ring[(i + 1) % ring.size()].center, 2.0f);
    }
    if (water.kind == 1) {
        const float s = rng.range(-10.0f, 10.0f);
        const glm::vec2 crossing = water.creekPoint(s);
        const glm::vec2 across = water.normal();
        const float bankA = t.heightAt(crossing.x + across.x * 7.0f, crossing.y + across.y * 7.0f);
        const float bankB = t.heightAt(crossing.x - across.x * 7.0f, crossing.y - across.y * 7.0f);
        const float deckY = std::max({bankA, bankB, t.waterLevel + 0.6f}) + 0.25f;
        const Placer bridge{map, crossing, deckY, std::atan2(across.x, across.y)};
        for (int i = 0; i < 14; ++i) {
            bridge.add(block({0.0f, 0.0f, -6.5f + static_cast<float>(i)}, {3.0f, 0.12f, 0.9f}, i % 3 ? kWood : kDarkWood,
                             {0.0f, i % 2 ? 1.5f : -1.0f, 0.0f}));
        }
        for (int side = -1; side <= 1; side += 2) {
            bridge.add(block({1.45f * static_cast<float>(side), 0.55f, 0.0f}, {0.12f, 0.12f, 14.0f}, kDarkWood));
            for (int i = 0; i < 4; ++i) {
                bridge.add(logBlock({1.45f * static_cast<float>(side), -0.6f, -6.0f + static_cast<float>(i) * 4.0f}, 2.4f, 0.22f,
                                    0.0f, kDarkWood, 90.0f));
            }
        }
        paintPath(t, crossing + across * 8.0f, crossing + across * 30.0f, 1.8f);
        paintPath(t, crossing - across * 8.0f, crossing - across * 30.0f, 1.8f);
    } else if (water.kind == 2) {
        // Dock from the shore facing the map center.
        const glm::vec2 toCenter = glm::length(water.lake) > 1.0f ? -glm::normalize(water.lake) : glm::vec2(1.0f, 0.0f);
        const glm::vec2 shore = water.lake + toCenter * water.lakeRadius * 0.95f;
        const glm::vec2 dir = -toCenter;
        const float deckY = t.waterLevel + 0.55f;
        const Placer dock{map, shore, 0.0f, std::atan2(dir.x, dir.y)};
        for (int i = 0; i < 12; ++i) {
            const glm::vec2 c = shore + dir * (static_cast<float>(i) * 0.95f);
            dock.add(block({0.0f, std::max(deckY, t.heightAt(c.x, c.y) + 0.08f), static_cast<float>(i) * 0.95f}, {2.4f, 0.12f, 0.85f},
                           i % 3 ? kWood : kDarkWood, {0.0f, 0.0f, i % 2 ? 1.0f : -1.0f}));
        }
        for (int i = 2; i < 12; i += 3) {
            for (int side = -1; side <= 1; side += 2) {
                dock.add(logBlock({1.15f * static_cast<float>(side), deckY - 0.6f, static_cast<float>(i) * 0.95f}, 1.6f, 0.2f, 0.0f,
                                  kDarkWood, 90.0f));
            }
        }
        paintPath(t, shore - dir * 2.0f, shore - dir * 20.0f, 1.8f);
    }

    for (const Site& s : sites) {
        const Placer p{map, s.center, t.heightAt(s.center.x, s.center.y), rng.range(0.0f, glm::two_pi<float>())};
        switch (s.kind) {
        case Cabin: buildCabin(p); break;
        case Camp: buildCamp(p); break;
        case Tower: buildWatchtower(p); break;
        case Mill: buildMillShed(p); break;
        case Bunk: buildBunkhouse(p); break;
        case LogYard: buildLogYard(p, rng); break;
        case Truck: buildTruck(p); break;
        default: buildRuins(p, rng); break;
        }
    }

    // Fallen logs as cover between the sites.
    for (int i = 0, placed = 0; i < 80 && placed < 10; ++i) {
        const glm::vec2 c(rng.range(-half + 15.0f, half - 15.0f), rng.range(-half + 15.0f, half - 15.0f));
        if (water.wet(c, 3.0f) || t.normalAt(c.x, c.y).y < 0.93f) continue;
        bool clear = true;
        for (const Site& s : sites) clear = clear && glm::length(s.center - c) > s.radius + 4.0f;
        if (!clear) continue;
        const float length = rng.range(5.0f, 9.0f);
        map.blocks.push_back(logBlock({c.x, t.heightAt(c.x, c.y) + 0.33f, c.y}, length, rng.range(0.55f, 0.75f),
                                      rng.range(0.0f, 180.0f), {0.3f, 0.22f, 0.15f}));
        ++placed;
    }

    // Vegetation: the forest mix, denser or sparser per seed.
    const float density = rng.range(0.6f, 1.3f);
    addLayer(map, "redwood", 0.04f * density, 0.85f, 1.15f, 0.25f, true, 0.15f, seed * 31u + 1)->clumping = 0.25f;
    addLayer(map, "pine_a", 0.45f * density, 0.75f, 1.25f, 0.4f, true, 0.3f, seed * 31u + 2);
    addLayer(map, "pine_b", 0.45f * density, 0.75f, 1.2f, 0.4f, true, 0.3f, seed * 31u + 3);
    addLayer(map, "fir_a", 0.4f * density, 0.8f, 1.3f, 0.45f, true, 0.35f, seed * 31u + 4)->clumping = 0.5f;
    addLayer(map, "maple", 0.14f * density, 0.8f, 1.2f, 0.3f, true, 0.4f, seed * 31u + 5)->clumping = 0.7f;
    VegetationLayer* rocks = addLayer(map, "rock_a", 0.3f, 0.6f, 1.5f, 0.9f, true, 0.0f, seed * 31u + 6);
    rocks->alignToGround = true;
    rocks->sink = 0.25f;
    rocks->waterMargin = -2.0f;
    VegetationLayer* pebbles = addLayer(map, "rock_b", 0.5f, 0.4f, 1.2f, 0.9f, true, 0.0f, seed * 31u + 7);
    pebbles->alignToGround = true;
    pebbles->sink = 0.2f;
    pebbles->waterMargin = -2.0f;
    VegetationLayer* boulders = addLayer(map, "boulder", 0.05f, 0.8f, 1.3f, 0.6f, true, 0.0f, seed * 31u + 8);
    boulders->alignToGround = true;
    boulders->sink = 0.6f;
    addLayer(map, "log", 0.04f, 0.8f, 1.2f, 0.25f, true, 0.0f, seed * 31u + 9)->alignToGround = true;
    addLayer(map, "stump", 0.1f, 0.8f, 1.3f, 0.35f, true, 0.0f, seed * 31u + 10)->sink = 0.1f;
    addLayer(map, "bush", 0.8f, 0.7f, 1.3f, 0.45f, false, 0.25f, seed * 31u + 11)->clumping = 0.6f;
    addLayer(map, "fern", 10.0f, 0.7f, 1.3f, 0.5f, false, 0.35f, seed * 31u + 12)->clumping = 0.7f;
    VegetationLayer* grass = addLayer(map, "grass", 130.0f, 0.6f, 1.1f, 0.45f, false, 0.5f, seed * 31u + 13);
    grass->groundCover = true;
    grass->clumping = 0.5f;

    placeSpawns(map, half * 0.35f, half * 0.6f, [&](const glm::vec3& p) { return !water.wet({p.x, p.z}, 2.0f); });
    return map;
}

} // namespace

MapData generateForestMap() { return makeForest(); }
MapData generateSawmillMap() { return makeSawmill(); }
MapData generateProceduralMap(uint32_t seed) { return makeProcedural(seed); }

} // namespace origon
