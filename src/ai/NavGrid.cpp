#include "ai/NavGrid.h"

#include "physics/World.h"
#include "world/MapData.h"
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
    m_terrain = terrain;
    m_floors.clear();
    m_linkAdj.clear();
    m_linkSlot.assign(total, -1);
}

bool NavGrid::Floor::inside(const glm::vec2& p) const {
    return p.x >= origin.x && p.y >= origin.y && p.x < origin.x + nx * 0.5f && p.y < origin.y + nz * 0.5f;
}

void NavGrid::addLevels(const World& world, const MapData& map) {
    if (!valid()) return;
    int next = m_n * m_n;
    std::vector<int> ov;
    for (const NavFloor& nf : map.floors) {
        Floor f;
        f.origin = nf.min;
        f.nx = std::max(1, int(std::ceil((nf.max.x - nf.min.x) / m_cell)));
        f.nz = std::max(1, int(std::ceil((nf.max.y - nf.min.y) / m_cell)));
        f.y = nf.y;
        f.offset = next;
        f.flags.assign(size_t(f.nx) * f.nz, 0);
        for (int z = 0; z < f.nz; ++z)
            for (int x = 0; x < f.nx; ++x) {
                glm::vec2 c = f.origin + glm::vec2((x + 0.5f) * m_cell, (z + 0.5f) * m_cell);
                // Needs a floor under it and nothing in the way between the step height and head height.
                bool support = false, blocked = false;
                AABB probe;
                probe.min = glm::vec3(c.x - kRadius, f.y - 0.15f, c.y - kRadius);
                probe.max = glm::vec3(c.x + kRadius, f.y + kHeadroom, c.y + kRadius);
                world.overlap(probe, ov);
                for (int ci : ov) {
                    const Collider& col = world.collider(ci);
                    if (!col.blocksPlayer || !col.active || col.dynamic || col.owner != kOwnerWorld) continue;
                    AABB b = col.box.bounds();
                    bool under = c.x >= b.min.x && c.x <= b.max.x && c.y >= b.min.z && c.y <= b.max.z;
                    if (under && b.max.y >= f.y - 0.12f && b.max.y <= f.y + 0.12f) support = true;
                    if (b.max.y > f.y + kStep && b.min.y < f.y + kHeadroom) blocked = true;
                }
                if (support && !blocked) f.flags[size_t(z) * f.nx + x] = Walkable;
            }
        next += f.nx * f.nz;
        m_floors.push_back(std::move(f));
    }
    m_g.assign(size_t(next), 0.0f);
    m_parent.assign(size_t(next), -1);
    m_stamp.assign(size_t(next), 0);
    m_closed.assign(size_t(next), 0);
    m_linkSlot.assign(size_t(next), -1);
    m_linkAdj.clear();
    m_search = 0;
    auto addEdge = [&](int from, int to, float cost, const glm::vec3& a, const glm::vec3& b) {
        if (m_linkSlot[from] < 0) {
            m_linkSlot[from] = int(m_linkAdj.size());
            m_linkAdj.emplace_back();
        }
        m_linkAdj[m_linkSlot[from]].push_back({to, cost, a, b});
    };
    for (const NavLink& l : map.links) {
        int na, nb;
        if (!nearestNode(l.a, na) || !nearestNode(l.b, nb) || na == nb) continue;
        float cost = glm::length(l.b - l.a) / m_cell * 1.3f;
        addEdge(na, nb, cost, l.a, l.b);
        addEdge(nb, na, cost, l.b, l.a);
    }
}

int NavGrid::levelAt(const glm::vec3& p) const {
    int best = 0;
    float bestDy = 1.2f;
    for (int i = 0; i < int(m_floors.size()); ++i) {
        const Floor& f = m_floors[i];
        if (!f.inside({p.x, p.z})) continue;
        float dy = std::abs(p.y - f.y);
        if (dy < bestDy) {
            bestDy = dy;
            best = i + 1;
        }
    }
    return best;
}

float NavGrid::levelHeight(const glm::vec3& p) const {
    int l = levelAt(p);
    if (l > 0) return m_floors[l - 1].y;
    return m_terrain ? m_terrain->height(p.x, p.z) : 0.0f;
}

int NavGrid::nodeAt(const glm::vec3& p) const {
    int l = levelAt(p);
    if (l == 0) {
        int x, z;
        return toCell({p.x, p.z}, x, z) ? index(x, z) : -1;
    }
    const Floor& f = m_floors[l - 1];
    int x = int((p.x - f.origin.x) / m_cell), z = int((p.z - f.origin.y) / m_cell);
    if (x < 0 || z < 0 || x >= f.nx || z >= f.nz) return -1;
    return f.offset + z * f.nx + x;
}

