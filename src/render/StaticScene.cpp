#include "render/StaticScene.h"

#include "world/MapData.h"
#include "world/Terrain.h"

#include <cmath>

void StaticScene::clear() {
    destroy();
    m_list.clear();
    m_index.clear();
}

StaticScene::Chunk& StaticScene::chunkAt(const glm::vec3& p) {
    int64_t cx = int64_t(std::floor(p.x / chunkSize)), cz = int64_t(std::floor(p.z / chunkSize));
    int64_t key = (cx << 32) ^ (cz & 0xffffffff);
    auto it = m_index.find(key);
    if (it != m_index.end()) return m_list[it->second];
    m_index[key] = int(m_list.size());
    m_list.emplace_back();
    return m_list.back();
}

namespace {
void addQuad(StaticScene::Chunk& c, const glm::vec3& a, const glm::vec3& b, const glm::vec3& cc, const glm::vec3& d,
             const glm::vec3& n, const glm::vec3& col, float pat) {
    uint32_t base = uint32_t(c.vertices.size());
    for (const glm::vec3* p : {&a, &b, &cc, &d}) {
        c.vertices.push_back({*p, n, col, pat});
        c.min = glm::min(c.min, *p);
        c.max = glm::max(c.max, *p);
    }
    c.indices.insert(c.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}
} // namespace

void StaticScene::add(const SceneBlock& b) {
    Chunk& c = chunkAt(b.box.center);
    const glm::mat3& R = b.box.rotation;
    const glm::vec3& h = b.box.half;
    float pat = float(int(b.pattern));
    auto W = [&](const glm::vec3& local) { return b.box.center + R * (local * h); };

    if (b.shape == SceneBlock::Box) {
        const glm::vec3 normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (const glm::vec3& n : normals) {
            glm::vec3 u = std::abs(n.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
            glm::vec3 t = glm::cross(u, n);
            glm::vec3 bt = glm::cross(n, t);
            addQuad(c, W(n - t - bt), W(n + t - bt), W(n + t + bt), W(n - t + bt), glm::normalize(R * n), b.color, pat);
        }
        return;
    }
    if (b.shape == SceneBlock::Cylinder) {
        const int seg = 8;
        for (int i = 0; i < seg; ++i) {
            float a0 = float(i) / seg * 6.2831853f, a1 = float(i + 1) / seg * 6.2831853f;
            glm::vec3 p0(std::cos(a0), 0, std::sin(a0)), p1(std::cos(a1), 0, std::sin(a1));
            glm::vec3 n = glm::normalize(R * ((p0 + p1) * 0.5f / h * h));
            addQuad(c, W(p0 + glm::vec3(0, -1, 0)), W(p0 + glm::vec3(0, 1, 0)), W(p1 + glm::vec3(0, 1, 0)), W(p1 + glm::vec3(0, -1, 0)),
                    n, b.color, pat);
            // Top cap (as a degenerate quad fan)
            addQuad(c, W(glm::vec3(0, 1, 0)), W(p1 + glm::vec3(0, 1, 0)), W(p0 + glm::vec3(0, 1, 0)), W(glm::vec3(0, 1, 0)),
                    glm::normalize(R[1]), b.color, pat);
        }
        return;
    }
    // Ellipsoid: low-poly UV sphere.
    const int stacks = 5, slices = 8;
    for (int i = 0; i < stacks; ++i) {
        float t0 = float(i) / stacks * 3.14159265f, t1 = float(i + 1) / stacks * 3.14159265f;
        for (int k = 0; k < slices; ++k) {
            float p0 = float(k) / slices * 6.2831853f, p1 = float(k + 1) / slices * 6.2831853f;
            auto S = [](float t, float p) { return glm::vec3(std::sin(t) * std::cos(p), std::cos(t), std::sin(t) * std::sin(p)); };
            glm::vec3 a = S(t0, p0), bb = S(t1, p0), cc = S(t1, p1), d = S(t0, p1);
            glm::vec3 n = glm::normalize(R * ((a + bb + cc + d) * 0.25f / glm::max(h, glm::vec3(0.01f))));
            addQuad(c, W(a), W(d), W(cc), W(bb), n, b.color, pat);
        }
    }
}

void StaticScene::addTerrain(const Terrain& t, const std::vector<glm::vec3>& colors) {
    int n = t.size();
    if (n < 2) return;
    float cell = t.cell();
    glm::vec2 o = t.origin();
    // One chunk-sized patch at a time so culling works.
    int per = std::max(1, int(chunkSize / cell));
    float pat = float(int(Pattern::Terrain));
    for (int z0 = 0; z0 < n - 1; z0 += per) {
        for (int x0 = 0; x0 < n - 1; x0 += per) {
            int x1 = std::min(n - 1, x0 + per), z1 = std::min(n - 1, z0 + per);
            glm::vec3 centre(o.x + (x0 + x1) * 0.5f * cell, 0.0f, o.y + (z0 + z1) * 0.5f * cell);
            Chunk& c = chunkAt(centre);
            uint32_t base = uint32_t(c.vertices.size());
            int w = x1 - x0 + 1;
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x) {
                    glm::vec3 p(o.x + x * cell, t.at(x, z), o.y + z * cell);
                    glm::vec3 col = colors.empty() ? glm::vec3(0.4f, 0.46f, 0.3f) : colors[size_t(z) * n + x];
                    c.vertices.push_back({p, t.normal(p.x, p.z), col, pat});
                    c.min = glm::min(c.min, p);
                    c.max = glm::max(c.max, p);
                }
            for (int z = 0; z < z1 - z0; ++z)
                for (int x = 0; x < x1 - x0; ++x) {
                    uint32_t i00 = base + z * w + x, i10 = i00 + 1, i01 = i00 + w, i11 = i01 + 1;
                    // Same split as Terrain::height: (00,10,11) and (00,11,01), counter-clockwise from above.
                    c.indices.insert(c.indices.end(), {i00, i11, i10, i00, i01, i11});
                }
        }
    }
}

void StaticScene::upload() {
    for (Chunk& c : m_list) {
        if (c.vao || c.indices.empty()) continue;
        glGenVertexArrays(1, &c.vao);
        glGenBuffers(1, &c.vbo);
        glGenBuffers(1, &c.ebo);
        glBindVertexArray(c.vao);
        glBindBuffer(GL_ARRAY_BUFFER, c.vbo);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(c.vertices.size() * sizeof(Vertex)), c.vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, c.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(c.indices.size() * sizeof(uint32_t)), c.indices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pattern));
        glBindVertexArray(0);
        c.count = GLsizei(c.indices.size());
        c.vertices.clear();
        c.vertices.shrink_to_fit();
        c.indices.clear();
        c.indices.shrink_to_fit();
    }
}

void StaticScene::destroy() {
    for (Chunk& c : m_list) {
        if (c.vao) glDeleteVertexArrays(1, &c.vao);
        if (c.vbo) glDeleteBuffers(1, &c.vbo);
        if (c.ebo) glDeleteBuffers(1, &c.ebo);
        c.vao = c.vbo = c.ebo = 0;
    }
}

size_t StaticScene::triangleCount() const {
    size_t n = 0;
    for (const Chunk& c : m_list) n += size_t(c.count) / 3;
    return n;
}
