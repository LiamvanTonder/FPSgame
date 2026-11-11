#include "world/MapBuilder.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace {

const float kHalf = 400.0f;
const glm::vec3 kGrass(0.40f, 0.46f, 0.30f);
const glm::vec3 kForestFloor(0.30f, 0.34f, 0.21f);
const glm::vec3 kRoad(0.47f, 0.40f, 0.30f);
const glm::vec3 kField(0.60f, 0.54f, 0.32f);
const glm::vec3 kConcrete(0.60f, 0.60f, 0.58f);
const glm::vec3 kDarkConcrete(0.46f, 0.46f, 0.45f);
const glm::vec3 kBrick(0.55f, 0.33f, 0.25f);
const glm::vec3 kPlaster(0.75f, 0.71f, 0.62f);
const glm::vec3 kWood(0.50f, 0.36f, 0.22f);
const glm::vec3 kBarnRed(0.52f, 0.20f, 0.15f);
const glm::vec3 kRoof(0.30f, 0.32f, 0.34f);
const glm::vec3 kRoofTile(0.45f, 0.24f, 0.18f);
const glm::vec3 kMetalSheet(0.52f, 0.55f, 0.56f);
const glm::vec3 kMetalDark(0.25f, 0.26f, 0.27f);
const glm::vec3 kBark(0.33f, 0.25f, 0.18f);
const glm::vec3 kLeaves(0.22f, 0.34f, 0.15f);
const glm::vec3 kRock(0.48f, 0.48f, 0.46f);
const glm::vec3 kStone(0.55f, 0.53f, 0.49f);
const glm::vec3 kHay(0.75f, 0.64f, 0.35f);
const glm::vec3 kCrate(0.36f, 0.38f, 0.22f);

// Seeded value noise
struct Noise {
    unsigned seed;
    float hash(int x, int z) const {
        unsigned h = unsigned(x) * 374761393u + unsigned(z) * 668265263u + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return float(h ^ (h >> 16)) / 4294967295.0f;
    }
    float value(float x, float z) const {
        int ix = int(std::floor(x)), iz = int(std::floor(z));
        float fx = x - ix, fz = z - iz;
        float ux = fx * fx * (3 - 2 * fx), uz = fz * fz * (3 - 2 * fz);
        float a = hash(ix, iz), b = hash(ix + 1, iz), c = hash(ix, iz + 1), d = hash(ix + 1, iz + 1);
        return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uz;
    }
    float fbm(float x, float z, int oct) const {
        float s = 0, amp = 1, norm = 0;
        for (int i = 0; i < oct; ++i) {
            s += amp * (value(x, z) * 2.0f - 1.0f);
            norm += amp;
            amp *= 0.5f;
            x *= 2.03f;
            z *= 2.03f;
        }
        return s / norm;
    }
};

float smoothstep(float e0, float e1, float x) {
    float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

float segDist(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, glm::vec2* closest = nullptr) {
    glm::vec2 ab = b - a;
    float t = std::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
    glm::vec2 c = a + ab * t;
    if (closest) *closest = c;
    return glm::length(p - c);
}

} // namespace

// ------------------------------------------------------------------ primitives

int MapBuilder::collider(const OBB& box, Surface s, bool player, bool bullets, bool vision) {
    Collider c;
    c.box = box;
    c.surface = s;
    c.blocksPlayer = player;
    c.blocksBullets = bullets;
    c.blocksVision = vision;
    return m_world->add(c);
}

void MapBuilder::visual(const OBB& box, const glm::vec3& color, Pattern p, SceneBlock::Shape shape) {
    SceneBlock b;
    b.box = box;
    b.color = color;
    b.pattern = p;
    b.shape = shape;
    m_map->blocks.push_back(b);
}

int MapBuilder::solid(const glm::vec3& mn, const glm::vec3& mx, const glm::vec3& color, Surface s, Pattern p) {
    OBB box = OBB::fromMinMax(glm::min(mn, mx), glm::max(mn, mx));
    visual(box, color, p);
    return collider(box, s);
}

void MapBuilder::wall(const glm::vec3& start, int dir, float length, float t, float height, const std::vector<Opening>& openings,
                      const glm::vec3& color, Surface s, Pattern p) {
    auto piece = [&](float a, float b, float y0, float y1) {
        if (b - a < 0.02f || y1 - y0 < 0.02f) return;
        glm::vec3 mn = dir == 0 ? glm::vec3(start.x + a, start.y + y0, start.z) : glm::vec3(start.x, start.y + y0, start.z + a);
        glm::vec3 mx = dir == 0 ? glm::vec3(start.x + b, start.y + y1, start.z + t) : glm::vec3(start.x + t, start.y + y1, start.z + b);
        solid(mn, mx, color, s, p);
    };
    std::vector<Opening> ops = openings;
    std::sort(ops.begin(), ops.end(), [](const Opening& x, const Opening& y) { return x.offset < y.offset; });
    float cursor = 0.0f;
    for (const Opening& o : ops) {
        float a = std::clamp(o.offset - o.width * 0.5f, 0.0f, length), b = std::clamp(o.offset + o.width * 0.5f, 0.0f, length);
        piece(cursor, a, 0.0f, height);
        piece(a, b, 0.0f, o.bottom);
        piece(a, b, o.top, height);
        cursor = b;
    }
    piece(cursor, length, 0.0f, height);
}

float MapBuilder::footprintHeight(const glm::vec2& c, float hw, float hd) const {
    float h = -1e9f;
    for (float dx : {-hw, 0.0f, hw})
        for (float dz : {-hd, 0.0f, hd}) h = std::max(h, ground(c.x + dx, c.y + dz));
    return h;
}

void MapBuilder::reserve(const glm::vec2& c, const glm::vec2& half) { m_reserved.push_back(glm::vec4(c, half)); }

bool MapBuilder::blocked(const glm::vec2& p, float r) const {
    for (const glm::vec4& f : m_reserved)
        if (std::abs(p.x - f.x) < f.z + r && std::abs(p.y - f.y) < f.w + r) return true;
    for (const Road& rd : m_roads)
        if (segDist(p, rd.a, rd.b) < rd.width * 0.5f + r) return true;
    return false;
}

void MapBuilder::addWallDoors(const std::vector<Opening>& ops, const glm::vec3& start, int dir, float t, const glm::vec2& outward) {
    for (const Opening& o : ops) {
        if (o.bottom > 0.01f) continue; // windows
        glm::vec2 c = dir == 0 ? glm::vec2(start.x + o.offset, start.z + t * 0.5f) : glm::vec2(start.x + t * 0.5f, start.z + o.offset);
        m_doors.push_back({c, outward});
    }
}

void MapBuilder::recordEntrances(float floorY) {
    for (const Door& d : m_doors) {
        if (glm::length(d.normal) < 0.5f) continue;
        Entrance e;
        glm::vec2 out = d.center + d.normal * 1.8f, in = d.center - d.normal * 1.6f;
        e.outside = glm::vec3(out.x, ground(out.x, out.y), out.y);
        e.inside = glm::vec3(in.x, floorY, in.y);
        m_map->entrances.push_back(e);
    }
}

bool MapBuilder::doorClear(const glm::vec2& p, float radius) const {
    for (const Door& d : m_doors)
        if (glm::length(p - d.center) < 1.5f + radius) return false;
    return true;
}

void MapBuilder::container(ContainerType type, const glm::vec3& p, float yaw, int tier, int hotspot) {
    ContainerSpawn c;
    c.type = type;
    c.position = p;
    c.yaw = yaw;
    c.tier = tier;
    c.hotspot = hotspot;
    m_map->containers.push_back(c);
}

// ------------------------------------------------------------------ structures

