#pragma once

#include "physics/World.h"
#include "world/MapData.h"

#include <glm/glm.hpp>
#include <vector>

class Targets;

// Outdoor rifle range: covered firing line, 220 m lane between berms, backstop.
class Level {
public:
    void build(World& world, Targets& targets, MapData& map);

    glm::vec3 spawnPoint{0.0f, 0.05f, 1.5f};
    float spawnYaw = 0.0f;
    glm::vec3 stashPos{6.5f, 0.05f, 5.3f};   // locker: open the stash
    glm::vec3 deployPos{-6.5f, 0.05f, 5.3f}; // gate: start a raid
    glm::vec3 storePos{0.0f, 0.05f, 5.2f};   // counter: buy and sell

private:
    void block(World& world, const glm::vec3& mn, const glm::vec3& mx, const glm::vec3& color, Surface surface,
               Pattern pattern = Pattern::Plain, bool collides = true);

    MapData* m_map = nullptr;
};