bool NavGrid::nodeWalkable(int node) const {
    if (node < 0) return false;
    if (node < m_n * m_n) return (m_flags[node] & Walkable) != 0;
    for (const Floor& f : m_floors)
        if (node >= f.offset && node < f.offset + f.nx * f.nz) return (f.flags[node - f.offset] & Walkable) != 0;
    return false;
}

glm::vec3 NavGrid::nodePos(int node) const {
    if (node < m_n * m_n) {
        glm::vec2 c = center(node % m_n, node / m_n);
        return {c.x, m_terrain ? m_terrain->height(c.x, c.y) : 0.0f, c.y};
    }
    for (const Floor& f : m_floors)
        if (node >= f.offset && node < f.offset + f.nx * f.nz) {
            int j = node - f.offset;
            glm::vec2 c = f.origin + glm::vec2((j % f.nx + 0.5f) * m_cell, (j / f.nx + 0.5f) * m_cell);
            return {c.x, f.y, c.y};
        }
    return glm::vec3(0.0f);
}

bool NavGrid::nearestNode(const glm::vec3& p, int& node) const {
    int l = levelAt(p);
    if (l == 0) {
        glm::vec2 q;
        if (!nearestWalkable({p.x, p.z}, q)) return false;
        int x, z;
        toCell(q, x, z);
        node = index(x, z);
        return true;
    }
    const Floor& f = m_floors[l - 1];
    int cx = int((p.x - f.origin.x) / m_cell), cz = int((p.z - f.origin.y) / m_cell);
    for (int r = 0; r <= 8; ++r)
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                int x = cx + dx, z = cz + dz;
                if (x < 0 || z < 0 || x >= f.nx || z >= f.nz) continue;
                if (f.flags[size_t(z) * f.nx + x] & Walkable) {
                    node = f.offset + z * f.nx + x;
                    return true;
                }
            }
    return false;
}

bool NavGrid::walkable3(const glm::vec3& p) const { return nodeWalkable(nodeAt(p)); }

bool NavGrid::clearLine3(const glm::vec3& a, const glm::vec3& b) const {
    int la = levelAt(a);
    if (la == 0) return levelAt(b) == 0 && clearLine({a.x, a.z}, {b.x, b.z});
    const Floor& f = m_floors[la - 1];
    float len = glm::length(glm::vec2(b.x - a.x, b.z - a.z));
    int steps = std::max(1, int(len / (m_cell * 0.5f)));
    for (int i = 0; i <= steps; ++i) {
        glm::vec3 p = a + (b - a) * (float(i) / steps);
        p.y = f.y;
        int x = int((p.x - f.origin.x) / m_cell), z = int((p.z - f.origin.y) / m_cell);
        if (x < 0 || z < 0 || x >= f.nx || z >= f.nz || !(f.flags[size_t(z) * f.nx + x] & Walkable)) return false;
    }
    return true;
}

