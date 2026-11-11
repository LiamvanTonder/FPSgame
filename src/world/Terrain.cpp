#include "world/Terrain.h"

#include <algorithm>
#include <cmath>

void Terrain::create(int n, float cell, const glm::vec2& origin) {
    m_n = n;
    m_cell = cell;
    m_origin = origin;
    m_h.assign(size_t(n) * n, 0.0f);
    m_maxH = m_minH = 0.0f;
}

void Terrain::updateBounds() {
    if (m_h.empty()) return;
    auto mm = std::minmax_element(m_h.begin(), m_h.end());
    m_minH = *mm.first;
    m_maxH = *mm.second;
}

bool Terrain::inside(float x, float z) const {
    float fx = (x - m_origin.x) / m_cell, fz = (z - m_origin.y) / m_cell;
    return fx >= 0.0f && fz >= 0.0f && fx <= float(m_n - 1) && fz <= float(m_n - 1);
}

float Terrain::height(float x, float z) const {
    if (m_n < 2) return 0.0f;
    float fx = std::clamp((x - m_origin.x) / m_cell, 0.0f, float(m_n - 1) - 1e-4f);
    float fz = std::clamp((z - m_origin.y) / m_cell, 0.0f, float(m_n - 1) - 1e-4f);
    int ix = int(fx), iz = int(fz);
    float u = fx - ix, v = fz - iz;
    float h00 = at(ix, iz), h10 = at(ix + 1, iz), h01 = at(ix, iz + 1), h11 = at(ix + 1, iz + 1);
    // Triangles (00,10,11) when u >= v, else (00,11,01).
    if (u >= v) return h00 + (h10 - h00) * u + (h11 - h10) * v;
    return h00 + (h11 - h01) * u + (h01 - h00) * v;
}

glm::vec3 Terrain::normal(float x, float z) const {
    const float e = m_cell * 0.5f;
    float hx = height(x + e, z) - height(x - e, z);
    float hz = height(x, z + e) - height(x, z - e);
    return glm::normalize(glm::vec3(-hx, 2.0f * e, -hz));
}

bool Terrain::segment(const glm::vec3& a, const glm::vec3& b, float& t, glm::vec3& n) const {
    if (m_n < 2) return false;
    if (a.y > m_maxH + 0.01f && b.y > m_maxH + 0.01f) return false;
    glm::vec3 d = b - a;
    float flat = std::sqrt(d.x * d.x + d.z * d.z);
    // March in steps of a third of a cell, then refine the crossing by bisection.
    int steps = std::max(1, int(std::ceil(flat / (m_cell * 0.33f))));
    steps = std::min(steps, 4096);
    float prevT = 0.0f;
    float prevGap = a.y - height(a.x, a.z);
    if (prevGap < 0.0f) {
        // Started underground (e.g. a probe from inside a hill): report the start.
        t = 0.0f;
        n = normal(a.x, a.z);
        return true;
    }
    for (int i = 1; i <= steps; ++i) {
        float tt = float(i) / steps;
        glm::vec3 p = a + d * tt;
        float gap = p.y - height(p.x, p.z);
        if (gap <= 0.0f) {
            float lo = prevT, hi = tt;
            for (int k = 0; k < 28; ++k) {
                float mid = (lo + hi) * 0.5f;
                glm::vec3 q = a + d * mid;
                if (q.y - height(q.x, q.z) > 0.0f) lo = mid;
                else hi = mid;
            }
            t = hi;
            glm::vec3 q = a + d * t;
            n = normal(q.x, q.z);
            return true;
        }
        prevT = tt;
        prevGap = gap;
    }
    return false;
}
