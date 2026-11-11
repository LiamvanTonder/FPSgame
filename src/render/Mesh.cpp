#include "render/Mesh.h"

#include <cmath>

void Mesh::upload(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices.size() * sizeof(Vertex)), vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(indices.size() * sizeof(uint32_t)), indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glBindVertexArray(0);

    m_count = GLsizei(indices.size());
}

void Mesh::draw() const {
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_count, GL_UNSIGNED_INT, nullptr);
}

void Mesh::destroy() {
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
    m_vao = m_vbo = m_ebo = 0;
}

Mesh Mesh::makeBox() {
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
    const glm::vec3 normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const glm::vec3& n : normals) {
        // Two tangent axes forming a right-handed basis with n, so winding is CCW from outside.
        glm::vec3 u = std::abs(n.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        glm::vec3 t = glm::cross(u, n);
        glm::vec3 b = glm::cross(n, t);
        uint32_t base = uint32_t(v.size());
        glm::vec3 c = n * 0.5f;
        v.push_back({c - t * 0.5f - b * 0.5f, n});
        v.push_back({c + t * 0.5f - b * 0.5f, n});
        v.push_back({c + t * 0.5f + b * 0.5f, n});
        v.push_back({c - t * 0.5f + b * 0.5f, n});
        idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    Mesh m;
    m.upload(v, idx);
    return m;
}

Mesh Mesh::makeCylinder(int segments) {
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
    const float r = 0.5f;
    // Side
    for (int i = 0; i <= segments; ++i) {
        float a = float(i) / segments * 2.0f * float(M_PI);
        glm::vec3 n(std::cos(a), std::sin(a), 0.0f);
        v.push_back({n * r + glm::vec3(0, 0, -0.5f), n});
        v.push_back({n * r + glm::vec3(0, 0, 0.5f), n});
    }
    for (int i = 0; i < segments; ++i) {
        uint32_t a = i * 2, b = a + 1, c = a + 2, d = a + 3;
        idx.insert(idx.end(), {a, c, b, b, c, d});
    }
    // Caps
    for (int cap = 0; cap < 2; ++cap) {
        float z = cap ? 0.5f : -0.5f;
        glm::vec3 n(0, 0, cap ? 1.0f : -1.0f);
        uint32_t centre = uint32_t(v.size());
        v.push_back({{0, 0, z}, n});
        for (int i = 0; i <= segments; ++i) {
            float a = float(i) / segments * 2.0f * float(M_PI);
            v.push_back({{std::cos(a) * r, std::sin(a) * r, z}, n});
        }
        for (int i = 0; i < segments; ++i) {
            uint32_t p0 = centre + 1 + i, p1 = p0 + 1;
            if (cap) idx.insert(idx.end(), {centre, p0, p1});
            else idx.insert(idx.end(), {centre, p1, p0});
        }
    }
    Mesh m;
    m.upload(v, idx);
    return m;
}

Mesh Mesh::makeSphere(int stacks, int slices) {
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
    for (int i = 0; i <= stacks; ++i) {
        float phi = float(i) / stacks * float(M_PI);
        for (int j = 0; j <= slices; ++j) {
            float theta = float(j) / slices * 2.0f * float(M_PI);
            glm::vec3 n(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            v.push_back({n * 0.5f, n});
        }
    }
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            uint32_t a = i * (slices + 1) + j, b = a + slices + 1;
            idx.insert(idx.end(), {a, a + 1, b, b, a + 1, b + 1});
        }
    }
    Mesh m;
    m.upload(v, idx);
    return m;
}