void MapBuilder::house(const glm::vec2& c, int front, float w, float d, int tier, int hotspot) {
    // front: 0 faces -Z, 1 +X, 2 +Z, 3 -X. Footprint w along X, d along Z.
    const float t = 0.25f, h = 2.8f;
    float floorY = footprintHeight(c, w * 0.5f, d * 0.5f) + 0.15f;
    bool brick = u01() < 0.5f;
    glm::vec3 col = brick ? kBrick : (u01() < 0.5f ? kPlaster : kWood);
    Surface surf = brick || col == kPlaster ? Surface::Concrete : Surface::Wood;
    Pattern pat = brick ? Pattern::Bricks : (col == kWood ? Pattern::Planks : Pattern::Plain);
    float x0 = c.x - w * 0.5f, z0 = c.y - d * 0.5f;

    // Foundation and floor
    solid({x0 - 0.1f, floorY - 1.2f, z0 - 0.1f}, {x0 + w + 0.1f, floorY, z0 + d + 0.1f}, kDarkConcrete, Surface::Concrete);
    solid({x0, floorY, z0}, {x0 + w, floorY + 0.04f, z0 + d}, kWood, Surface::Wood, Pattern::Planks);
    float fy = floorY + 0.04f;

    auto door = [](float off) { return Opening{off, 1.7f, 0.0f, 2.15f}; };
    auto window = [](float off) { return Opening{off, 1.3f, 1.0f, 2.0f}; };
    // Walls: north (z0), south (z0+d) along X; west (x0), east (x0+w) along Z.
    std::vector<Opening> north{window(w * 0.25f), window(w * 0.75f)}, south = north;
    std::vector<Opening> west{window(d * 0.5f)}, east = west;
    float doorOff = (u01() < 0.5f ? 0.35f : 0.6f);
    switch (front) {
    case 0: north = {door(w * doorOff), window(w * (doorOff < 0.5f ? 0.78f : 0.22f))}; south.push_back(door(w * 0.5f)); break;
    case 2: south = {door(w * doorOff), window(w * (doorOff < 0.5f ? 0.78f : 0.22f))}; north.push_back(door(w * 0.5f)); break;
    case 1: east = {door(d * doorOff), window(d * (doorOff < 0.5f ? 0.8f : 0.2f))}; break;
    default: west = {door(d * doorOff), window(d * (doorOff < 0.5f ? 0.8f : 0.2f))}; break;
    }
    // Remove windows that collide with doors on the same wall
    auto tidy = [](std::vector<Opening>& v) {
        for (size_t i = 0; i < v.size(); ++i)
            for (size_t j = i + 1; j < v.size();)
                if (std::abs(v[i].offset - v[j].offset) < (v[i].width + v[j].width) * 0.5f + 0.4f) v.erase(v.begin() + j);
                else ++j;
    };
    tidy(north);
    tidy(south);
    tidy(east);
    tidy(west);
    wall({x0, fy, z0}, 0, w, t, h, north, col, surf, pat);
    wall({x0, fy, z0 + d - t}, 0, w, t, h, south, col, surf, pat);
    wall({x0, fy, z0 + t}, 1, d - 2 * t, t, h, west, col, surf, pat);
    wall({x0 + w - t, fy, z0 + t}, 1, d - 2 * t, t, h, east, col, surf, pat);
    m_doors.clear();
    addWallDoors(north, {x0, fy, z0}, 0, t, {0.0f, -1.0f});
    addWallDoors(south, {x0, fy, z0 + d - t}, 0, t, {0.0f, 1.0f});
    addWallDoors(west, {x0, fy, z0 + t}, 1, t, {-1.0f, 0.0f});
    addWallDoors(east, {x0 + w - t, fy, z0 + t}, 1, t, {1.0f, 0.0f});
    if (w >= d) m_doors.push_back({{x0 + w * 0.45f + 0.06f, z0 + t + d * 0.3f}, {0.0f, 0.0f}});
    else m_doors.push_back({{x0 + t + w * 0.3f, z0 + d * 0.45f + 0.06f}, {0.0f, 0.0f}});
    recordEntrances(fy);
    // Interior partition across the longer side with a doorway
    if (w >= d) wall({x0 + w * 0.45f, fy, z0 + t}, 1, d - 2 * t, 0.12f, h, {Opening{d * 0.3f, 1.5f, 0.0f, 2.1f}}, kPlaster, Surface::Wood, Pattern::Plain);
    else wall({x0 + t, fy, z0 + d * 0.45f}, 0, w - 2 * t, 0.12f, h, {Opening{w * 0.3f, 1.5f, 0.0f, 2.1f}}, kPlaster, Surface::Wood, Pattern::Plain);

    // Pitched roof, ridge along the longer side
    float top = fy + h;
    const float a = 0.52f; // ~30 degrees
    bool ridgeX = w >= d;
    float span = (ridgeX ? d : w) * 0.5f + 0.4f;
    float slabHalf = span / std::cos(a) * 0.5f;
    for (int side = -1; side <= 1; side += 2) {
        OBB slab;
        float rise = span * 0.5f * std::tan(a);
        if (ridgeX) {
            slab.center = glm::vec3(c.x, top + rise, c.y + side * span * 0.5f);
            slab.half = glm::vec3(w * 0.5f + 0.35f, 0.07f, slabHalf);
            slab.rotation = glm::mat3_cast(glm::angleAxis(side < 0 ? -a : a, glm::vec3(1, 0, 0)));
        } else {
            slab.center = glm::vec3(c.x + side * span * 0.5f, top + rise, c.y);
            slab.half = glm::vec3(slabHalf, 0.07f, d * 0.5f + 0.35f);
            slab.rotation = glm::mat3_cast(glm::angleAxis(side < 0 ? a : -a, glm::vec3(0, 0, 1)));
        }
        visual(slab, kRoofTile);
        collider(slab, Surface::Wood);
    }
    // Gables: stepped boxes filling the triangle under the ridge
    for (int g = 0; g < 2; ++g) {
        for (int k = 0; k < 3; ++k) {
            float fr = 1.0f - (k + 0.5f) / 3.0f;
            float hh = span * std::tan(a) / 3.0f;
            if (ridgeX) {
                float x = g == 0 ? x0 : x0 + w - t;
                solid({x, top + hh * k, c.y - (d * 0.5f) * fr}, {x + t, top + hh * (k + 1), c.y + (d * 0.5f) * fr}, col, surf, pat);
            } else {
                float z = g == 0 ? z0 : z0 + d - t;
                solid({c.x - (w * 0.5f) * fr, top + hh * k, z}, {c.x + (w * 0.5f) * fr, top + hh * (k + 1), z + t}, col, surf, pat);
            }
        }
    }
    // Ceiling (stops bullets from above being silly and makes interiors dark)
    visual(OBB::fromMinMax({x0 + t, top - 0.05f, z0 + t}, {x0 + w - t, top, z0 + d - t}), kPlaster);

    // Furniture and loot, never in a doorway (candidate spots tried in order).
    auto inside = [&](float fx, float fz) { return glm::vec2(x0 + t + 0.3f + fx * (w - 2 * t - 0.6f), z0 + t + 0.3f + fz * (d - 2 * t - 0.6f)); };
    float partition = w >= d ? x0 + w * 0.45f : z0 + d * 0.45f;
    auto offPartition = [&](const glm::vec2& p, float r) {
        float v = w >= d ? p.x : p.y;
        return std::abs(v - partition) > r + 0.2f;
    };
    auto place = [&](std::initializer_list<glm::vec2> spots, float radius, glm::vec2& out) {
        for (const glm::vec2& f : spots) {
            glm::vec2 q = inside(f.x, f.y);
            if (doorClear(q, radius) && offPartition(q, radius)) {
                out = q;
                return true;
            }
        }
        return false;
    };
    glm::vec2 tbl;
    if (place({{0.25f, 0.5f}, {0.75f, 0.5f}, {0.2f, 0.3f}, {0.8f, 0.7f}}, 0.75f, tbl)) {
        solid({tbl.x - 0.6f, fy + 0.72f, tbl.y - 0.4f}, {tbl.x + 0.6f, fy + 0.77f, tbl.y + 0.4f}, kWood, Surface::Wood);
        for (float dx : {-0.5f, 0.5f})
            for (float dz : {-0.3f, 0.3f}) solid({tbl.x + dx - 0.03f, fy, tbl.y + dz - 0.03f}, {tbl.x + dx + 0.03f, fy + 0.72f, tbl.y + dz + 0.03f}, kWood, Surface::Wood);
    }
    glm::vec2 shelf;
    if (place({{0.92f, 0.05f}, {0.08f, 0.05f}, {0.92f, 0.95f}, {0.08f, 0.95f}}, 0.6f, shelf))
        solid({shelf.x - 0.5f, fy, shelf.y - 0.2f}, {shelf.x + 0.5f, fy + 1.8f, shelf.y + 0.2f}, kWood, Surface::Wood, Pattern::Planks);
    glm::vec2 bed;
    if (place({{0.85f, 0.85f}, {0.15f, 0.85f}, {0.85f, 0.15f}, {0.15f, 0.15f}}, 0.7f, bed) && glm::length(bed - shelf) > 1.5f)
        solid({bed.x - 0.55f, fy, bed.y - 0.45f}, {bed.x + 0.55f, fy + 0.5f, bed.y + 0.45f}, glm::vec3(0.45f, 0.42f, 0.38f), Surface::Wood);

    ContainerType types[] = {ContainerType::Duffel, ContainerType::MedCabinet, ContainerType::Toolbox, ContainerType::AmmoBox};
    int n = 1 + (u01() < 0.45f ? 1 : 0);
    std::vector<glm::vec2> used = {tbl, shelf, bed};
    const glm::vec2 spots[] = {{0.05f, 0.95f}, {0.95f, 0.5f}, {0.5f, 0.95f}, {0.05f, 0.5f}, {0.5f, 0.05f}, {0.95f, 0.05f}};
    for (int i = 0, k = 0; i < n && k < 6; ++k) {
        glm::vec2 q = inside(spots[k].x, spots[k].y);
        bool free = doorClear(q, 0.5f) && offPartition(q, 0.5f);
        for (const glm::vec2& u : used) free = free && glm::length(q - u) > 1.2f;
        if (!free) continue;
        used.push_back(q);
        container(types[int(u01() * 4) % 4], glm::vec3(q.x, fy, q.y), (k % 2) ? 1.5708f : 0.0f, tier, hotspot);
        ++i;
    }
    reserve(c, glm::vec2(w * 0.5f + 1.5f, d * 0.5f + 1.5f));
    m_map->mapRects.push_back({c, glm::vec2(w, d) * 0.5f, 0.0f, glm::vec3(0.45f, 0.35f, 0.30f)});
    (void)front;
}

