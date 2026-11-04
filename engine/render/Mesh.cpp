#include "render/Mesh.h"

#include <glad/gl.h>
#include <glm/gtc/constants.hpp>

#include <cmath>
#include <cstddef>
#include <utility>

namespace origon {

Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    glCreateBuffers(1, &m_vbo);
    glNamedBufferStorage(m_vbo, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), 0);
    glCreateBuffers(1, &m_ibo);
    glNamedBufferStorage(m_ibo, static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)), indices.data(), 0);

    glCreateVertexArrays(1, &m_vao);
    glVertexArrayVertexBuffer(m_vao, 0, m_vbo, 0, sizeof(Vertex));
    glVertexArrayElementBuffer(m_vao, m_ibo);

    glEnableVertexArrayAttrib(m_vao, 0);
    glVertexArrayAttribFormat(m_vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
    glVertexArrayAttribBinding(m_vao, 0, 0);
    glEnableVertexArrayAttrib(m_vao, 1);
    glVertexArrayAttribFormat(m_vao, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal));
    glVertexArrayAttribBinding(m_vao, 1, 0);
    glEnableVertexArrayAttrib(m_vao, 2);
    glVertexArrayAttribFormat(m_vao, 2, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, uv));
    glVertexArrayAttribBinding(m_vao, 2, 0);

    m_indexCount = static_cast<int>(indices.size());
}

Mesh::Mesh(const std::vector<SkinnedVertex>& vertices, const std::vector<uint32_t>& indices) {
    glCreateBuffers(1, &m_vbo);
    glNamedBufferStorage(m_vbo, static_cast<GLsizeiptr>(vertices.size() * sizeof(SkinnedVertex)), vertices.data(), 0);
    glCreateBuffers(1, &m_ibo);
    glNamedBufferStorage(m_ibo, static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)), indices.data(), 0);
    glCreateVertexArrays(1, &m_vao);
    glVertexArrayVertexBuffer(m_vao, 0, m_vbo, 0, sizeof(SkinnedVertex));
    glVertexArrayElementBuffer(m_vao, m_ibo);
    glEnableVertexArrayAttrib(m_vao, 0);
    glVertexArrayAttribFormat(m_vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(SkinnedVertex, position));
    glVertexArrayAttribBinding(m_vao, 0, 0);
    glEnableVertexArrayAttrib(m_vao, 1);
    glVertexArrayAttribFormat(m_vao, 1, 3, GL_FLOAT, GL_FALSE, offsetof(SkinnedVertex, normal));
    glVertexArrayAttribBinding(m_vao, 1, 0);
    glEnableVertexArrayAttrib(m_vao, 2);
    glVertexArrayAttribFormat(m_vao, 2, 2, GL_FLOAT, GL_FALSE, offsetof(SkinnedVertex, uv));
    glVertexArrayAttribBinding(m_vao, 2, 0);
    glEnableVertexArrayAttrib(m_vao, 3);
    glVertexArrayAttribIFormat(m_vao, 3, 4, GL_UNSIGNED_SHORT, offsetof(SkinnedVertex, joints));
    glVertexArrayAttribBinding(m_vao, 3, 0);
    glEnableVertexArrayAttrib(m_vao, 4);
    glVertexArrayAttribFormat(m_vao, 4, 4, GL_FLOAT, GL_FALSE, offsetof(SkinnedVertex, weights));
    glVertexArrayAttribBinding(m_vao, 4, 0);
    m_indexCount = static_cast<int>(indices.size());
}

Mesh::~Mesh() { release(); }

Mesh::Mesh(Mesh&& other) noexcept
    : m_vao(std::exchange(other.m_vao, 0)), m_vbo(std::exchange(other.m_vbo, 0)),
      m_ibo(std::exchange(other.m_ibo, 0)), m_indexCount(std::exchange(other.m_indexCount, 0)) {}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        release();
        m_vao = std::exchange(other.m_vao, 0);
        m_vbo = std::exchange(other.m_vbo, 0);
        m_ibo = std::exchange(other.m_ibo, 0);
        m_indexCount = std::exchange(other.m_indexCount, 0);
    }
    return *this;
}

void Mesh::release() {
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_ibo) glDeleteBuffers(1, &m_ibo);
    m_vao = m_vbo = m_ibo = 0;
    m_indexCount = 0;
}

