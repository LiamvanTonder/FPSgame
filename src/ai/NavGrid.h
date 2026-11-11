#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <vector>

class World;
struct MapData;
class Terrain;

// Walkability grid over the map with A* path finding and cover lookups.
class NavGrid {
public:
    enum Flags : uint8_t { Walkable = 1, NearCover = 2 };

    void build(const World& world, float halfSize, float cellSize = 0.5f);
    // Upper levels and stairs (after build).
    void addLevels(const World& world, const MapData& map);

    // ---- Multi-level queries (points carry their height; level 0 is the ground grid)
    int levelAt(const glm::vec3& p) const;          // 0 ground, k > 0 floor k-1
    float levelHeight(const glm::vec3& p) const;    // walking height of p's level there
    bool walkable3(const glm::vec3& p) const;
    bool clearLine3(const glm::vec3& a, const glm::vec3& b) const; // same level only
    // Path over every level (stairs included). Waypoints carry their height.
    bool findPath3(const glm::vec3& from, const glm::vec3& to, std::vector<glm::vec3>& out, int maxExpansions = 300000);
    int floorCount() const { return int(m_floors.size()); }
    // Optional extra cost per metre at (x, z) for the next searches (e.g. ground an enemy can see).
    // Straightening only skips cells when the shortcut is no more exposed than the start.
    std::function<float(float, float)> extraCost;
    bool valid() const { return m_n > 0; }

    bool walkable(const glm::vec2& p) const;
    // Straight walk between two points without hitting blocked cells.
    bool clearLine(const glm::vec2& a, const glm::vec2& b) const;
    bool nearestWalkable(const glm::vec2& p, glm::vec2& out, int maxRadiusCells = 12) const;
    // Smoothed path of waypoints (excluding the start). Empty if none.
    bool findPath(const glm::vec2& from, const glm::vec2& to, std::vector<glm::vec2>& out, int maxExpansions = 300000);

    struct Cover {
        glm::vec2 position;
        float obstacleHeight; // of the blocking geometry toward the threat
    };
    // Cells next to obstacles that stand between them and 'threat', within radius of 'around'.
    void findCover(const glm::vec2& around, const glm::vec2& threat, float radius, std::vector<Cover>& out, int maxResults = 16) const;

    float cell() const { return m_cell; }
    int lastExpansions() const { return m_lastExpansions; }

private:
    int index(int x, int z) const { return z * m_n + x; }
    bool toCell(const glm::vec2& p, int& x, int& z) const;
    glm::vec2 center(int x, int z) const { return {m_origin + (x + 0.5f) * m_cell, m_origin + (z + 0.5f) * m_cell}; }
    bool cellWalkable(int x, int z) const { return x >= 0 && z >= 0 && x < m_n && z < m_n && (m_flags[index(x, z)] & Walkable); }

    int m_n = 0;
    float m_cell = 0.5f, m_origin = 0.0f;
    std::vector<uint8_t> m_flags;
    std::vector<uint8_t> m_height; // obstacle height above ground (dm) for blocked cells

    // A* scratch
    std::vector<float> m_g;
    std::vector<int> m_parent;
    std::vector<uint32_t> m_stamp;
    std::vector<uint8_t> m_closed;
    uint32_t m_search = 0;
    int m_lastExpansions = 0;

    struct Floor {
        glm::vec2 origin{0.0f};
        int nx = 0, nz = 0;
        float y = 0.0f;
        int offset = 0; // first node index in the combined graph
        std::vector<uint8_t> flags;
        bool inside(const glm::vec2& p) const;
    };
    std::vector<Floor> m_floors;
    struct LinkEdge {
        int to;
        float cost;
        glm::vec3 from3, to3;
    };
    std::vector<std::vector<LinkEdge>> m_linkAdj; // per node (sparse: only for linked nodes, via map)
    std::vector<int> m_linkNodes;                 // nodes that have link edges
    std::vector<int> m_linkSlot;                  // node -> index into m_linkAdj (-1 none)
    const Terrain* m_terrain = nullptr;
    int nodeAt(const glm::vec3& p) const;         // combined-graph node, -1 if none
    bool nodeWalkable(int node) const;
    glm::vec3 nodePos(int node) const;
    bool nearestNode(const glm::vec3& p, int& node) const;
};