void MapBuilder::bigBuilding(const glm::vec2& c, int front, float w, float d, float h, const glm::vec3& col, Surface s, Pattern p,
                             float doorW, float doorH, int crates, int tier, int hotspot, bool racks, bool mezz) {
    const float t = 0.3f;
    float floorY = footprintHeight(c, w * 0.5f, d * 0.5f) + 0.1f;
    float x0 = c.x - w * 0.5f, z0 = c.y - d * 0.5f;
    solid({x0 - 0.2f, floorY - 1.5f, z0 - 0.2f}, {x0 + w + 0.2f, floorY, z0 + d + 0.2f}, kDarkConcrete, Surface::Concrete);
    float fy = floorY;
    Opening big{0, doorW, 0.0f, doorH}, small{0, 1.6f, 0.0f, 2.2f}, win{0, 2.0f, 2.2f, 3.2f};
    auto at = [](Opening o, float off) {
        o.offset = off;
        return o;
    };
    std::vector<Opening> north, south, west, east;
    if (front == 0 || front == 2) {
        north = {at(big, w * 0.5f)};
        south = {at(big, w * 0.5f)};
        west = {at(small, d * 0.3f), at(win, d * 0.75f)};
        east = {at(small, d * 0.7f), at(win, d * 0.25f)};
    } else {
        west = {at(big, d * 0.5f)};
        east = {at(big, d * 0.5f)};
        north = {at(small, w * 0.3f), at(win, w * 0.75f)};
        south = {at(small, w * 0.7f), at(win, w * 0.25f)};
    }
    if (doorW < 1.9f) {
        // Small building (armoury): one door only, no windows on the back.
        north = front == 0 ? std::vector<Opening>{at(small, w * 0.5f)} : std::vector<Opening>{};
        south = front == 2 ? std::vector<Opening>{at(small, w * 0.5f)} : std::vector<Opening>{};
        west = front == 3 ? std::vector<Opening>{at(small, d * 0.5f)} : std::vector<Opening>{at(win, d * 0.5f)};
        east = front == 1 ? std::vector<Opening>{at(small, d * 0.5f)} : std::vector<Opening>{};
    }
    wall({x0, fy, z0}, 0, w, t, h, north, col, s, p);
    wall({x0, fy, z0 + d - t}, 0, w, t, h, south, col, s, p);
    wall({x0, fy, z0 + t}, 1, d - 2 * t, t, h, west, col, s, p);
    wall({x0 + w - t, fy, z0 + t}, 1, d - 2 * t, t, h, east, col, s, p);
    m_doors.clear();
    addWallDoors(north, {x0, fy, z0}, 0, t, {0.0f, -1.0f});
    addWallDoors(south, {x0, fy, z0 + d - t}, 0, t, {0.0f, 1.0f});
    addWallDoors(west, {x0, fy, z0 + t}, 1, t, {-1.0f, 0.0f});
    addWallDoors(east, {x0 + w - t, fy, z0 + t}, 1, t, {1.0f, 0.0f});
    recordEntrances(fy);
    solid({x0 - 0.3f, fy + h, z0 - 0.3f}, {x0 + w + 0.3f, fy + h + 0.15f, z0 + d + 0.3f}, kRoof, Surface::Metal);
    if (mezz && h >= 5.5f) mezzanine(x0, z0, w, fy, t, std::min(3.4f, h - 2.3f));

    if (racks) {
        // Rows of shelving with aisles
        bool alongX = w >= d;
        float len = (alongX ? w : d) - 6.0f;
        int rows = int(((alongX ? d : w) - 6.0f) / 4.0f);
        for (int r = 0; r < rows; ++r) {
            float off = -((alongX ? d : w) * 0.5f - 3.0f) + r * 4.0f + 2.0f;
            if (mezz && alongX && off < -((d * 0.5f) - 4.6f)) continue; // keep clear of the walkway
            for (int seg = 0; seg < 2; ++seg) {
                float a = -len * 0.5f + seg * (len * 0.5f + 1.0f), b = a + len * 0.5f - 1.0f;
                if (alongX) solid({c.x + a, fy, c.y + off - 0.5f}, {c.x + b, fy + 2.6f, c.y + off + 0.5f}, kMetalDark, Surface::Metal);
                else solid({c.x + off - 0.5f, fy, c.y + a}, {c.x + off + 0.5f, fy + 2.6f, c.y + b}, kMetalDark, Surface::Metal);
            }
        }
    }
    // Crates (cover) and loot
    for (int i = 0; i < crates; ++i) {
        glm::vec2 p(x0 + 1.5f + u01() * (w - 3.0f), z0 + 1.5f + u01() * (d - 3.0f));
        if (racks && i % 2 == 0) p = glm::vec2(x0 + 1.2f, z0 + 1.2f + (d - 2.4f) * float(i) / std::max(1, crates));
        for (int tries = 0; tries < 24 && !doorClear(p, 0.8f); ++tries)
            p = glm::vec2(x0 + 1.2f + u01() * (w - 2.4f), z0 + 1.2f + u01() * (d - 2.4f));
        if (!doorClear(p, 0.8f)) continue;
        bool weaponCrate = hotspot >= 0 && i < 3;
        container(weaponCrate ? ContainerType::WeaponCrate : (i % 3 == 0 ? ContainerType::AmmoBox : ContainerType::Duffel),
                  glm::vec3(p.x, fy, p.y), (u01() < 0.5f ? 0.0f : 1.57f), tier, hotspot);
    }
    reserve(c, glm::vec2(w * 0.5f + 2.0f, d * 0.5f + 2.0f));
    m_map->mapRects.push_back({c, glm::vec2(w, d) * 0.5f, 0.0f, col * 0.8f});
}

void MapBuilder::stairs(const glm::vec3& foot, const glm::vec2& dir, float width, float top, const glm::vec3& color) {
    // Risers low enough that a body spanning two treads can still step up both.
    const float riser = 0.16f, tread = 0.28f;
    int n = std::max(1, int(std::ceil((top - foot.y) / riser - 0.01f)));
    float r = (top - foot.y) / n;
    glm::vec2 side(-dir.y, dir.x);
    for (int i = 0; i < n; ++i) {
        glm::vec2 a = glm::vec2(foot.x, foot.z) + dir * (tread * i), b = a + dir * tread;
        glm::vec2 s0 = a - side * (width * 0.5f), s1 = b + side * (width * 0.5f);
        solid({std::min(s0.x, s1.x), foot.y - 0.3f, std::min(s0.y, s1.y)}, {std::max(s0.x, s1.x), foot.y + r * (i + 1), std::max(s0.y, s1.y)},
              color * (i % 2 ? 0.95f : 1.0f), Surface::Wood, Pattern::Planks);
    }
}

