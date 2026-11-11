#pragma once

#include <glm/glm.hpp>
#include <vector>

// Regular heightfield. Each cell is split into two triangles along its (x0,z0)-(x1,z1)
// diagonal; height() is exact for that triangulation, so rendering, walking and
// bullets all agree on where the ground is.
class Terrain {
public:
    void create(int verticesPerSide, float cellSize, const glm::vec2& origin);
    float& at(int ix, int iz) { return m_h[size_t(iz) * m_n + ix]; }
    float at(int ix, int iz) const { return m_h[size_t(iz) * m_n + ix]; }

    float height(float x, float z) const;
    glm::vec3 normal(float x, float z) const;
    bool inside(float x, float z) const;
    // Segment a->b: first crossing below the surface.
    bool segment(const glm::vec3& a, const glm::vec3& b, float& t, glm::vec3& n) const;

    int size() const { return m_n; }
    float cell() const { return m_cell; }
    glm::vec2 origin() const { return m_origin; }
    float extent() const { return m_cell * float(m_n - 1); }
    float maxHeight() const { return m_maxH; }
    void updateBounds();

private:
    int m_n = 0;
    float m_cell = 1.0f;
    glm::vec2 m_origin{0.0f};
    float m_maxH = 0.0f, m_minH = 0.0f;
    std::vector<float> m_h;
};
