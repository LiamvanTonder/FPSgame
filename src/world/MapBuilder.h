#pragma once

#include "physics/World.h"
#include "world/MapData.h"
#include "world/Terrain.h"

#include <glm/glm.hpp>
#include <random>
#include <vector>

// Generates the raid map: terrain, settlements, woods, roads, hotspots, extractions,
// spawn points and loot containers. Deterministic for a given seed.
class MapBuilder {
public:
    void build(unsigned seed, World& world, Terrain& terrain, MapData& map, std::vector<glm::vec3>& terrainColors);

private:
    struct Road {
        glm::vec2 a, b;
        float width;
    };
    struct Zone {
        glm::vec2 c;
        float radius;
        float baseHeight;
    };

    // Primitive helpers (axis-aligned unless rotated explicitly)
    int solid(const glm::vec3& mn, const glm::vec3& mx, const glm::vec3& color, Surface s, Pattern p = Pattern::Plain);
    void visual(const OBB& box, const glm::vec3& color, Pattern p = Pattern::Plain, SceneBlock::Shape shape = SceneBlock::Box);
    int collider(const OBB& box, Surface s, bool player = true, bool bullets = true, bool vision = true);
    // Wall along X (dir 0) or Z (dir 1) from 'start' with openings {offset, width, bottom, top}.
    struct Opening {
        float offset, width, bottom, top;
    };
    void wall(const glm::vec3& start, int dir, float length, float thickness, float height, const std::vector<Opening>& openings,
              const glm::vec3& color, Surface s, Pattern p);

    void house(const glm::vec2& c, int rot, float w, float d, int zoneTier, int hotspot);
    // Two floors with a straight stair, upper windows to shoot from, optional balcony and roof access.
    void twoStorey(const glm::vec2& c, int front, float w, float d, const glm::vec3& color, Surface s, Pattern p, int tier, int hotspot,
                   bool balcony, bool roofAccess);
    // A raised walkway along the inside of a big building's north wall (z0), with a stair at one end.
    void mezzanine(float x0, float z0, float w, float fy, float t, float height);
    // Sandbags, barriers, crates and wrecks around a point.
    void coverCluster(const glm::vec2& c, float radius, int count);
    // A straight flight of solid steps from 'foot' rising toward 'dir' (unit, xz) up to height 'top'.
    void stairs(const glm::vec3& foot, const glm::vec2& dir, float width, float top, const glm::vec3& color);
    struct LootSpot {
        glm::vec3 p;
        int area;
    };
    std::vector<LootSpot> m_lootSpots;
    int m_area = -1; // area being built (0 village, 1 industrial, 2 farm, 3 radio)
    void bigBuilding(const glm::vec2& c, int rot, float w, float d, float h, const glm::vec3& color, Surface s, Pattern p,
                     float doorWidth, float doorHeight, int crates, int tier, int hotspot, bool racks, bool mezz = false);
    void shed(const glm::vec2& c, int rot, float w, float d, int tier, int hotspot);
    void shippingContainer(const glm::vec2& c, float yawRight, int level, const glm::vec3& color);
    void car(const glm::vec2& c, bool alongX);
    void tree(const glm::vec2& p, float scale);
    void bush(const glm::vec2& p, float scale);
    void rock(const glm::vec2& p, float size);
    void fenceLine(const glm::vec2& a, const glm::vec2& b, float gapAt = -1.0f, float gapWidth = 0.0f);
    void stoneWall(const glm::vec2& a, const glm::vec2& b, float gapAt = -1.0f);
    void container(ContainerType type, const glm::vec3& p, float yaw, int tier, int hotspot);
    void extraction(const std::string& name, const glm::vec2& p);
    // Doorways of the building being built: furniture and loot are kept clear of them.
    struct Door {
        glm::vec2 center, normal; // normal points out of the building (zero for interior doorways)
    };
    std::vector<Door> m_doors;
    void addWallDoors(const std::vector<Opening>& ops, const glm::vec3& start, int dir, float t, const glm::vec2& outward);
    void recordEntrances(float floorY);
    bool doorClear(const glm::vec2& p, float radius) const;

    void village();
    void industrial();
    void farm();
    void radioTower();
    void woods();

    float ground(float x, float z) const { return m_terrain->height(x, z); }
    // Floor level for a footprint: the highest terrain point under it, so nothing floats.
    float footprintHeight(const glm::vec2& c, float hw, float hd) const;
    glm::vec2 rotate(const glm::vec2& v, int rot) const;
    float u01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(m_rng); }
    float u11() { return std::uniform_real_distribution<float>(-1.0f, 1.0f)(m_rng); }
    bool blocked(const glm::vec2& p, float r) const; // overlaps a building footprint or road
    void reserve(const glm::vec2& c, const glm::vec2& half);

    World* m_world = nullptr;
    Terrain* m_terrain = nullptr;
    MapData* m_map = nullptr;
    std::mt19937 m_rng;
    std::vector<Road> m_roads;
    std::vector<Zone> m_zones;
    std::vector<glm::vec4> m_reserved; // footprints: centre xy, half extents zw
    glm::vec2 m_village, m_industrial, m_farm, m_radio;
};