void MapBuilder::twoStorey(const glm::vec2& c, int front, float w, float d, const glm::vec3& col, Surface surf, Pattern pat, int tier,
                           int hotspot, bool balcony, bool roofAccess) {
    // front: 0 faces -Z, 2 faces +Z (the stair runs along the west wall, rising toward +Z).
    const float t = 0.25f, h1 = 3.0f, slab = 0.2f, h2 = 2.8f;
    float floorY = footprintHeight(c, w * 0.5f, d * 0.5f) + 0.15f;
    float x0 = c.x - w * 0.5f, z0 = c.y - d * 0.5f, x1 = x0 + w, z1 = z0 + d;
    solid({x0 - 0.1f, floorY - 1.2f, z0 - 0.1f}, {x1 + 0.1f, floorY, z1 + 0.1f}, kDarkConcrete, Surface::Concrete);
    solid({x0, floorY, z0}, {x1, floorY + 0.04f, z1}, kWood, Surface::Wood, Pattern::Planks);
    float fy = floorY + 0.04f, y2 = fy + h1 + slab, y3 = y2 + h2;

    auto door = [](float off) { return Opening{off, 1.7f, 0.0f, 2.15f}; };
    auto window = [](float off) { return Opening{off, 1.3f, 1.0f, 2.0f}; };
    // Ground floor: front door, back door, windows; the stair hugs the west wall.
    std::vector<Opening> north{window(w * 0.3f), window(w * 0.75f)}, south = north, west{}, east{window(d * 0.3f), window(d * 0.75f)};
    if (front == 0) {
        north = {door(w * 0.62f), window(w * 0.85f)};
        south = {door(w * 0.55f), window(w * 0.82f)};
    } else {
        south = {door(w * 0.62f), window(w * 0.85f)};
        north = {door(w * 0.55f), window(w * 0.82f)};
    }
    wall({x0, fy, z0}, 0, w, t, h1, north, col, surf, pat);
    wall({x0, fy, z1 - t}, 0, w, t, h1, south, col, surf, pat);
    wall({x0, fy, z0 + t}, 1, d - 2 * t, t, h1, west, col, surf, pat);
    wall({x1 - t, fy, z0 + t}, 1, d - 2 * t, t, h1, east, col, surf, pat);
    m_doors.clear();
    addWallDoors(north, {x0, fy, z0}, 0, t, {0.0f, -1.0f});
    addWallDoors(south, {x0, fy, z1 - t}, 0, t, {0.0f, 1.0f});
    addWallDoors(east, {x1 - t, fy, z0 + t}, 1, t, {1.0f, 0.0f});
    recordEntrances(fy);

    // Stair: 1.1 m wide along the west wall, foot near the north end, rising toward +Z.
    const float sw = 1.1f, tread = 0.28f;
    int nSteps = int(std::ceil((y2 - fy) / 0.16f - 0.01f));
    float run = nSteps * tread;
    float sx0 = x0 + t, sx1 = sx0 + sw, scx = (sx0 + sx1) * 0.5f;
    float zs = z0 + t + 1.2f;
    stairs({scx, fy, zs}, {0.0f, 1.0f}, sw, y2, kWood);
    m_doors.push_back({{scx, zs - 0.5f}, {0.0f, 0.0f}});
    m_doors.push_back({{scx, zs + run * 0.5f}, {0.0f, 0.0f}});
    m_doors.push_back({{scx, zs + run + 0.6f}, {0.0f, 0.0f}});
    // Upper floor slab with the stairwell open above the flight.
    float hx = sx1 + 0.05f;
    solid({hx, y2 - slab, z0 + t}, {x1 - t, y2, z1 - t}, kWood, Surface::Wood, Pattern::Planks);
    solid({x0 + t, y2 - slab, zs + run}, {hx, y2, z1 - t}, kWood, Surface::Wood, Pattern::Planks);
    solid({x0 + t, y2 - slab, z0 + t}, {hx, y2, zs}, kWood, Surface::Wood, Pattern::Planks);
    // Railings round the stairwell
    solid({hx, y2, zs}, {hx + 0.06f, y2 + 1.0f, zs + run - 0.9f}, kWood * 0.8f, Surface::Wood);
    solid({x0 + t, y2, zs - 0.06f}, {hx + 0.06f, y2 + 1.0f, zs}, kWood * 0.8f, Surface::Wood);
    m_map->links.push_back({glm::vec3(scx, fy, zs - 0.6f), glm::vec3(scx, y2, zs + run + 0.55f)});
    m_map->entrances.push_back({glm::vec3(scx, fy, zs - 0.6f), glm::vec3(scx, y2, zs + run + 0.6f)});

    // Upper walls: windows all round (firing positions), a door onto the balcony.
    float bal = 1.4f;
    std::vector<Opening> n2{window(w * 0.3f), window(w * 0.72f)}, s2 = n2, w2{window(d * 0.7f)}, e2{window(d * 0.3f), window(d * 0.72f)};
    if (balcony) {
        Opening bd{w * 0.62f, 1.2f, 0.0f, 2.15f};
        if (front == 0) n2 = {window(w * 0.3f), bd};
        else s2 = {window(w * 0.3f), bd};
    }
    wall({x0, y2, z0}, 0, w, t, h2, n2, col, surf, pat);
    wall({x0, y2, z1 - t}, 0, w, t, h2, s2, col, surf, pat);
    wall({x0, y2, z0 + t}, 1, d - 2 * t, t, h2, w2, col, surf, pat);
    if (roofAccess) e2.clear();
    wall({x1 - t, y2, z0 + t}, 1, d - 2 * t, t, h2, e2, col, surf, pat);
    auto spots = [&](const std::vector<Opening>& ops, int dir, const glm::vec3& start, const glm::vec2& out) {
        for (const Opening& o : ops) {
            if (o.bottom < 0.01f) continue;
            glm::vec2 q = dir == 0 ? glm::vec2(start.x + o.offset, start.z + t * 0.5f) : glm::vec2(start.x + t * 0.5f, start.z + o.offset);
            q -= out * 0.75f;
            m_map->firingSpots.push_back({glm::vec3(q.x, y2, q.y), out});
        }
    };
    spots(n2, 0, {x0, y2, z0}, {0.0f, -1.0f});
    spots(s2, 0, {x0, y2, z1 - t}, {0.0f, 1.0f});
    spots(w2, 1, {x0, y2, z0 + t}, {-1.0f, 0.0f});
    spots(e2, 1, {x1 - t, y2, z0 + t}, {1.0f, 0.0f});
    NavFloor upper{{x0, z0}, {x1, z1}, y2};
    if (balcony) {
        // Slab and railing outside the balcony door.
        float bz0 = front == 0 ? z0 - bal : z1, bz1 = front == 0 ? z0 : z1 + bal;
        float bx0 = x0 + w * 0.62f - 1.6f, bx1 = x0 + w * 0.62f + 1.6f;
        solid({bx0, y2 - slab, bz0}, {bx1, y2, bz1}, kDarkConcrete, Surface::Concrete);
        float rz = front == 0 ? bz0 : bz1 - 0.06f;
        solid({bx0, y2, rz}, {bx1, y2 + 1.0f, rz + 0.06f}, kMetalDark, Surface::Metal);
        solid({bx0, y2, bz0}, {bx0 + 0.06f, y2 + 1.0f, bz1}, kMetalDark, Surface::Metal);
        solid({bx1 - 0.06f, y2, bz0}, {bx1, y2 + 1.0f, bz1}, kMetalDark, Surface::Metal);
        if (front == 0) upper.min.y = bz0;
        else upper.max.y = bz1;
        m_map->firingSpots.push_back({glm::vec3(x0 + w * 0.62f, y2, front == 0 ? z0 - bal * 0.5f : z1 + bal * 0.5f), front == 0 ? glm::vec2(0, -1) : glm::vec2(0, 1)});
    }
    m_map->floors.push_back(upper);

    // Flat roof with a parapet. With roof access, a second flight along the east wall climbs to a hatch.
    float ry = y3 + 0.2f;
    bool access = roofAccess;
    if (access) {
        int n2 = int(std::ceil((ry - y2) / 0.16f - 0.01f));
        float run2 = n2 * tread;
        float foot = z1 - t - 1.2f, scx2 = x1 - t - sw * 0.5f, hx0 = x1 - t - sw - 0.05f;
        float hz0 = foot - run2, hz1 = foot;
        stairs({scx2, y2, foot}, {0.0f, -1.0f}, sw, ry, kWood);
        solid({x0 - 0.15f, y3, z0 - 0.15f}, {hx0, ry, z1 + 0.15f}, kRoof, Surface::Concrete);
        solid({hx0, y3, z0 - 0.15f}, {x1 + 0.15f, ry, hz0}, kRoof, Surface::Concrete);
        solid({hx0, y3, hz1}, {x1 + 0.15f, ry, z1 + 0.15f}, kRoof, Surface::Concrete);
        // Hatch rails on the roof
        solid({hx0 - 0.06f, ry, hz0 + 0.9f}, {hx0, ry + 1.0f, hz1}, kMetalDark, Surface::Metal);
        solid({hx0 - 0.06f, ry, hz1}, {x1 - t, ry + 1.0f, hz1 + 0.06f}, kMetalDark, Surface::Metal);
        m_map->links.push_back({glm::vec3(scx2, y2, foot + 0.6f), glm::vec3(scx2, ry, hz0 - 0.55f)});
        m_map->entrances.push_back({glm::vec3(scx2, y2, foot + 0.6f), glm::vec3(scx2, ry, hz0 - 0.6f)});
        m_doors.push_back({{scx2, foot + 0.5f}, {0.0f, 0.0f}});
        m_doors.push_back({{scx2, (hz0 + hz1) * 0.5f}, {0.0f, 0.0f}});
        m_doors.push_back({{scx2, hz0 - 0.6f}, {0.0f, 0.0f}});
    } else {
        solid({x0 - 0.15f, y3, z0 - 0.15f}, {x1 + 0.15f, ry, z1 + 0.15f}, kRoof, Surface::Concrete);
    }
    solid({x0 - 0.15f, ry, z0 - 0.15f}, {x1 + 0.15f, ry + 0.9f, z0 + 0.05f}, col, surf, pat);
    solid({x0 - 0.15f, ry, z1 - 0.05f}, {x1 + 0.15f, ry + 0.9f, z1 + 0.15f}, col, surf, pat);
    solid({x0 - 0.15f, ry, z0 + 0.05f}, {x0 + 0.05f, ry + 0.9f, z1 - 0.05f}, col, surf, pat);
    solid({x1 - 0.05f, ry, z0 + 0.05f}, {x1 + 0.15f, ry + 0.9f, z1 - 0.05f}, col, surf, pat);
    if (access) m_map->floors.push_back({{x0, z0}, {x1, z1}, ry});
    if (access)
        for (const glm::vec2& cpt : {glm::vec2(x0 + 0.8f, z0 + 0.8f), glm::vec2(x0 + 0.8f, z1 - 0.8f), glm::vec2(x0 + w * 0.5f, z0 + 0.8f)})
            m_map->firingSpots.push_back({glm::vec3(cpt.x, ry, cpt.y), glm::normalize(cpt - c)});

    // Furniture and loot on both floors, clear of doors and the stair.
    auto clearOf = [&](const glm::vec2& p, float r) { return doorClear(p, r) && !(p.x < sx1 + 0.6f + r && p.y > zs - 1.4f && p.y < zs + run + 1.2f); };
    auto inside = [&](float fx, float fz) { return glm::vec2(x0 + t + 0.4f + fx * (w - 2 * t - 0.8f), z0 + t + 0.4f + fz * (d - 2 * t - 0.8f)); };
    glm::vec2 tbl = inside(0.65f, 0.5f);
    if (clearOf(tbl, 0.8f)) {
        solid({tbl.x - 0.6f, fy + 0.72f, tbl.y - 0.4f}, {tbl.x + 0.6f, fy + 0.77f, tbl.y + 0.4f}, kWood, Surface::Wood);
        for (float dx : {-0.5f, 0.5f})
            for (float dz : {-0.3f, 0.3f}) solid({tbl.x + dx - 0.03f, fy, tbl.y + dz - 0.03f}, {tbl.x + dx + 0.03f, fy + 0.72f, tbl.y + dz + 0.03f}, kWood, Surface::Wood);
    }
    ContainerType types[] = {ContainerType::Duffel, ContainerType::MedCabinet, ContainerType::Toolbox, ContainerType::AmmoBox};
    const glm::vec2 groundSpots[] = {{0.95f, 0.06f}, {0.95f, 0.94f}, {0.5f, 0.94f}};
    int placed = 0;
    for (const glm::vec2& f : groundSpots) {
        glm::vec2 q = inside(f.x, f.y);
        if (placed >= 1 || !clearOf(q, 0.5f)) continue;
        container(types[int(u01() * 4) % 4], glm::vec3(q.x, fy, q.y), 0.0f, tier, hotspot);
        ++placed;
    }
    // Upstairs: a bed, a shelf, a container, and spots for loot-zone extras.
    glm::vec2 bed = inside(0.8f, 0.2f), shelf = roofAccess ? inside(0.6f, 0.97f) : inside(0.95f, 0.85f), box = inside(0.55f, 0.85f);
    solid({bed.x - 0.55f, y2, bed.y - 0.45f}, {bed.x + 0.55f, y2 + 0.5f, bed.y + 0.45f}, glm::vec3(0.45f, 0.42f, 0.38f), Surface::Wood);
    solid({shelf.x - 0.2f, y2, shelf.y - 0.5f}, {shelf.x + 0.2f, y2 + 1.8f, shelf.y + 0.5f}, kWood, Surface::Wood, Pattern::Planks);
    container(types[int(u01() * 4) % 4], glm::vec3(box.x, y2, box.y), 0.0f, tier + 1, hotspot);
    m_lootSpots.push_back({glm::vec3(inside(0.45f, 0.15f).x, y2, inside(0.45f, 0.15f).y), m_area});
    if (access) m_lootSpots.push_back({glm::vec3(x0 + w * 0.5f, ry, c.y), m_area});
    reserve(c, glm::vec2(w * 0.5f + 1.5f, d * 0.5f + 1.5f + (balcony ? bal : 0.0f)));
    m_map->mapRects.push_back({c, glm::vec2(w, d) * 0.5f, 0.0f, glm::vec3(0.50f, 0.38f, 0.30f)});
}

void MapBuilder::mezzanine(float x0, float z0, float w, float fy, float t, float hm) {
    // Walkway 2.6 m deep along the north wall from 55 % of the width to 3 m short of the east wall.
    // The north wall's small door is at 30 % of the width: the stair (which ends where the walkway
    // starts) must finish clear of it.
    int nSteps = int(std::ceil(hm / 0.16f - 0.01f));
    float a = std::max(x0 + w * 0.55f, x0 + w * 0.3f + 1.6f + nSteps * 0.28f), b = x0 + w - t - 0.2f, depth = 2.6f;
    if (b - a < 4.0f) return;
    float zi = z0 + t, zo = zi + depth;
    float top = fy + hm;
    solid({a, top - 0.2f, zi}, {b, top, zo}, kMetalDark, Surface::Metal, Pattern::Grid);
    for (float x = a + 0.3f; x < b; x += 4.0f) solid({x - 0.1f, fy, zo - 0.25f}, {x + 0.1f, top - 0.2f, zo - 0.05f}, kMetalDark, Surface::Metal);
    solid({a, top, zo - 0.06f}, {b, top + 1.0f, zo}, kMetalDark, Surface::Metal); // railing
    solid({a, top, zi + 1.25f}, {a + 0.06f, top + 1.0f, zo}, kMetalDark, Surface::Metal); // west end, beside the stair head
    solid({b - 0.06f, top, zi}, {b, top + 1.0f, zo}, kMetalDark, Surface::Metal);
    // Stair along the wall, rising toward +X up to the walkway's west end.
    int n = int(std::ceil(hm / 0.16f - 0.01f));
    float run = n * 0.28f;
    float cz = zi + 0.6f;
    stairs({a - run, fy, cz}, {1.0f, 0.0f}, 1.1f, top, kMetalDark * 1.5f);
    m_doors.push_back({{a - run - 0.6f, cz}, {0.0f, 0.0f}});
    m_doors.push_back({{a - run * 0.5f, cz}, {0.0f, 0.0f}});
    m_doors.push_back({{a - run * 0.5f, zo}, {0.0f, 0.0f}});
    for (float x = a + 0.5f; x < b; x += 3.0f) m_doors.push_back({{x, zo + 0.4f}, {0.0f, 0.0f}}); // keep crates out from under it
    m_map->links.push_back({glm::vec3(a - run - 0.6f, fy, cz), glm::vec3(a + 0.6f, top, cz)});
    m_map->entrances.push_back({glm::vec3(a - run - 0.6f, fy, cz), glm::vec3(a + 0.6f, top, cz)});
    m_map->floors.push_back({{a, zi}, {b, zo}, top});
    m_map->firingSpots.push_back({glm::vec3((a + b) * 0.5f, top, zo - 0.5f), glm::vec2(0.0f, 1.0f)});
    m_lootSpots.push_back({glm::vec3(b - 1.2f, top, zi + 1.0f), m_area});
    m_lootSpots.push_back({glm::vec3((a + b) * 0.5f, top, zi + 0.9f), m_area});
}

