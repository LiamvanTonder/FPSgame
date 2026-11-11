#include "ai/NavGrid.h"

#include "physics/World.h"
#include "world/Terrain.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace {
const float kRadius = 0.42f;    // body radius + margin (keeps routes off walls and door frames)
const float kStep = 0.4f;       // anything lower is walked over
const float kHeadroom = 1.7f;   // anything higher is walked under
} // namespace

bool NavGrid::toCell(const glm::vec2& p, int& x, int& z) const {
    x = int(std::floor((p.x - m_origin) / m_cell));
    z = int(std::floor((p.y - m_origin) / m_cell));
    return x >= 0 && z >= 0 && x < m_n && z < m_n;
}

void NavGrid::build(const World& world, float halfSize, float cellSize) {
    m_cell = cellSize;
    m_origin = -halfSize;
    m_n = int(std::ceil(2.0f * halfSize / cellSize));
    size_t total = size_t(m_n) * m_n;
    m_flags.assign(total, Walkable);
    m_height.assign(total, 0);
    const Terrain* terrain = world.terrain();

    // Slopes and the map edge
    if (terrain) {
        for (int z = 0; z < m_n; ++z)
            for (int x = 0; x < m_n; ++x) {
                glm::vec2 c = center(x, z);
                if (terrain->normal(c.x, c.y).y < 0.66f) m_flags[index(x, z)] = 0;
            }
    }
    for (int z = 0; z < m_n; ++z)
        for (int x = 0; x < m_n; ++x) {
            glm::vec2 c = center(x, z);
            if (std::abs(c.x) > halfSize - 4.0f || std::abs(c.y) > halfSize - 4.0f) m_flags[index(x, z)] = 0;
        }

    // Obstacles
    for (const Collider& col : world.colliders()) {
        if (!col.blocksPlayer || !col.active || col.dynamic || col.terrain || col.owner != kOwnerWorld) continue;
        AABB b = col.box.bounds();
        int x0, z0, x1, z1;
        toCell({b.min.x - kRadius, b.min.z - kRadius}, x0, z0);
        toCell({b.max.x + kRadius, b.max.z + kRadius}, x1, z1);
        x0 = std::clamp(x0, 0, m_n - 1);
        z0 = std::clamp(z0, 0, m_n - 1);
        x1 = std::clamp(x1, 0, m_n - 1);
        z1 = std::clamp(z1, 0, m_n - 1);
        if ((x1 - x0) * (z1 - z0) > 400000) continue; // ground slabs
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                glm::vec2 c = center(x, z);
                // Bodies collide with the box's axis-aligned bounds, so that is what blocks.
                if (c.x < b.min.x - kRadius || c.x > b.max.x + kRadius || c.y < b.min.z - kRadius || c.y > b.max.z + kRadius) continue;
                float ground = terrain ? terrain->height(c.x, c.y) : 0.0f;
                if (b.max.y <= ground + kStep || b.min.y >= ground + kHeadroom) continue;
                m_flags[index(x, z)] &= ~Walkable;
                float h = std::clamp((b.max.y - ground) * 10.0f, 0.0f, 255.0f);
                m_height[index(x, z)] = std::max(m_height[index(x, z)], uint8_t(h));
            }
    }

    // Cells beside tall obstacles count as covered; open ground costs more to cross.
    for (int z = 2; z < m_n - 2; ++z)
        for (int x = 2; x < m_n - 2; ++x) {
            if (!(m_flags[index(x, z)] & Walkable)) continue;
            bool cover = false;
            for (int dz = -2; dz <= 2 && !cover; ++dz)
                for (int dx = -2; dx <= 2 && !cover; ++dx)
                    if (!(m_flags[index(x + dx, z + dz)] & Walkable) && m_height[index(x + dx, z + dz)] >= 10) cover = true;
            if (cover) m_flags[index(x, z)] |= NearCover;
        }

    m_g.assign(total, 0.0f);
    m_parent.assign(total, -1);
    m_stamp.assign(total, 0);
    m_closed.assign(total, 0);
    m_search = 0;
}

bool NavGrid::walkable(const glm::vec2& p) const {
    int x, z;
    return toCell(p, x, z) && cellWalkable(x, z);
}

bool NavGrid::clearLine(const glm::vec2& a, const glm::vec2& b) const {
    float len = glm::length(b - a);
    int steps = std::max(1, int(len / (m_cell * 0.5f)));
    for (int i = 0; i <= steps; ++i) {
        glm::vec2 p = a + (b - a) * (float(i) / steps);
        if (!walkable(p)) return false;
    }
    return true;
}

bool NavGrid::nearestWalkable(const glm::vec2& p, glm::vec2& out, int maxR) const {
    int cx, cz;
    toCell(p, cx, cz);
    for (int r = 0; r <= maxR; ++r)
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                if (cellWalkable(cx + dx, cz + dz)) {
                    out = center(cx + dx, cz + dz);
                    return true;
                }
            }
    return false;
}

