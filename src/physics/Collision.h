#pragma once

#include <glm/glm.hpp>

struct AABB {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    bool overlaps(const AABB& o) const {
        return min.x < o.max.x && max.x > o.min.x && min.y < o.max.y && max.y > o.min.y && min.z < o.max.z && max.z > o.min.z;
    }
};

// Oriented box. rotation columns are the box's local axes in world space.
struct OBB {
    glm::vec3 center{0.0f};
    glm::vec3 half{0.5f};
    glm::mat3 rotation{1.0f};

    static OBB fromMinMax(const glm::vec3& mn, const glm::vec3& mx) {
        return OBB{(mn + mx) * 0.5f, (mx - mn) * 0.5f, glm::mat3(1.0f)};
    }
    static OBB fromCenterSize(const glm::vec3& c, const glm::vec3& size) {
        return OBB{c, size * 0.5f, glm::mat3(1.0f)};
    }

    AABB bounds() const;
    glm::mat4 modelMatrix() const; // transforms the unit box mesh onto this OBB
    glm::vec3 toLocal(const glm::vec3& world) const { return glm::transpose(rotation) * (world - center); }
};

// Segment a->b against an OBB. On hit: t in [0,1] along the segment and outward surface normal.
// A segment starting inside the box reports t = 0 with normal opposing the direction.
bool intersectSegmentOBB(const glm::vec3& a, const glm::vec3& b, const OBB& box, float& tOut, glm::vec3& normalOut);
