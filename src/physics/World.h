#pragma once

#include "physics/Collision.h"

#include <cstdint>
#include <vector>

enum class Surface { Dirt, Grass, Concrete, Wood, Steel, Paper, Metal, Flesh, Foliage };

// Who a collider belongs to. Bullets ignore colliders of their own shooter.
// Character k (0 = the player) owns colliders with owner == k.
enum : int { kOwnerWorld = -1, kOwnerPlayer = 0 };

class Terrain;

struct Collider {
    OBB box;
    Surface surface = Surface::Concrete;
    bool blocksPlayer = true;  // movement
    bool blocksBullets = true; // false: foliage (bullets pass, sight does not)
    bool blocksVision = true;
    bool penetrable = false;   // bullets pass through (paper)
    bool active = true;
    bool dynamic = false;      // moves (plates, bodies): kept out of the static grid
    bool terrain = false;      // the heightfield's stand-in collider
    int owner = kOwnerWorld;
    int paperTarget = -1;
    int steelPlate = -1;
    int bodyPart = -1;         // index into the owner's body
};

struct WorldHit {
    float t = 1.0f;          // fraction along the query segment
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f};
    int collider = -1;
};

struct SegmentQuery {
    int ignoreCollider = -1;
    int ignoreOwner = -100;      // skip colliders with this owner
    bool skipPenetrable = false;
    bool staticOnly = false;     // only world geometry (owner == kOwnerWorld, not targets)
    bool vision = false;         // line of sight: foliage blocks, glass-like penetrables don't
    bool noTerrain = false;
};

class World {
public:
    int add(const Collider& c);
    Collider& collider(int index) { return m_colliders[index]; }
    const Collider& collider(int index) const { return m_colliders[index]; }
    const std::vector<Collider>& colliders() const { return m_colliders; }

    void setTerrain(const Terrain* t);
    const Terrain* terrain() const { return m_terrain; }
    int terrainCollider() const { return m_terrainCollider; }

    // Nearest hit along segment a->b.
    bool segment(const glm::vec3& a, const glm::vec3& b, WorldHit& out, const SegmentQuery& q) const;
    bool segment(const glm::vec3& a, const glm::vec3& b, WorldHit& out, int ignore = -1, bool skipPenetrable = false) const {
        SegmentQuery q;
        q.ignoreCollider = ignore;
        q.skipPenetrable = skipPenetrable;
        return segment(a, b, out, q);
    }
    // Colliders whose bounds overlap the box (static via the grid, then dynamic).
    void overlap(const AABB& box, std::vector<int>& out) const;
    // Highest standing surface at (x, z) at or below 'top' (terrain or box tops).
    float groundHeight(const glm::vec3& p, float top, float fallback) const;

private:
    void buildGrid() const;
    bool testCollider(int i, const glm::vec3& a, const glm::vec3& b, const SegmentQuery& q, float& best, WorldHit& out) const;

    std::vector<Collider> m_colliders;
    const Terrain* m_terrain = nullptr;
    int m_terrainCollider = -1;

    // Uniform 2-D grid over static colliders (rebuilt lazily when statics are added).
    mutable bool m_gridDirty = true;
    mutable float m_cell = 8.0f;
    mutable glm::vec2 m_gridMin{0.0f};
    mutable int m_gridW = 0, m_gridH = 0;
    mutable std::vector<std::vector<int>> m_cells;
    mutable std::vector<int> m_big;      // statics too large for the grid (ground slabs)
    mutable std::vector<int> m_dynamic;
    mutable std::vector<uint32_t> m_stamp;
    mutable uint32_t m_stampValue = 0;
};