void MapBuilder::coverCluster(const glm::vec2& c, float radius, int count) {
    const glm::vec3 kSand(0.62f, 0.56f, 0.40f), kBarrier(0.66f, 0.66f, 0.62f);
    for (int i = 0; i < count; ++i) {
        glm::vec2 p = c + glm::vec2(u11(), u11()) * radius;
        if (blocked(p, 1.8f)) continue;
        float y = ground(p.x, p.y);
        bool alongX = u01() < 0.5f;
        float k = u01();
        if (k < 0.35f) {
            // Sandbag wall, chest-high when crouched
            float L = 2.2f + 1.4f * u01();
            glm::vec3 half = alongX ? glm::vec3(L * 0.5f, 0.55f, 0.35f) : glm::vec3(0.35f, 0.55f, L * 0.5f);
            solid(glm::vec3(p.x, y - 0.2f, p.y) - glm::vec3(half.x, 0, half.z), glm::vec3(p.x, y + 1.05f, p.y) + glm::vec3(half.x, 0, half.z), kSand, Surface::Dirt);
            reserve(p, glm::vec2(half.x, half.z) + glm::vec2(0.6f));
        } else if (k < 0.6f) {
            // Concrete barriers in a row
            for (int j = 0; j < 3; ++j) {
                glm::vec2 q = p + (alongX ? glm::vec2(j * 2.1f, 0) : glm::vec2(0, j * 2.1f));
                if (blocked(q, 1.0f)) break;
                float yy = ground(q.x, q.y);
                glm::vec3 half = alongX ? glm::vec3(1.0f, 0.0f, 0.3f) : glm::vec3(0.3f, 0.0f, 1.0f);
                solid(glm::vec3(q.x, yy - 0.2f, q.y) - half, glm::vec3(q.x, yy + 0.95f, q.y) + half, kBarrier, Surface::Concrete);
            }
            reserve(p + (alongX ? glm::vec2(2.1f, 0) : glm::vec2(0, 2.1f)), alongX ? glm::vec2(3.8f, 1.0f) : glm::vec2(1.0f, 3.8f));
        } else if (k < 0.85f) {
            // Stacked crates (one may be on top)
            float s = 0.6f + 0.2f * u01();
            solid({p.x - s, y, p.y - s}, {p.x + s, y + 2 * s, p.y + s}, kCrate, Surface::Wood, Pattern::Planks);
            if (u01() < 0.5f) solid({p.x - s * 0.8f, y + 2 * s, p.y - s * 0.8f}, {p.x + s * 0.8f, y + 3.6f * s, p.y + s * 0.8f}, kCrate * 0.9f, Surface::Wood, Pattern::Planks);
            reserve(p, glm::vec2(s + 0.6f));
        } else {
            car(p, alongX);
            reserve(p, alongX ? glm::vec2(3.0f, 1.7f) : glm::vec2(1.7f, 3.0f));
        }
    }
}

void MapBuilder::shed(const glm::vec2& c, int front, float w, float d, int tier, int hotspot) {
    bigBuilding(c, front, w, d, 2.6f, kWood, Surface::Wood, Pattern::Planks, 1.3f, 2.2f, 1, tier, hotspot, false);
}

void MapBuilder::shippingContainer(const glm::vec2& c, float yawRight, int level, const glm::vec3& color) {
    float y = (level == 0 ? footprintHeight(c, 3.0f, 3.0f) : 0.0f) + level * 2.6f;
    bool alongX = yawRight > 0.5f;
    glm::vec3 half = alongX ? glm::vec3(3.05f, 1.3f, 1.22f) : glm::vec3(1.22f, 1.3f, 3.05f);
    glm::vec3 centre(c.x, y + 1.3f, c.y);
    if (level > 0) centre.y = footprintHeight(c, 3.0f, 3.0f) + 2.6f + 1.3f;
    solid(centre - half, centre + half, color, Surface::Metal, Pattern::Grid);
    if (level == 0) reserve(c, glm::vec2(half.x, half.z) + glm::vec2(1.0f));
}

void MapBuilder::car(const glm::vec2& c, bool alongX) {
    float y = ground(c.x, c.y);
    glm::vec3 col[] = {{0.55f, 0.15f, 0.12f}, {0.20f, 0.30f, 0.45f}, {0.60f, 0.60f, 0.58f}, {0.25f, 0.30f, 0.20f}};
    glm::vec3 k = col[int(u01() * 4) % 4];
    glm::vec3 body = alongX ? glm::vec3(2.2f, 0.45f, 0.9f) : glm::vec3(0.9f, 0.45f, 2.2f);
    glm::vec3 cab = alongX ? glm::vec3(1.1f, 0.35f, 0.82f) : glm::vec3(0.82f, 0.35f, 1.1f);
    solid(glm::vec3(c.x, y + 0.25f, c.y) - glm::vec3(body.x, 0.0f, body.z), glm::vec3(c.x, y + 0.25f, c.y) + glm::vec3(body.x, 0.9f, body.z), k,
          Surface::Metal);
    solid(glm::vec3(c.x, y + 1.15f, c.y) - glm::vec3(cab.x, 0.0f, cab.z), glm::vec3(c.x, y + 1.15f, c.y) + glm::vec3(cab.x, 0.7f, cab.z),
          k * 0.8f, Surface::Metal);
}

void MapBuilder::tree(const glm::vec2& p, float s) {
    float y = ground(p.x, p.y);
    float trunkH = (6.0f + 6.0f * u01()) * s, r = (0.18f + 0.15f * u01()) * s;
    OBB trunk = OBB::fromCenterSize(glm::vec3(p.x, y + trunkH * 0.5f - 0.3f, p.y), glm::vec3(r * 2.0f, trunkH + 0.6f, r * 2.0f));
    visual(trunk, kBark, Pattern::Plain, SceneBlock::Cylinder);
    collider(trunk, Surface::Wood);
    bool conifer = u01() < 0.5f;
    int crowns = conifer ? 3 : 2;
    for (int i = 0; i < crowns; ++i) {
        float cr = conifer ? (2.6f - 0.7f * i) * s : (2.6f + 0.8f * u01()) * s;
        float cy = conifer ? y + trunkH * (0.45f + 0.22f * i) : y + trunkH * 0.75f + i * 1.2f * s;
        glm::vec2 off = conifer ? glm::vec2(0.0f) : glm::vec2(u11(), u11()) * 0.8f;
        glm::vec3 size(cr * 2.0f, conifer ? cr * 1.6f : cr * 1.5f, cr * 2.0f);
        OBB crown = OBB::fromCenterSize(glm::vec3(p.x + off.x, cy, p.y + off.y), size);
        glm::vec3 col = kLeaves * (0.8f + 0.4f * u01());
        visual(crown, col, Pattern::Foliage, SceneBlock::Sphere);
        // Leaves hide people but don't stop bullets or bodies. The box is shrunk to the dense core.
        OBB core = crown;
        core.half *= 0.7f;
        Collider& c = m_world->collider(collider(core, Surface::Foliage, false, false, true));
        (void)c;
    }
}

void MapBuilder::bush(const glm::vec2& p, float s) {
    float y = ground(p.x, p.y);
    glm::vec3 size(1.6f * s + u01(), 1.1f * s + 0.4f * u01(), 1.6f * s + u01());
    OBB b = OBB::fromCenterSize(glm::vec3(p.x, y + size.y * 0.45f, p.y), size);
    visual(b, kLeaves * (0.9f + 0.3f * u01()), Pattern::Foliage, SceneBlock::Sphere);
    OBB core = b;
    core.half *= 0.8f;
    collider(core, Surface::Foliage, false, false, true);
}

void MapBuilder::rock(const glm::vec2& p, float size) {
    float y = ground(p.x, p.y);
    OBB b;
    b.center = glm::vec3(p.x, y + size * 0.3f, p.y);
    b.half = glm::vec3(size * (0.6f + 0.4f * u01()), size * (0.5f + 0.3f * u01()), size * (0.6f + 0.4f * u01()));
    b.rotation = glm::mat3_cast(glm::angleAxis(u01() * 3.14f, glm::vec3(0, 1, 0)) * glm::angleAxis(u11() * 0.15f, glm::vec3(1, 0, 0)));
    visual(b, kRock * (0.85f + 0.3f * u01()));
    collider(b, Surface::Concrete);
}

void MapBuilder::fenceLine(const glm::vec2& a, const glm::vec2& b, float gapAt, float gapWidth) {
    float len = glm::length(b - a);
    glm::vec2 dir = (b - a) / len;
    int posts = int(len / 3.0f);
    for (int i = 0; i <= posts; ++i) {
        float s = len * float(i) / posts;
        if (gapAt >= 0.0f && std::abs(s - gapAt) < gapWidth * 0.5f) continue;
        glm::vec2 p = a + dir * s;
        float y = ground(p.x, p.y);
        solid({p.x - 0.05f, y - 0.3f, p.y - 0.05f}, {p.x + 0.05f, y + 2.2f, p.y + 0.05f}, kMetalDark, Surface::Metal);
    }
    // Chain-link panels: see-through and shoot-through, but you can't walk through.
    auto panel = [&](float s0, float s1) {
        if (s1 - s0 < 0.3f) return;
        glm::vec2 p0 = a + dir * s0, p1 = a + dir * s1, m = (p0 + p1) * 0.5f;
        float y = std::min(ground(p0.x, p0.y), ground(p1.x, p1.y));
        float yTop = std::max(ground(p0.x, p0.y), ground(p1.x, p1.y)) + 2.1f;
        OBB box = OBB::fromCenterSize(glm::vec3(m.x, (y + yTop) * 0.5f, m.y),
                                      std::abs(dir.x) > 0.5f ? glm::vec3(s1 - s0, yTop - y, 0.05f) : glm::vec3(0.05f, yTop - y, s1 - s0));
        Collider c;
        c.box = box;
        c.surface = Surface::Metal;
        c.blocksBullets = false;
        c.blocksVision = false;
        m_world->add(c);
        // Top and middle rails
        for (float ry : {yTop - 0.05f, y + 1.1f}) {
            OBB rail = OBB::fromCenterSize(glm::vec3(m.x, ry, m.y), std::abs(dir.x) > 0.5f ? glm::vec3(s1 - s0, 0.04f, 0.04f) : glm::vec3(0.04f, 0.04f, s1 - s0));
            visual(rail, kMetalDark);
        }
    };
    if (gapAt >= 0.0f) {
        panel(0.0f, gapAt - gapWidth * 0.5f);
        panel(gapAt + gapWidth * 0.5f, len);
    } else {
        panel(0.0f, len);
    }
}

