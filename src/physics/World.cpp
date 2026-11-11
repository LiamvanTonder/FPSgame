#include "physics/World.h"

#include "world/Terrain.h"

#include <algorithm>
#include <cmath>

int World::add(const Collider& c) {
    m_colliders.push_back(c);
    m_gridDirty = true;
    return int(m_colliders.size()) - 1;
}

void World::setTerrain(const Terrain* t) {
    m_terrain = t;
    if (t && m_terrainCollider < 0) {
        Collider c;
        c.box = OBB{glm::vec3(0.0f), glm::vec3(1.0f), glm::mat3(1.0f)}; // identity frame: decals store world coordinates
        c.surface = Surface::Grass;
        c.terrain = true;
        c.blocksPlayer = false;
        c.dynamic = true; // never in the grid; terrain is tested separately
        m_terrainCollider = add(c);
    }
}

void World::buildGrid() const {
    m_gridDirty = false;
    m_cells.clear();
    m_big.clear();
    m_dynamic.clear();
    m_stamp.assign(m_colliders.size(), 0);
    m_stampValue = 0;

    glm::vec2 mn(1e9f), mx(-1e9f);
    int statics = 0;
    for (const Collider& c : m_colliders) {
        if (c.dynamic || c.terrain || c.owner != kOwnerWorld) continue;
        AABB b = c.box.bounds();
        mn = glm::min(mn, glm::vec2(b.min.x, b.min.z));
        mx = glm::max(mx, glm::vec2(b.max.x, b.max.z));
        ++statics;
    }
    if (statics == 0) {
        m_gridW = m_gridH = 0;
        for (int i = 0; i < int(m_colliders.size()); ++i)
            if (!m_colliders[i].terrain) m_dynamic.push_back(i);
        return;
    }
    m_gridMin = mn - glm::vec2(1.0f);
    m_gridW = std::max(1, int(std::ceil((mx.x - mn.x + 2.0f) / m_cell)));
    m_gridH = std::max(1, int(std::ceil((mx.y - mn.y + 2.0f) / m_cell)));
    m_cells.assign(size_t(m_gridW) * m_gridH, {});

    for (int i = 0; i < int(m_colliders.size()); ++i) {
        const Collider& c = m_colliders[i];
        if (c.terrain) continue;
        if (c.dynamic || c.owner != kOwnerWorld) {
            m_dynamic.push_back(i);
            continue;
        }
        AABB b = c.box.bounds();
        int x0 = int((b.min.x - m_gridMin.x) / m_cell), x1 = int((b.max.x - m_gridMin.x) / m_cell);
        int z0 = int((b.min.z - m_gridMin.y) / m_cell), z1 = int((b.max.z - m_gridMin.y) / m_cell);
        x0 = std::clamp(x0, 0, m_gridW - 1);
        x1 = std::clamp(x1, 0, m_gridW - 1);
        z0 = std::clamp(z0, 0, m_gridH - 1);
        z1 = std::clamp(z1, 0, m_gridH - 1);
        if ((x1 - x0 + 1) * (z1 - z0 + 1) > 64) {
            m_big.push_back(i);
            continue;
        }
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) m_cells[size_t(z) * m_gridW + x].push_back(i);
    }
}

bool World::testCollider(int i, const glm::vec3& a, const glm::vec3& b, const SegmentQuery& q, float& best, WorldHit& out) const {
    const Collider& c = m_colliders[i];
    if (i == q.ignoreCollider || !c.active || c.owner == q.ignoreOwner || c.terrain) return false;
    if (q.skipPenetrable && c.penetrable) return false;
    if (q.vision ? !c.blocksVision : !c.blocksBullets) return false;
    if (q.staticOnly && (c.owner != kOwnerWorld || c.steelPlate >= 0 || c.paperTarget >= 0)) return false;
    float t;
    glm::vec3 n;
    if (intersectSegmentOBB(a, b, c.box, t, n) && t <= best) {
        best = t;
        out.t = t;
        out.normal = n;
        out.collider = i;
        return true;
    }
    return false;
}