bool NavGrid::findPath3(const glm::vec3& from, const glm::vec3& to, std::vector<glm::vec3>& out, int maxExpansions) {
    out.clear();
    if (!valid()) return false;
    int la = levelAt(from), lb = levelAt(to);
    if (la == 0 && lb == 0) {
        std::vector<glm::vec2> p2;
        if (!findPath({from.x, from.z}, {to.x, to.z}, p2, maxExpansions)) return false;
        for (const glm::vec2& p : p2) out.push_back({p.x, m_terrain ? m_terrain->height(p.x, p.y) : 0.0f, p.y});
        return true;
    }
    int start, goal;
    if (!nearestNode(from, start) || !nearestNode(to, goal)) return false;
    if (la == lb && clearLine3(from, nodePos(goal))) {
        out.push_back(nodePos(goal));
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
    glm::vec3 gp = nodePos(goal);
    auto h = [&](const glm::vec3& p) {
        float dx = std::abs(p.x - gp.x) / m_cell, dz = std::abs(p.z - gp.z) / m_cell;
        return (std::max(dx, dz) + 0.4142f * std::min(dx, dz) + std::abs(p.y - gp.y) * 2.0f) * 1.5f;
    };
    m_g[start] = 0.0f;
    m_parent[start] = -1;
    m_stamp[start] = m_search;
    m_closed[start] = 0;
    open.push({h(nodePos(start)), start});
    int expansions = 0;
    bool found = false;
    const int ox[8] = {1, -1, 0, 0, 1, 1, -1, -1}, oz[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    auto relax = [&](int from, int ni, float step) {
        float ng = m_g[from] + step;
        if (m_stamp[ni] != m_search) {
            m_stamp[ni] = m_search;
            m_closed[ni] = 0;
            m_g[ni] = ng;
            m_parent[ni] = from;
            open.push({ng + h(nodePos(ni)), ni});
        } else if (!m_closed[ni] && ng < m_g[ni]) {
            m_g[ni] = ng;
            m_parent[ni] = from;
            open.push({ng + h(nodePos(ni)), ni});
        }
    };
    const int groundN = m_n * m_n;
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
        if (n.i < groundN) {
            int x = n.i % m_n, z = n.i / m_n;
            for (int k = 0; k < 8; ++k) {
                int nx = x + ox[k], nz = z + oz[k];
                if (!cellWalkable(nx, nz)) continue;
                if (k >= 4 && (!cellWalkable(x + ox[k], z) || !cellWalkable(x, z + oz[k]))) continue;
                int ni = index(nx, nz);
                float step = k < 4 ? 1.0f : 1.4142f;
                if (!(m_flags[ni] & NearCover)) step *= 1.25f;
                if (extraCost) {
                    glm::vec2 c = center(nx, nz);
                    step *= extraCost(c.x, c.y);
                }
                relax(n.i, ni, step);
            }
        } else {
            for (const Floor& f : m_floors) {
                if (n.i < f.offset || n.i >= f.offset + f.nx * f.nz) continue;
                int j = n.i - f.offset, x = j % f.nx, z = j / f.nx;
                auto ok = [&](int xx, int zz) { return xx >= 0 && zz >= 0 && xx < f.nx && zz < f.nz && (f.flags[size_t(zz) * f.nx + xx] & Walkable); };
                for (int k = 0; k < 8; ++k) {
                    int nx = x + ox[k], nz = z + oz[k];
                    if (!ok(nx, nz)) continue;
                    if (k >= 4 && (!ok(x + ox[k], z) || !ok(x, z + oz[k]))) continue;
                    relax(n.i, f.offset + nz * f.nx + nx, k < 4 ? 1.0f : 1.4142f);
                }
                break;
            }
        }
        if (m_linkSlot[n.i] >= 0)
            for (const LinkEdge& e : m_linkAdj[m_linkSlot[n.i]]) relax(n.i, e.to, e.cost);
    }
    m_lastExpansions = expansions;
    if (!found) return false;

    std::vector<int> nodes;
    for (int i = goal; i >= 0; i = m_parent[i]) {
        nodes.push_back(i);
        if (i == start) break;
    }
    std::reverse(nodes.begin(), nodes.end());
    // Waypoints: string-pull within each level; stairs are walked foot to head exactly.
    glm::vec3 cur = from;
    size_t i = 0;
    while (i < nodes.size()) {
        // Is there a stair from nodes[i] to nodes[i+1]?
        if (i + 1 < nodes.size() && m_linkSlot[nodes[i]] >= 0) {
            const LinkEdge* stair = nullptr;
            for (const LinkEdge& e : m_linkAdj[m_linkSlot[nodes[i]]])
                if (e.to == nodes[i + 1]) stair = &e;
            if (stair) {
                out.push_back(stair->from3);
                out.push_back(stair->to3);
                cur = stair->to3;
                i += 1;
                continue;
            }
        }
        // The next stair on the route: string-pull only up to its foot, then walk it exactly.
        size_t stairAt = nodes.size() - 1;
        for (size_t k = i; k + 1 < nodes.size(); ++k) {
            bool link = false;
            if (m_linkSlot[nodes[k]] >= 0)
                for (const LinkEdge& e : m_linkAdj[m_linkSlot[nodes[k]]])
                    if (e.to == nodes[k + 1]) link = true;
            if (link) {
                stairAt = k;
                break;
            }
        }
        size_t far = i;
        for (size_t j = std::min(stairAt, i + 200); j > i; --j)
            if (clearLine3(cur, nodePos(nodes[j]))) {
                far = j;
                break;
            }
        if (far == stairAt && stairAt + 1 < nodes.size()) {
            i = far; // handled as a stair next time round (its foot is the link's exact end)
            continue;
        }
        cur = nodePos(nodes[far]);
        out.push_back(cur);
        i = far + 1;
    }
    return true;
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
    auto hiddenEnough = [&](const glm::vec2& a, const glm::vec2& b) {
        if (!extraCost) return true;
        float limit = std::max(1.01f, extraCost(a.x, a.y) + 0.01f);
        float len = glm::length(b - a);
        int steps = std::max(1, int(len / 1.0f));
        for (int i = 1; i <= steps; ++i) {
            glm::vec2 p = a + (b - a) * (float(i) / steps);
            if (extraCost(p.x, p.y) > limit) return false;
        }
        return true;
    };
    if (clearLine(from, g) && hiddenEnough(from, g)) {
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
            if (extraCost) {
                glm::vec2 c = center(nx, nz);
                step *= extraCost(c.x, c.y);
            }
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
            if (clearLine(cur, cells[j]) && hiddenEnough(cur, cells[j])) {
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