void MapBuilder::stoneWall(const glm::vec2& a, const glm::vec2& b, float gapAt) {
    float len = glm::length(b - a);
    glm::vec2 dir = (b - a) / len;
    const float seg = 4.0f;
    for (float s = 0.0f; s < len; s += seg) {
        float e = std::min(len, s + seg);
        float mid = (s + e) * 0.5f;
        if (gapAt >= 0.0f && std::abs(mid - gapAt) < 3.0f) continue;
        if (u01() < 0.08f) continue; // tumbled sections
        glm::vec2 p = a + dir * mid;
        float y = ground(p.x, p.y);
        float hgt = 0.85f + 0.2f * u01();
        glm::vec3 size = std::abs(dir.x) > 0.5f ? glm::vec3(e - s, hgt + 0.4f, 0.6f) : glm::vec3(0.6f, hgt + 0.4f, e - s);
        OBB box = OBB::fromCenterSize(glm::vec3(p.x, y + (hgt - 0.4f) * 0.5f, p.y), size);
        visual(box, kStone * (0.9f + 0.2f * u01()), Pattern::Bricks);
        collider(box, Surface::Concrete);
    }
}

void MapBuilder::extraction(const std::string& name, const glm::vec2& p) {
    ExtractPoint e;
    e.name = name;
    float y = ground(p.x, p.y);
    e.position = glm::vec3(p.x, y, p.y);
    e.radius = 8.0f;
    // Flare pole with a red marker on top
    OBB pole = OBB::fromCenterSize(glm::vec3(p.x, y + 3.0f, p.y), glm::vec3(0.12f, 6.0f, 0.12f));
    visual(pole, kMetalDark, Pattern::Plain, SceneBlock::Cylinder);
    collider(pole, Surface::Metal);
    visual(OBB::fromCenterSize(glm::vec3(p.x, y + 6.15f, p.y), glm::vec3(0.35f, 0.3f, 0.35f)), glm::vec3(0.9f, 0.15f, 0.1f), Pattern::Unlit);
    // Sandbag ring (partial) for cover
    for (int i = 0; i < 5; ++i) {
        float a = float(i) / 7.0f * 6.283f + 0.5f;
        glm::vec2 q = p + glm::vec2(std::cos(a), std::sin(a)) * 5.0f;
        float gy = ground(q.x, q.y);
        OBB bag;
        bag.center = glm::vec3(q.x, gy + 0.45f, q.y);
        bag.half = glm::vec3(1.0f, 0.5f, 0.35f);
        bag.rotation = glm::mat3_cast(glm::angleAxis(-a + 1.5708f, glm::vec3(0, 1, 0)));
        visual(bag, glm::vec3(0.55f, 0.50f, 0.38f));
        collider(bag, Surface::Dirt);
    }
    // Send-back uplink crate
    glm::vec2 boxP = p + glm::vec2(2.5f, -1.5f);
    float by = ground(boxP.x, boxP.y);
    solid({boxP.x - 0.6f, by, boxP.y - 0.45f}, {boxP.x + 0.6f, by + 0.8f, boxP.y + 0.45f}, glm::vec3(0.2f, 0.35f, 0.25f), Surface::Metal);
    visual(OBB::fromCenterSize(glm::vec3(boxP.x, by + 1.3f, boxP.y), glm::vec3(0.05f, 1.0f, 0.05f)), kMetalDark);
    e.dropBox = glm::vec3(boxP.x, by + 0.8f, boxP.y);
    m_map->extracts.push_back(e);
    reserve(p, glm::vec2(7.0f));
}

// ------------------------------------------------------------------ areas

void MapBuilder::village() {
    glm::vec2 v = m_village;
    int hall = int(m_map->hotspots.size());
    m_map->hotspots.push_back({"Town hall", glm::vec3(v.x - 28.0f, 0, v.y - 28.0f), 25.0f});
    m_map->hotspots.back().position.y = ground(v.x - 28.0f, v.y - 28.0f);
    bigBuilding(v + glm::vec2(-28.0f, -28.0f), 2, 16.0f, 12.0f, 4.2f, kPlaster, Surface::Concrete, Pattern::Plain, 2.0f, 2.6f, 6, 2, hall, false);

    const float mainOffsets[] = {-66.0f, -42.0f, 34.0f, 58.0f};
    for (float x : mainOffsets)
        for (int side = -1; side <= 1; side += 2) {
            float w = 8.0f + 2.0f * u01(), d = 7.0f + 2.0f * u01();
            if ((x == -42.0f && side < 0) || (x == 34.0f && side > 0)) {
                twoStorey(v + glm::vec2(x, side * 15.0f), side < 0 ? 2 : 0, 10.0f, 8.5f, x < 0 ? kBrick : kPlaster, Surface::Concrete,
                          x < 0 ? Pattern::Bricks : Pattern::Plain, 1, -1, true, false);
                continue;
            }
            house(v + glm::vec2(x, side * 14.0f), side < 0 ? 2 : 0, w, d, 0, -1);
        }
    const float crossOffsets[] = {-58.0f, 30.0f, 54.0f, 78.0f};
    for (float z : crossOffsets)
        for (int side = -1; side <= 1; side += 2) {
            if (z < 0 && side < 0) continue; // town hall corner
            float w = 7.0f + 2.0f * u01(), d = 8.0f + 2.0f * u01();
            house(v + glm::vec2(side * 14.0f, z), side < 0 ? 1 : 3, w, d, 0, -1);
        }
    // Square with a fountain, parked cars along the streets
    float fy = ground(v.x, v.y);
    OBB fountain = OBB::fromCenterSize(glm::vec3(v.x, fy + 0.4f, v.y), glm::vec3(4.0f, 0.9f, 4.0f));
    visual(fountain, kStone, Pattern::Plain, SceneBlock::Cylinder);
    collider(OBB::fromCenterSize(glm::vec3(v.x, fy + 0.4f, v.y), glm::vec3(3.2f, 0.9f, 3.2f)), Surface::Concrete);
    for (int i = 0; i < 6; ++i) {
        bool alongX = i % 2 == 0;
        float off = (u01() * 2.0f - 1.0f) * 70.0f;
        if (std::abs(off) < 10.0f) off += 20.0f;
        car(alongX ? v + glm::vec2(off, (u01() < 0.5f ? -4.5f : 4.5f)) : v + glm::vec2((u01() < 0.5f ? -4.5f : 4.5f), off), alongX);
    }
    coverCluster(v + glm::vec2(12.0f, 12.0f), 10.0f, 5);
    coverCluster(v + glm::vec2(-14.0f, -12.0f), 10.0f, 4);
    m_lootSpots.push_back({glm::vec3(v.x + 6.0f, ground(v.x + 6.0f, v.y + 6.0f), v.y + 6.0f), m_area});
    m_map->labels.push_back({"Village", v, glm::vec2(180.0f, 160.0f), glm::vec3(0.5f, 0.35f, 0.3f)});
}

void MapBuilder::industrial() {
    glm::vec2 c = m_industrial;
    // Yard fence with gates to the west and north roads
    glm::vec2 h(85.0f, 65.0f);
    fenceLine(c + glm::vec2(-h.x, -h.y), c + glm::vec2(h.x, -h.y), h.x, 10.0f);
    fenceLine(c + glm::vec2(-h.x, h.y), c + glm::vec2(h.x, h.y), h.x * 0.5f, 8.0f);
    fenceLine(c + glm::vec2(-h.x, -h.y), c + glm::vec2(-h.x, h.y), h.y, 10.0f);
    fenceLine(c + glm::vec2(h.x, -h.y), c + glm::vec2(h.x, h.y), h.y * 1.4f, 8.0f);

    bigBuilding(c + glm::vec2(-35.0f, -15.0f), 1, 36.0f, 24.0f, 7.0f, kMetalSheet, Surface::Metal, Pattern::Grid, 5.0f, 4.5f, 6, 1, -1, true, true);
    bigBuilding(c + glm::vec2(32.0f, 28.0f), 0, 30.0f, 20.0f, 6.5f, kMetalSheet * 0.9f, Surface::Metal, Pattern::Grid, 5.0f, 4.5f, 5, 1, -1, true);
    int armoury = int(m_map->hotspots.size());
    glm::vec2 ap = c + glm::vec2(45.0f, -38.0f);
    m_map->hotspots.push_back({"Armoury", glm::vec3(ap.x, ground(ap.x, ap.y), ap.y), 20.0f});
    bigBuilding(ap, 3, 10.0f, 7.0f, 3.2f, kDarkConcrete, Surface::Concrete, Pattern::Plain, 1.3f, 2.2f, 5, 3, armoury, false);
    // Office: two floors and a roof reached by an outside stair
    twoStorey(c + glm::vec2(2.0f, -45.0f), 2, 12.0f, 10.0f, kConcrete, Surface::Concrete, Pattern::Plain, 1, -1, false, true);
    // Container stacks
    const glm::vec3 boxCols[] = {{0.55f, 0.22f, 0.15f}, {0.18f, 0.32f, 0.50f}, {0.25f, 0.42f, 0.25f}, {0.60f, 0.48f, 0.20f}};
    for (int row = 0; row < 3; ++row)
        for (int k = 0; k < 4; ++k) {
            if (u01() < 0.2f) continue;
            glm::vec2 p = c + glm::vec2(-60.0f + k * 7.5f, 28.0f + row * 10.0f);
            shippingContainer(p, 0.0f, 0, boxCols[(row + k) % 4]);
            if (u01() < 0.35f) shippingContainer(p, 0.0f, 1, boxCols[(row + k + 1) % 4]);
        }
    for (int k = 0; k < 4; ++k) shippingContainer(c + glm::vec2(10.0f + k * 3.2f, -12.0f + (k % 2) * 7.0f), 1.0f, 0, boxCols[k]);
    // Water tower
    glm::vec2 wt = c + glm::vec2(-68.0f, 50.0f);
    float wy = ground(wt.x, wt.y);
    for (float dx : {-2.0f, 2.0f})
        for (float dz : {-2.0f, 2.0f}) solid({wt.x + dx - 0.15f, wy - 0.5f, wt.y + dz - 0.15f}, {wt.x + dx + 0.15f, wy + 12.0f, wt.y + dz + 0.15f}, kMetalDark, Surface::Metal);
    OBB tank = OBB::fromCenterSize(glm::vec3(wt.x, wy + 14.5f, wt.y), glm::vec3(6.0f, 5.0f, 6.0f));
    visual(tank, kMetalSheet, Pattern::Plain, SceneBlock::Cylinder);
    collider(OBB::fromCenterSize(tank.center, glm::vec3(5.0f, 5.0f, 5.0f)), Surface::Metal);
    reserve(wt, glm::vec2(4.0f));
    // Pallets and crates around the yard (cover)
    for (int i = 0; i < 14; ++i) {
        glm::vec2 p = c + glm::vec2(u11() * 75.0f, u11() * 55.0f);
        if (blocked(p, 2.0f)) continue;
        float y = ground(p.x, p.y);
        float s = 0.8f + 0.6f * u01();
        solid({p.x - s, y, p.y - s * 0.7f}, {p.x + s, y + s * 1.2f, p.y + s * 0.7f}, kCrate, Surface::Wood, Pattern::Planks);
        if (u01() < 0.3f) container(ContainerType::Toolbox, glm::vec3(p.x + s + 0.4f, y, p.y), 0.0f, 1, -1);
    }
    coverCluster(c + glm::vec2(5.0f, 5.0f), 22.0f, 9);
    m_map->labels.push_back({"Industrial yard", c, h * 2.0f, glm::vec3(0.45f, 0.47f, 0.5f)});
}

