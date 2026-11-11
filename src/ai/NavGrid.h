#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

class World;

// Walkability grid over the map with A* path finding and cover lookups.
class NavGrid {
public:
    enum Flags : uint8_t { Walkable = 1, NearCover = 2 };

    void build(const World& world, float halfSize, float cellSize = 0.5f);
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
};