bool World::segment(const glm::vec3& a, const glm::vec3& b, WorldHit& out, const SegmentQuery& q) const {
    if (m_gridDirty) buildGrid();
    bool hit = false;
    float best = 1.0f;

    if (m_terrain && !q.noTerrain && m_terrainCollider != q.ignoreCollider) {
        float t;
        glm::vec3 n;
        if (m_terrain->segment(a, b, t, n) && t <= best) {
            best = t;
            out.t = t;
            out.normal = n;
            out.collider = m_terrainCollider;
            hit = true;
        }
    }
    for (int i : m_dynamic) hit |= testCollider(i, a, b, q, best, out);
    for (int i : m_big) hit |= testCollider(i, a, b, q, best, out);

    if (m_gridW > 0) {
        // 2-D DDA through the grid cells the segment crosses (XZ), nearest first.
        if (++m_stampValue == 0) {
            std::fill(m_stamp.begin(), m_stamp.end(), 0);
            m_stampValue = 1;
        }
        glm::vec2 p0((a.x - m_gridMin.x) / m_cell, (a.z - m_gridMin.y) / m_cell);
        glm::vec2 p1((b.x - m_gridMin.x) / m_cell, (b.z - m_gridMin.y) / m_cell);
        glm::vec2 d = p1 - p0;
        // Clip to the grid rectangle.
        float t0 = 0.0f, t1 = 1.0f;
        bool inside = true;
        for (int k = 0; k < 2 && inside; ++k) {
            float lo = 0.0f, hi = float(k == 0 ? m_gridW : m_gridH);
            if (std::abs(d[k]) < 1e-9f) {
                if (p0[k] < lo || p0[k] >= hi) inside = false;
            } else {
                float ta = (lo - p0[k]) / d[k], tb = (hi - p0[k]) / d[k];
                if (ta > tb) std::swap(ta, tb);
                t0 = std::max(t0, ta);
                t1 = std::min(t1, tb);
                if (t0 > t1) inside = false;
            }
        }
        if (inside) {
            glm::vec2 s = p0 + d * t0;
            int cx = std::clamp(int(std::floor(s.x)), 0, m_gridW - 1);
            int cz = std::clamp(int(std::floor(s.y)), 0, m_gridH - 1);
            int stepX = d.x > 0 ? 1 : -1, stepZ = d.y > 0 ? 1 : -1;
            float tDeltaX = std::abs(d.x) > 1e-9f ? 1.0f / std::abs(d.x) : 1e30f;
            float tDeltaZ = std::abs(d.y) > 1e-9f ? 1.0f / std::abs(d.y) : 1e30f;
            float nextX = std::abs(d.x) > 1e-9f ? ((d.x > 0 ? (cx + 1 - p0.x) : (p0.x - cx)) / std::abs(d.x)) : 1e30f;
            float nextZ = std::abs(d.y) > 1e-9f ? ((d.y > 0 ? (cz + 1 - p0.y) : (p0.y - cz)) / std::abs(d.y)) : 1e30f;
            float tCell = t0;
            for (int guard = 0; guard < 100000; ++guard) {
                // Everything in later cells starts beyond tCell; stop once a nearer hit is known.
                if (tCell > best + 1e-6f) break;
                for (int i : m_cells[size_t(cz) * m_gridW + cx]) {
                    if (m_stamp[i] == m_stampValue) continue;
                    m_stamp[i] = m_stampValue;
                    hit |= testCollider(i, a, b, q, best, out);
                }
                if (nextX < nextZ) {
                    tCell = nextX;
                    nextX += tDeltaX;
                    cx += stepX;
                } else {
                    tCell = nextZ;
                    nextZ += tDeltaZ;
                    cz += stepZ;
                }
                if (tCell > t1 || cx < 0 || cz < 0 || cx >= m_gridW || cz >= m_gridH) break;
            }
        }
    }
    if (hit) out.point = a + (b - a) * out.t;
    return hit;
}

void World::overlap(const AABB& box, std::vector<int>& out) const {
    if (m_gridDirty) buildGrid();
    out.clear();
    auto consider = [&](int i) {
        const Collider& c = m_colliders[i];
        if (c.active && !c.terrain && box.overlaps(c.box.bounds())) out.push_back(i);
    };
    for (int i : m_dynamic) consider(i);
    for (int i : m_big) consider(i);
    if (m_gridW <= 0) return;
    if (++m_stampValue == 0) {
        std::fill(m_stamp.begin(), m_stamp.end(), 0);
        m_stampValue = 1;
    }
    int x0 = std::clamp(int((box.min.x - m_gridMin.x) / m_cell), 0, m_gridW - 1);
    int x1 = std::clamp(int((box.max.x - m_gridMin.x) / m_cell), 0, m_gridW - 1);
    int z0 = std::clamp(int((box.min.z - m_gridMin.y) / m_cell), 0, m_gridH - 1);
    int z1 = std::clamp(int((box.max.z - m_gridMin.y) / m_cell), 0, m_gridH - 1);
    if (box.max.x < m_gridMin.x || box.max.z < m_gridMin.y) return;
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x)
            for (int i : m_cells[size_t(z) * m_gridW + x]) {
                if (m_stamp[i] == m_stampValue) continue;
                m_stamp[i] = m_stampValue;
                consider(i);
            }
}

float World::groundHeight(const glm::vec3& p, float top, float fallback) const {
    SegmentQuery q;
    q.staticOnly = true;
    WorldHit hit;
    if (segment(glm::vec3(p.x, top, p.z), glm::vec3(p.x, top - 60.0f, p.z), hit, q)) return hit.point.y;
    return fallback;
}