void MapBuilder::farm() {
    glm::vec2 f = m_farm;
    twoStorey(f + glm::vec2(-25.0f, -15.0f), 0, 10.0f, 8.5f, kPlaster, Surface::Concrete, Pattern::Plain, 0, -1, true, false);
    int barn = int(m_map->hotspots.size());
    glm::vec2 bp = f + glm::vec2(15.0f, 5.0f);
    m_map->hotspots.push_back({"Barn cache", glm::vec3(bp.x, ground(bp.x, bp.y), bp.y), 22.0f});
    bigBuilding(bp, 1, 22.0f, 14.0f, 6.0f, kBarnRed, Surface::Wood, Pattern::Planks, 4.0f, 4.0f, 6, 2, barn, false, true);
    shed(f + glm::vec2(10.0f, -30.0f), 3, 6.0f, 5.0f, 0, -1);
    // Hay bales
    for (int i = 0; i < 18; ++i) {
        glm::vec2 p = f + glm::vec2(u11() * 45.0f, u11() * 45.0f);
        if (blocked(p, 1.5f)) continue;
        float y = ground(p.x, p.y);
        OBB bale;
        bale.center = glm::vec3(p.x, y + 0.7f, p.y);
        bale.half = glm::vec3(0.75f, 0.75f, 0.6f);
        bale.rotation = glm::mat3_cast(glm::angleAxis(u01() * 3.14f, glm::vec3(0, 1, 0)) * glm::angleAxis(1.5708f, glm::vec3(1, 0, 0)));
        visual(bale, kHay, Pattern::Plain, SceneBlock::Cylinder);
        collider(OBB{bale.center, glm::vec3(0.75f, 0.6f, 0.75f), bale.rotation}, Surface::Wood);
    }
    car(f + glm::vec2(-8.0f, 18.0f), true);
    // Fields with low stone walls
    struct Field { glm::vec2 c, h; };
    const Field fields[] = {{f + glm::vec2(80.0f, 10.0f), {30.0f, 25.0f}}, {f + glm::vec2(-10.0f, 70.0f), {35.0f, 20.0f}},
                            {f + glm::vec2(75.0f, 70.0f), {25.0f, 22.0f}}};
    for (const Field& fl : fields) {
        glm::vec2 a = fl.c - fl.h, b = fl.c + fl.h;
        stoneWall({a.x, a.y}, {b.x, a.y}, fl.h.x);
        stoneWall({a.x, b.y}, {b.x, b.y}, -1.0f);
        stoneWall({a.x, a.y}, {a.x, b.y}, fl.h.y);
        stoneWall({b.x, a.y}, {b.x, b.y}, -1.0f);
        m_map->mapRects.push_back({fl.c, fl.h, 0.0f, glm::vec3(0.62f, 0.56f, 0.34f)});
    }
    coverCluster(f + glm::vec2(-5.0f, 22.0f), 12.0f, 5);
    m_map->labels.push_back({"Farm", f, glm::vec2(140.0f), glm::vec3(0.6f, 0.5f, 0.3f)});
}

void MapBuilder::radioTower() {
    glm::vec2 r = m_radio;
    int hs = int(m_map->hotspots.size());
    float y = ground(r.x, r.y);
    m_map->hotspots.push_back({"Radio tower", glm::vec3(r.x, y, r.y), 25.0f});
    // Lattice tower: four slanted legs and a few braces
    const float H = 38.0f, base = 3.0f, top = 0.7f;
    for (int i = 0; i < 4; ++i) {
        glm::vec2 s(i & 1 ? base : -base, i & 2 ? base : -base), e(i & 1 ? top : -top, i & 2 ? top : -top);
        glm::vec3 a(r.x + s.x, y - 0.5f, r.y + s.y), b(r.x + e.x, y + H, r.y + e.y);
        glm::vec3 d = b - a;
        float len = glm::length(d);
        glm::vec3 ax = d / len;
        glm::vec3 xa = glm::normalize(glm::cross(ax, glm::vec3(0, 0, 1)));
        glm::vec3 za = glm::cross(xa, ax);
        OBB leg{(a + b) * 0.5f, glm::vec3(0.12f, len * 0.5f, 0.12f), glm::mat3(xa, ax, za)};
        visual(leg, glm::vec3(0.7f, 0.25f, 0.2f));
        collider(leg, Surface::Metal);
    }
    for (int k = 1; k < 6; ++k) {
        float hy = H * k / 6.0f;
        float w = base + (top - base) * (hy / H);
        for (int s = 0; s < 4; ++s) {
            glm::vec3 c(r.x, y + hy, r.y);
            OBB brace = s < 2 ? OBB::fromCenterSize(c + glm::vec3(0, 0, s == 0 ? -w : w), glm::vec3(w * 2.0f, 0.08f, 0.08f))
                              : OBB::fromCenterSize(c + glm::vec3(s == 2 ? -w : w, 0, 0), glm::vec3(0.08f, 0.08f, w * 2.0f));
            visual(brace, glm::vec3(0.85f, 0.85f, 0.82f));
        }
    }
    visual(OBB::fromCenterSize(glm::vec3(r.x, y + H + 0.4f, r.y), glm::vec3(0.5f)), glm::vec3(1.0f, 0.1f, 0.05f), Pattern::Unlit);
    reserve(r, glm::vec2(5.0f));
    twoStorey(r + glm::vec2(-12.0f, 11.0f), 0, 10.5f, 10.0f, kDarkConcrete, Surface::Concrete, Pattern::Plain, 2, hs, false, true);
    shed(r + glm::vec2(10.0f, 6.0f), 1, 6.0f, 4.0f, 2, hs);
    shed(r + glm::vec2(-9.0f, -8.0f), 3, 5.0f, 4.0f, 2, hs);
    solid(glm::vec3(r.x + 6.0f, y, r.y - 7.0f), glm::vec3(r.x + 8.0f, y + 1.4f, r.y - 5.8f), kMetalDark, Surface::Metal); // generator
    glm::vec2 h(22.0f, 22.0f);
    fenceLine(r + glm::vec2(-h.x, -h.y), r + glm::vec2(h.x, -h.y), -1.0f);
    fenceLine(r + glm::vec2(-h.x, h.y), r + glm::vec2(h.x, h.y), h.x, 6.0f);
    fenceLine(r + glm::vec2(-h.x, -h.y), r + glm::vec2(-h.x, h.y), h.y, 6.0f);
    fenceLine(r + glm::vec2(h.x, -h.y), r + glm::vec2(h.x, h.y), -1.0f);
    coverCluster(r + glm::vec2(8.0f, -12.0f), 6.0f, 3);
    m_map->labels.push_back({"Radio tower", r, glm::vec2(50.0f), glm::vec3(0.6f, 0.3f, 0.3f)});
}

void MapBuilder::woods() {
    struct Patch { glm::vec2 c; float r; };
    const Patch patches[] = {{{-40.0f, -90.0f}, 85.0f}, {{45.0f, 75.0f}, 55.0f}, {{-310.0f, 40.0f}, 70.0f},
                             {{320.0f, 70.0f}, 50.0f},  {{-120.0f, 300.0f}, 60.0f}, {{60.0f, -300.0f}, 70.0f},
                             {{-250.0f, -320.0f}, 50.0f}, {{280.0f, 300.0f}, 55.0f}};
    for (const Patch& p : patches) {
        glm::vec2 c = p.c + glm::vec2(u11(), u11()) * 10.0f;
        int n = int(p.r * p.r * 3.14159f / 70.0f);
        for (int i = 0; i < n; ++i) {
            float a = u01() * 6.2832f, d = std::sqrt(u01()) * p.r;
            glm::vec2 q = c + glm::vec2(std::cos(a), std::sin(a)) * d;
            if (std::abs(q.x) > kHalf - 15.0f || std::abs(q.y) > kHalf - 15.0f || blocked(q, 1.5f)) continue;
            if (u01() < 0.75f) tree(q, 0.8f + 0.5f * u01());
            else bush(q, 0.8f + 0.5f * u01());
        }
        m_map->labels.push_back({"Woods", c, glm::vec2(p.r * 2.0f), glm::vec3(0.25f, 0.38f, 0.2f)});
        m_map->woods.push_back(glm::vec3(c, p.r));
        m_map->mapRects.push_back({c, glm::vec2(p.r * 0.8f), 0.0f, glm::vec3(0.30f, 0.42f, 0.25f)});
    }
    // Scattered trees, bushes and rocks in the open
    for (int i = 0; i < 260; ++i) {
        glm::vec2 q(u11() * (kHalf - 20.0f), u11() * (kHalf - 20.0f));
        if (blocked(q, 3.0f)) continue;
        float k = u01();
        if (k < 0.35f) tree(q, 0.9f + 0.5f * u01());
        else if (k < 0.75f) bush(q, 0.7f + 0.6f * u01());
        else rock(q, 0.8f + 1.4f * u01());
    }
}

// ------------------------------------------------------------------ main

glm::vec2 MapBuilder::rotate(const glm::vec2& v, int rot) const {
    switch (rot & 3) {
    case 1: return {-v.y, v.x};
    case 2: return {-v.x, -v.y};
    case 3: return {v.y, -v.x};
    default: return v;
    }
}