bool NavGrid::findPath(const glm::vec2& from, const glm::vec2& to, std::vector<glm::vec2>& out, int maxExpansions) {
    out.clear();
    if (!valid()) return false;
    glm::vec2 s, g;
    if (!nearestWalkable(from, s) || !nearestWalkable(to, g)) return false;
    int sx, sz, gx, gz;
    toCell(s, sx, sz);
    toCell(g, gx, gz);
    if (clearLine(from, g)) {
        out.push_back(g);
        return true;
    }
    if (++m_search == 0) {
        std::fill(m_stamp.begin(), m_stamp.end(), 0);
        m_search = 1;
    }
    struct Node {
        float f;
        int i;
        bool operator<(const Node& o) const { return f > o.f; }
    };
    std::priority_queue<Node> open;
    auto h = [&](int x, int z) {
        float dx = float(std::abs(x - gx)), dz = float(std::abs(z - gz));
        return (std::max(dx, dz) + 0.4142f * std::min(dx, dz)) * 1.5f; // weighted octile
    };
    int start = index(sx, sz), goal = index(gx, gz);
    m_g[start] = 0.0f;
    m_parent[start] = -1;
    m_stamp[start] = m_search;
    m_closed[start] = 0;
    open.push({h(sx, sz), start});
    int expansions = 0;
    bool found = false;
    const int ox[8] = {1, -1, 0, 0, 1, 1, -1, -1}, oz[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    while (!open.empty() && expansions < maxExpansions) {
        Node n = open.top();
        open.pop();
        if (m_closed[n.i] && m_stamp[n.i] == m_search) continue;
        m_closed[n.i] = 1;
        ++expansions;
        if (n.i == goal) {
            found = true;
            break;
        }
        int x = n.i % m_n, z = n.i / m_n;
        for (int k = 0; k < 8; ++k) {
            int nx = x + ox[k], nz = z + oz[k];
            if (!cellWalkable(nx, nz)) continue;
            if (k >= 4 && (!cellWalkable(x + ox[k], z) || !cellWalkable(x, z + oz[k]))) continue; // no corner cutting
            int ni = index(nx, nz);
            float step = k < 4 ? 1.0f : 1.4142f;
            if (!(m_flags[ni] & NearCover)) step *= 1.25f; // exposed ground
            float ng = m_g[n.i] + step;
            if (m_stamp[ni] != m_search) {
                m_stamp[ni] = m_search;
                m_closed[ni] = 0;
                m_g[ni] = ng;
                m_parent[ni] = n.i;
                open.push({ng + h(nx, nz), ni});
            } else if (!m_closed[ni] && ng < m_g[ni]) {
                m_g[ni] = ng;
                m_parent[ni] = n.i;
                open.push({ng + h(nx, nz), ni});
            }
        }
    }
    m_lastExpansions = expansions;
    if (!found) return false;

    std::vector<glm::vec2> cells;
    for (int i = goal; i >= 0; i = m_parent[i]) {
        cells.push_back(center(i % m_n, i / m_n));
        if (i == start) break;
    }
    std::reverse(cells.begin(), cells.end());
    // String pulling: skip to the farthest cell reachable in a straight line.
    glm::vec2 cur = from;
    size_t i = 0;
    while (i < cells.size()) {
        size_t far = i;
        for (size_t j = cells.size() - 1; j > i; --j) {
            if (j - i > 200) j = i + 200;
            if (clearLine(cur, cells[j])) {
                far = j;
                break;
            }
        }
        cur = cells[far];
        out.push_back(cur);
        i = far + 1;
    }
    return true;
}

void NavGrid::findCover(const glm::vec2& around, const glm::vec2& threat, float radius, std::vector<Cover>& out, int maxResults) const {
    out.clear();
    if (!valid()) return;
    int cx, cz;
    toCell(around, cx, cz);
    int r = int(radius / m_cell);
    struct Cand {
        float d;
        Cover c;
    };
    std::vector<Cand> cands;
    for (int dz = -r; dz <= r; dz += 2)
        for (int dx = -r; dx <= r; dx += 2) {
            int x = cx + dx, z = cz + dz;
            if (!cellWalkable(x, z) || !(m_flags[index(x, z)] & NearCover)) continue;
            glm::vec2 p = center(x, z);
            float d = glm::length(p - around);
            if (d > radius) continue;
            glm::vec2 toThreat = threat - p;
            float tl = glm::length(toThreat);
            if (tl < 4.0f) continue;
            glm::vec2 dir = toThreat / tl;
            // Look one to three cells toward the threat for a tall-enough blocker.
            float best = 0.0f;
            for (int k = 1; k <= 3; ++k) {
                int bx, bz;
                if (!toCell(p + dir * (m_cell * k), bx, bz)) break;
                if (!(m_flags[index(bx, bz)] & Walkable)) {
                    best = m_height[index(bx, bz)] / 10.0f;
                    break;
                }
            }
            if (best < 0.9f) continue;
            cands.push_back({d, {p, best}});
        }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
    for (const Cand& c : cands) {
        out.push_back(c.c);
        if (int(out.size()) >= maxResults) break;
    }
}
