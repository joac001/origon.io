#include "world/Terrain.h"

#include "render/Mesh.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace origon {
namespace {

float hash2(int x, int z, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(z) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

float valueNoise(float x, float z, uint32_t seed) {
    const int ix = static_cast<int>(std::floor(x));
    const int iz = static_cast<int>(std::floor(z));
    const float fx = smooth(x - static_cast<float>(ix));
    const float fz = smooth(z - static_cast<float>(iz));
    const float a = hash2(ix, iz, seed);
    const float b = hash2(ix + 1, iz, seed);
    const float c = hash2(ix, iz + 1, seed);
    const float d = hash2(ix + 1, iz + 1, seed);
    return (a + (b - a) * fx + (c - a) * fz + (a - b - c + d) * fx * fz) * 2.0f - 1.0f;
}

float fbm(float x, float z, uint32_t seed, int octaves) {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        sum += valueNoise(x * frequency, z * frequency, seed + static_cast<uint32_t>(o) * 101u) * amplitude;
        norm += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.03f;
    }
    return sum / norm;
}

} // namespace

void TerrainData::ensureStorage() {
    resolution = std::clamp(resolution / 2 * 2, 16, 1024);
    const size_t count = static_cast<size_t>(resolution) * static_cast<size_t>(resolution);
    if (heights.size() != count) heights.assign(count, 0.0f);
    if (paint.size() != count) paint.assign(count, 0);
}

void TerrainData::generate() {
    ensureStorage();
    const float cell = cellSize();
    const glm::vec2 o = origin();
    for (int z = 0; z < resolution; ++z) {
        for (int x = 0; x < resolution; ++x) {
            const float wx = o.x + static_cast<float>(x) * cell;
            const float wz = o.y + static_cast<float>(z) * cell;
            // Rolling hills plus ridged detail.
            const float base = fbm(wx * frequency, wz * frequency, seed, octaves);
            const float ridge = 1.0f - std::abs(fbm(wx * frequency * 2.1f, wz * frequency * 2.1f, seed + 7u, 3));
            heights[static_cast<size_t>(z * resolution + x)] = (base * 0.8f + ridge * ridge * 0.35f) * amplitude;
        }
    }
}

float TerrainData::sample(int ix, int iz) const {
    ix = std::clamp(ix, 0, resolution - 1);
    iz = std::clamp(iz, 0, resolution - 1);
    return heights[static_cast<size_t>(iz * resolution + ix)];
}

float TerrainData::heightAt(float x, float z) const {
    if (!valid()) return 0.0f;
    const glm::vec2 o = origin();
    const float cell = cellSize();
    const float gx = (x - o.x) / cell;
    const float gz = (z - o.y) / cell;
    const int ix = static_cast<int>(std::floor(gx));
    const int iz = static_cast<int>(std::floor(gz));
    const float fx = gx - static_cast<float>(ix);
    const float fz = gz - static_cast<float>(iz);
    // Same diagonal (00-11) as the mesh and the Jolt heightfield.
    const float h00 = sample(ix, iz), h10 = sample(ix + 1, iz), h01 = sample(ix, iz + 1), h11 = sample(ix + 1, iz + 1);
    if (fx > fz) {
        return h00 + (h10 - h00) * fx + (h11 - h10) * fz;
    }
    return h00 + (h01 - h00) * fz + (h11 - h01) * fx;
}

float TerrainData::paintAt(float x, float z) const {
    if (!valid() || paint.size() != heights.size()) return 0.0f;
    const glm::vec2 o = origin();
    const int ix = std::clamp(static_cast<int>(std::round((x - o.x) / cellSize())), 0, resolution - 1);
    const int iz = std::clamp(static_cast<int>(std::round((z - o.y) / cellSize())), 0, resolution - 1);
    return static_cast<float>(paint[static_cast<size_t>(iz * resolution + ix)]) / 255.0f;
}

glm::vec3 TerrainData::normalAt(float x, float z) const {
    const float e = cellSize();
    const float hl = heightAt(x - e, z), hr = heightAt(x + e, z);
    const float hd = heightAt(x, z - e), hu = heightAt(x, z + e);
    return glm::normalize(glm::vec3(hl - hr, 2.0f * e, hd - hu));
}

