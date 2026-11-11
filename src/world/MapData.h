#pragma once

#include "physics/Collision.h"
#include "render/Pattern.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <string>
#include <vector>

// Static scenery shape. Cylinders and spheres are inscribed in the box (cylinder axis = local Y).
struct SceneBlock {
    enum Shape { Box, Cylinder, Sphere };
    OBB box;
    glm::vec3 color{0.5f};
    Pattern pattern = Pattern::Plain;
    Shape shape = Box;
};

enum class ContainerType { WeaponCrate, AmmoBox, MedCabinet, Duffel, Toolbox };

struct ContainerSpawn {
    ContainerType type = ContainerType::Duffel;
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    int tier = 0;     // loot quality bias
    int hotspot = -1; // index into hotspots, -1 if none
};

struct Hotspot {
    std::string name;
    glm::vec3 position{0.0f};
    float radius = 30.0f;
};

struct ExtractPoint {
    std::string name;
    glm::vec3 position{0.0f}; // centre of the zone
    float radius = 8.0f;
    glm::vec3 dropBox{0.0f};  // send-back uplink crate
};

// A way into a building: a point outside the door and one inside it (for tests and the AI).
struct Entrance {
    glm::vec3 outside{0.0f}, inside{0.0f};
};

struct SpawnPoint {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
};

// Labelled areas and lines for the paper map.
struct MapLabel {
    std::string name;
    glm::vec2 center{0.0f}, size{0.0f};
    glm::vec3 color{0.5f};
};
struct MapRect {
    glm::vec2 center{0.0f}, half{0.0f};
    float angle = 0.0f;
    glm::vec3 color{0.3f};
};

struct MapData {
    std::vector<SceneBlock> blocks;
    std::vector<Hotspot> hotspots;
    std::vector<ExtractPoint> extracts;
    std::vector<SpawnPoint> spawns;
    std::vector<Entrance> entrances;
    std::vector<ContainerSpawn> containers;
    std::vector<MapLabel> labels;
    std::vector<MapRect> mapRects; // buildings, roads, woods for the map screen
    std::vector<glm::vec4> roads;  // segment a.xy -> b.zw (8 m wide incl. verges)
    std::vector<glm::vec3> woods;  // centre xz + radius
    float halfSize = 0.0f;         // playable area is [-halfSize, halfSize]^2 (0: no map)
    bool hasTerrain = false;
    unsigned seed = 0;
    void clear() { *this = MapData{}; }
    // What the open ground is like underfoot (0 grass, 1 dirt road, 2 leaf litter).
    int groundAt(float x, float z) const {
        glm::vec2 p(x, z);
        for (const glm::vec4& r : roads) {
            glm::vec2 a(r.x, r.y), b(r.z, r.w), ab = b - a;
            float t = std::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
            if (glm::length(p - (a + ab * t)) < 3.5f) return 1;
        }
        for (const glm::vec3& w : woods)
            if (glm::length(p - glm::vec2(w.x, w.y)) < w.z * 0.85f) return 2;
        return 0;
    }
};
