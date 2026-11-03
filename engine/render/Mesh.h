#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace origon {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

// Static GPU mesh (immutable buffers, DSA vertex array).
class Mesh {
public:
    Mesh() = default;
    Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    ~Mesh();
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    void draw() const;
    void drawInstanced(int count) const;
    int indexCount() const { return m_indexCount; }
    bool valid() const { return m_vao != 0; }

    // Unit cube centered at the origin (size 1 on each axis).
    static Mesh makeCube();
    // Capsule along +Y, centered at the origin.
    static Mesh makeCapsule(float radius, float cylinderHeight, int segments = 16, int rings = 6);

private:
    void release();

    unsigned m_vao = 0;
    unsigned m_vbo = 0;
    unsigned m_ibo = 0;
    int m_indexCount = 0;
};

} // namespace origon