void Mesh::draw() const {
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
}

void Mesh::drawInstanced(int count) const {
    glBindVertexArray(m_vao);
    glDrawElementsInstanced(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr, count);
}

Mesh Mesh::makeCube() {
    struct Face {
        glm::vec3 normal;
        glm::vec3 u;
        glm::vec3 v;
    };
    const Face faces[6] = {
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},  {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},  {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},   {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    };
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    for (const Face& face : faces) {
        const auto base = static_cast<uint32_t>(vertices.size());
        const glm::vec3 center = face.normal * 0.5f;
        const glm::vec2 corners[4] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
        for (const glm::vec2& c : corners) {
            vertices.push_back({center + face.u * c.x + face.v * c.y, face.normal, c + 0.5f});
        }
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return Mesh(vertices, indices);
}

Mesh Mesh::makeCylinder(int segments) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    for (int seg = 0; seg <= segments; ++seg) {
        const float theta = glm::two_pi<float>() * static_cast<float>(seg) / static_cast<float>(segments);
        const glm::vec3 normal(std::cos(theta), std::sin(theta), 0.0f);
        const float u = static_cast<float>(seg) / static_cast<float>(segments);
        vertices.push_back({normal * 0.5f + glm::vec3(0, 0, -0.5f), normal, {u, 0.0f}});
        vertices.push_back({normal * 0.5f + glm::vec3(0, 0, 0.5f), normal, {u, 1.0f}});
    }
    for (int seg = 0; seg < segments; ++seg) {
        const auto a = static_cast<uint32_t>(seg * 2);
        indices.insert(indices.end(), {a, a + 2, a + 1, a + 1, a + 2, a + 3});
    }
    for (const float side : {-1.0f, 1.0f}) {
        const auto center = static_cast<uint32_t>(vertices.size());
        const glm::vec3 normal(0.0f, 0.0f, side);
        vertices.push_back({glm::vec3(0, 0, 0.5f * side), normal, {0.5f, 0.5f}});
        for (int seg = 0; seg <= segments; ++seg) {
            const float theta = glm::two_pi<float>() * static_cast<float>(seg) / static_cast<float>(segments);
            const glm::vec2 c(std::cos(theta), std::sin(theta));
            vertices.push_back({glm::vec3(c * 0.5f, 0.5f * side), normal, c * 0.5f + 0.5f});
        }
        for (int seg = 0; seg < segments; ++seg) {
            const auto a = center + 1 + static_cast<uint32_t>(seg);
            if (side > 0.0f) indices.insert(indices.end(), {center, a, a + 1});
            else indices.insert(indices.end(), {center, a + 1, a});
        }
    }
    return Mesh(vertices, indices);
}

Mesh Mesh::makeCapsule(float radius, float cylinderHeight, int segments, int rings) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    const float halfHeight = cylinderHeight * 0.5f;
    // Latitude rows: top hemisphere (rings+1 rows) then bottom hemisphere (rings+1 rows).
    const int rows = (rings + 1) * 2;
    for (int row = 0; row < rows; ++row) {
        const bool top = row <= rings;
        const int ringIndex = top ? row : row - (rings + 1);
        const float phi = top ? glm::half_pi<float>() * (1.0f - static_cast<float>(ringIndex) / rings)
                              : -glm::half_pi<float>() * static_cast<float>(ringIndex) / rings;
        const float y = std::sin(phi);
        const float r = std::cos(phi);
        const float offset = top ? halfHeight : -halfHeight;
        for (int seg = 0; seg <= segments; ++seg) {
            const float theta = glm::two_pi<float>() * static_cast<float>(seg) / segments;
            const glm::vec3 normal(r * std::cos(theta), y, r * std::sin(theta));
            vertices.push_back({normal * radius + glm::vec3(0, offset, 0), normal,
                                {static_cast<float>(seg) / segments, static_cast<float>(row) / (rows - 1)}});
        }
    }
    const int stride = segments + 1;
    for (int row = 0; row < rows - 1; ++row) {
        for (int seg = 0; seg < segments; ++seg) {
            const auto a = static_cast<uint32_t>(row * stride + seg);
            const auto b = static_cast<uint32_t>((row + 1) * stride + seg);
            indices.insert(indices.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
    }
    return Mesh(vertices, indices);
}

} // namespace origon