bool TerrainData::applyBrush(Brush brush, const glm::vec3& center, float radius, float strength, float dt, float flattenHeight) {
    if (!valid()) return false;
    const glm::vec2 o = origin();
    const float cell = cellSize();
    const int minX = std::max(0, static_cast<int>(std::floor((center.x - radius - o.x) / cell)));
    const int maxX = std::min(resolution - 1, static_cast<int>(std::ceil((center.x + radius - o.x) / cell)));
    const int minZ = std::max(0, static_cast<int>(std::floor((center.z - radius - o.y) / cell)));
    const int maxZ = std::min(resolution - 1, static_cast<int>(std::ceil((center.z + radius - o.y) / cell)));
    if (minX > maxX || minZ > maxZ) return false;

    const std::vector<float> source = brush == Brush::Smooth ? heights : std::vector<float>();
    for (int z = minZ; z <= maxZ; ++z) {
        for (int x = minX; x <= maxX; ++x) {
            const float wx = o.x + static_cast<float>(x) * cell;
            const float wz = o.y + static_cast<float>(z) * cell;
            const float d = glm::length(glm::vec2(wx - center.x, wz - center.z)) / radius;
            if (d >= 1.0f) continue;
            const float falloff = smooth(1.0f - d);
            const float amount = strength * falloff * dt;
            const size_t i = static_cast<size_t>(z * resolution + x);
            switch (brush) {
            case Brush::Raise: heights[i] += amount * 4.0f; break;
            case Brush::Lower: heights[i] -= amount * 4.0f; break;
            case Brush::Flatten: heights[i] += (flattenHeight - heights[i]) * std::min(1.0f, amount * 3.0f); break;
            case Brush::Smooth: {
                float sum = 0.0f;
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int sx = std::clamp(x + dx, 0, resolution - 1);
                        const int sz = std::clamp(z + dz, 0, resolution - 1);
                        sum += source[static_cast<size_t>(sz * resolution + sx)];
                    }
                }
                heights[i] += (sum / 9.0f - heights[i]) * std::min(1.0f, amount * 4.0f);
                break;
            }
            case Brush::Paint:
                paint[i] = static_cast<uint8_t>(std::min(255.0f, static_cast<float>(paint[i]) + amount * 600.0f));
                break;
            case Brush::Erase:
                paint[i] = static_cast<uint8_t>(std::max(0.0f, static_cast<float>(paint[i]) - amount * 600.0f));
                break;
            }
        }
    }
    return true;
}

void TerrainData::buildVertices(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) const {
    vertices.clear();
    indices.clear();
    if (!valid()) return;
    const glm::vec2 o = origin();
    const float cell = cellSize();
    vertices.reserve(heights.size());
    for (int z = 0; z < resolution; ++z) {
        for (int x = 0; x < resolution; ++x) {
            const float h = sample(x, z);
            const glm::vec3 normal = glm::normalize(glm::vec3(sample(x - 1, z) - sample(x + 1, z), 2.0f * cell,
                                                              sample(x, z - 1) - sample(x, z + 1)));
            const float p = static_cast<float>(paint[static_cast<size_t>(z * resolution + x)]) / 255.0f;
            vertices.push_back({{o.x + static_cast<float>(x) * cell, h, o.y + static_cast<float>(z) * cell}, normal, {p, 0.0f}});
        }
    }
    indices.reserve(static_cast<size_t>((resolution - 1) * (resolution - 1) * 6));
    for (int z = 0; z < resolution - 1; ++z) {
        for (int x = 0; x < resolution - 1; ++x) {
            const auto i00 = static_cast<uint32_t>(z * resolution + x);
            const auto i10 = i00 + 1;
            const auto i01 = i00 + static_cast<uint32_t>(resolution);
            const auto i11 = i01 + 1;
            // Split along i00-i11 (same diagonal as heightAt), counter-clockwise seen from above.
            indices.insert(indices.end(), {i00, i01, i11, i00, i11, i10});
        }
    }
}

} // namespace origon
