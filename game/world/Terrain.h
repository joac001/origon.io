#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace origon {

struct Vertex;

// Square heightfield centered at the origin. Heights and the paint mask are map data, so
// collision and visibility are identical for every player.
struct TerrainData {
    bool enabled = false;
    float size = 200.0f;  // Meters per side.
    int resolution = 256; // Samples per side (multiple of 2 for Jolt).
    // Generator parameters (used by "generate").
    uint32_t seed = 1;
    float amplitude = 10.0f;
    float frequency = 0.012f;
    int octaves = 5;
    // Water plane (rendered when waterEnabled).
    bool waterEnabled = false;
    float waterLevel = 0.0f;

    std::vector<float> heights;  // resolution * resolution, row-major (z rows, x columns).
    std::vector<uint8_t> paint;  // Dirt/path mask 0..255, same layout.

    float cellSize() const { return size / static_cast<float>(resolution - 1); }
    glm::vec2 origin() const { return {-size * 0.5f, -size * 0.5f}; }
    bool valid() const {
        return enabled && resolution >= 2 && heights.size() == static_cast<size_t>(resolution * resolution);
    }

    void generate();
    void ensureStorage();
    float heightAt(float x, float z) const;
    glm::vec3 normalAt(float x, float z) const;
    float sample(int ix, int iz) const;

    enum class Brush { Raise, Lower, Smooth, Flatten, Paint, Erase };
    // Applies a brush stroke. Returns true if anything changed.
    bool applyBrush(Brush brush, const glm::vec3& center, float radius, float strength, float dt, float flattenHeight);

    void buildVertices(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) const;
};

} // namespace origon