void MapBuilder::build(unsigned seed, World& world, Terrain& terrain, MapData& map, std::vector<glm::vec3>& colors) {
    m_world = &world;
    m_terrain = &terrain;
    m_map = &map;
    m_rng.seed(seed);
    m_roads.clear();
    m_zones.clear();
    m_reserved.clear();
    map.clear();
    map.seed = seed;
    map.halfSize = kHalf;
    map.hasTerrain = true;

    Noise noise{seed};
    auto jitter = [&](glm::vec2 p, float r) { return p + glm::vec2(u11(), u11()) * r; };
    m_village = jitter({-150.0f, 110.0f}, 15.0f);
    m_industrial = jitter({190.0f, -140.0f}, 15.0f);
    m_farm = jitter({175.0f, 210.0f}, 15.0f);
    m_radio = jitter({-230.0f, -215.0f}, 15.0f);
    glm::vec2 extracts[4] = {jitter({20.0f, -378.0f}, 25.0f), jitter({378.0f, 10.0f}, 25.0f), jitter({-60.0f, 378.0f}, 25.0f),
                             jitter({-378.0f, -130.0f}, 25.0f)};

    // ---------- terrain
    const int n = 411;
    const float cell = 2.0f;
    terrain.create(n, cell, glm::vec2(-410.0f));
    auto rawHeight = [&](float x, float z) {
        return 14.0f * noise.fbm(x * 0.0055f + 11.3f, z * 0.0055f - 7.1f, 3) + 3.5f * noise.fbm(x * 0.02f, z * 0.02f, 3);
    };
    m_zones = {{m_village, 110.0f, 0}, {m_industrial, 115.0f, 0}, {m_farm, 95.0f, 0}, {m_radio, 32.0f, 0}};
    for (Zone& z : m_zones) z.baseHeight = rawHeight(z.c.x, z.c.y);
    m_zones[3].baseHeight += 9.0f; // the tower stands on a hill
    for (int i = 0; i < 4; ++i) m_zones.push_back({extracts[i], 12.0f, rawHeight(extracts[i].x, extracts[i].y)});

    // Roads: zones linked to each other and to the extracts
    auto road = [&](glm::vec2 a, glm::vec2 b, float w) { m_roads.push_back({a, b, w}); };
    road(m_village + glm::vec2(-95.0f, 0.0f), m_village + glm::vec2(95.0f, 0.0f), 7.0f); // main street
    road(m_village + glm::vec2(0.0f, -80.0f), m_village + glm::vec2(0.0f, 95.0f), 7.0f); // cross street
    road(m_village + glm::vec2(95.0f, 0.0f), m_industrial + glm::vec2(-85.0f, 0.0f), 6.0f);
    road(m_village + glm::vec2(95.0f, 0.0f), m_farm + glm::vec2(-60.0f, 0.0f), 6.0f);
    road(m_industrial + glm::vec2(0.0f, 65.0f), m_farm + glm::vec2(0.0f, -45.0f), 6.0f);
    road(m_village + glm::vec2(0.0f, -80.0f), m_radio + glm::vec2(0.0f, 22.0f), 5.0f);
    const glm::vec2 hubs[] = {m_village, m_industrial, m_farm, m_radio};
    for (const glm::vec2& e : extracts) {
        const glm::vec2* best = &hubs[0];
        for (const glm::vec2& h : hubs)
            if (glm::length(h - e) < glm::length(*best - e)) best = &h;
        road(e, *best, 5.0f);
    }

    std::vector<float> base(size_t(n) * n);
    for (int iz = 0; iz < n; ++iz)
        for (int ix = 0; ix < n; ++ix) {
            float x = -410.0f + ix * cell, z = -410.0f + iz * cell;
            float h = rawHeight(x, z);
            // Bump for the radio hill
            float dr = glm::length(glm::vec2(x, z) - m_radio);
            h += 9.0f * std::exp(-dr * dr / (2.0f * 55.0f * 55.0f));
            for (const Zone& zn : m_zones) {
                float d = glm::length(glm::vec2(x, z) - zn.c);
                float w = 1.0f - smoothstep(zn.radius, zn.radius + 30.0f, d);
                h = h + (zn.baseHeight - h) * w;
            }
            base[size_t(iz) * n + ix] = h;
        }
    // Roads follow a smoothed version of the ground.
    std::vector<float> smooth = base, tmp(base.size());
    for (int pass = 0; pass < 4; ++pass) {
        for (int iz = 0; iz < n; ++iz)
            for (int ix = 0; ix < n; ++ix) {
                float s = 0.0f;
                int c = 0;
                for (int dz = -3; dz <= 3; ++dz)
                    for (int dx = -3; dx <= 3; ++dx) {
                        int x = std::clamp(ix + dx, 0, n - 1), z = std::clamp(iz + dz, 0, n - 1);
                        s += smooth[size_t(z) * n + x];
                        ++c;
                    }
                tmp[size_t(iz) * n + ix] = s / c;
            }
        smooth.swap(tmp);
    }
    colors.assign(size_t(n) * n, kGrass);
    for (int iz = 0; iz < n; ++iz)
        for (int ix = 0; ix < n; ++ix) {
            float x = -410.0f + ix * cell, z = -410.0f + iz * cell;
            size_t k = size_t(iz) * n + ix;
            float h = base[k];
            float roadD = 1e9f;
            for (const Road& r : m_roads) roadD = std::min(roadD, segDist({x, z}, r.a, r.b) - r.width * 0.5f);
            float rw = 1.0f - smoothstep(0.0f, 8.0f, roadD);
            h = h + (smooth[k] - h) * rw;
            // Edge berm: the map is a valley you cannot climb out of.
            float edge = kHalf - std::max(std::abs(x), std::abs(z));
            if (edge < 25.0f) h += std::pow(25.0f - edge, 1.5f) * 0.2f;
            terrain.at(ix, iz) = h;

            float v = 0.9f + 0.2f * noise.value(x * 0.05f, z * 0.05f);
            glm::vec3 col = kGrass * v;
            col = glm::mix(col, kField, std::clamp(noise.value(x * 0.012f + 40.0f, z * 0.012f) - 0.55f, 0.0f, 0.3f) * 2.0f);
            if (roadD < 0.0f) col = kRoad * (0.92f + 0.12f * noise.value(x * 0.3f, z * 0.3f));
            else if (roadD < 1.5f) col = glm::mix(kRoad, col, roadD / 1.5f);
            colors[k] = col;
        }
    terrain.updateBounds();
    world.setTerrain(&terrain);
    for (const Road& r : m_roads) {
        m_map->roads.push_back(glm::vec4(r.a, r.b));
        glm::vec2 c = (r.a + r.b) * 0.5f, d = r.b - r.a;
        m_map->mapRects.push_back({c, glm::vec2(glm::length(d) * 0.5f, r.width * 0.5f), std::atan2(d.y, d.x), glm::vec3(0.55f, 0.47f, 0.35f)});
    }

    // ---------- structures
    for (int i = 0; i < 4; ++i) {
        const char* names[] = {"North road", "East bridge", "South fields", "West ridge"};
        extraction(names[i], extracts[i]);
    }
    m_lootSpots.clear();
    {
        // This raid's loot zone: one of the four areas, picked from the seed.
        std::mt19937 pickRng(seed * 2654435761u + 97u);
        int area = int(pickRng() % 4u);
        const char* names[] = {"Village", "Industrial yard", "Farm", "Radio tower"};
        const glm::vec2 centres[] = {m_village, m_industrial, m_farm, m_radio};
        const float radii[] = {105.0f, 100.0f, 90.0f, 40.0f};
        map.lootZone = LootZone{names[area], centres[area], radii[area], area};
    }
    m_area = 0;
    village();
    m_area = 1;
    industrial();
    m_area = 2;
    farm();
    m_area = 3;
    radioTower();
    m_area = -1;
    // Containers in the loot zone carry better gear; extra caches go upstairs and on walkways.
    for (ContainerSpawn& cs : map.containers)
        if (map.inLootZone(cs.position)) {
            cs.lootZone = true;
            cs.tier = std::max(cs.tier, 1);
        }
    {
        int extra = 0;
        const ContainerType kinds[] = {ContainerType::WeaponCrate, ContainerType::Duffel, ContainerType::AmmoBox, ContainerType::WeaponCrate,
                                       ContainerType::MedCabinet, ContainerType::Duffel};
        for (const LootSpot& ls : m_lootSpots)
            if (ls.area == map.lootZone.area && extra < 6) {
                container(kinds[extra % 6], ls.p, u01() * 6.28f, 2, -1);
                map.containers.back().lootZone = true;
                ++extra;
            }
        for (Hotspot& h : map.hotspots) h.lootZone = map.inLootZone(h.position);
    }
    // Woods darken the ground under them (after structures so they avoid them)
    woods();
    for (const SceneBlock& b : m_map->blocks) {
        if (b.pattern != Pattern::Foliage || b.box.half.x < 1.2f) continue;
        int ix = int((b.box.center.x + 410.0f) / cell), iz = int((b.box.center.z + 410.0f) / cell);
        for (int dz = -2; dz <= 2; ++dz)
            for (int dx = -2; dx <= 2; ++dx) {
                int x = ix + dx, z = iz + dz;
                if (x < 0 || z < 0 || x >= n || z >= n) continue;
                glm::vec3& c = colors[size_t(z) * n + x];
                c = glm::mix(c, kForestFloor, 0.35f);
            }
    }

    // Loose loot in the open (near extracts and ruins)
    for (int i = 0; i < 10; ++i) {
        glm::vec2 q(u11() * 330.0f, u11() * 330.0f);
        if (blocked(q, 2.0f)) continue;
        container(i % 3 == 0 ? ContainerType::AmmoBox : ContainerType::Duffel, glm::vec3(q.x, ground(q.x, q.y), q.y), u01() * 6.28f, 0, -1);
    }

    // Spawn points around the edge, away from extracts
    for (int i = 0; i < 24; ++i) {
        float a = float(i) / 24.0f * 6.2832f + u11() * 0.08f;
        glm::vec2 p = glm::vec2(std::cos(a), std::sin(a)) * (kHalf - 55.0f - 15.0f * u01());
        p = glm::clamp(p, glm::vec2(-kHalf + 40.0f), glm::vec2(kHalf - 40.0f));
        bool nearExtract = false;
        for (const glm::vec2& e : extracts) nearExtract |= glm::length(e - p) < 70.0f;
        if (nearExtract || blocked(p, 1.0f)) continue;
        SpawnPoint s;
        s.position = glm::vec3(p.x, ground(p.x, p.y) + 0.05f, p.y);
        s.yaw = std::atan2(p.x, p.y); // facing the centre: yaw 0 looks down -Z
        m_map->spawns.push_back(s);
    }
}
