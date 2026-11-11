#include "game/Level.h"

#include "game/Targets.h"

namespace {
const glm::vec3 kGrass(0.40f, 0.46f, 0.30f);
const glm::vec3 kConcrete(0.60f, 0.60f, 0.58f);
const glm::vec3 kDarkConcrete(0.46f, 0.46f, 0.45f);
const glm::vec3 kDirt(0.50f, 0.40f, 0.28f);
const glm::vec3 kWood(0.50f, 0.36f, 0.22f);
const glm::vec3 kRoof(0.30f, 0.32f, 0.34f);
} // namespace

void Level::block(World& world, const glm::vec3& mn, const glm::vec3& mx, const glm::vec3& color, Surface surface,
                  Pattern pattern, bool collides) {
    OBB box = OBB::fromMinMax(mn, mx);
    SceneBlock b;
    b.box = box;
    b.color = color;
    b.pattern = pattern;
    m_map->blocks.push_back(b);
    if (collides) {
        Collider c;
        c.box = box;
        c.surface = surface;
        world.add(c);
    }
}

void Level::build(World& world, Targets& targets, MapData& map) {
    m_map = &map;
    map.clear();

    // Ground
    block(world, {-80, -1, -300}, {80, 0, 40}, kGrass, Surface::Grass, Pattern::Grid);

    // Firing line: concrete pad, roof on posts, back and side walls.
    block(world, {-10, 0, -1.8f}, {10, 0.05f, 6.3f}, kConcrete, Surface::Concrete, Pattern::Grid);
    block(world, {-10.3f, 2.75f, -2.0f}, {10.3f, 2.95f, 6.5f}, kRoof, Surface::Metal);
    for (float x : {-9.8f, -5.0f, 5.0f, 9.8f}) {
        block(world, {x - 0.1f, 0.05f, -1.9f}, {x + 0.1f, 2.75f, -1.7f}, kWood, Surface::Wood);
    }
    block(world, {-10, 0.05f, 6.0f}, {10, 2.75f, 6.3f}, kDarkConcrete, Surface::Concrete);
    block(world, {-10.3f, 0.05f, -1.0f}, {-10.0f, 2.75f, 6.3f}, kDarkConcrete, Surface::Concrete);
    block(world, {10.0f, 0.05f, -1.0f}, {10.3f, 2.75f, 6.3f}, kDarkConcrete, Surface::Concrete);

    // Shooting benches with gaps between them to walk downrange.
    for (float x : {-4.0f, 0.0f, 4.0f}) {
        block(world, {x - 0.7f, 0.82f, -1.65f}, {x + 0.7f, 0.88f, -0.95f}, kWood, Surface::Wood);
        for (float lx : {x - 0.62f, x + 0.56f})
            block(world, {lx, 0.05f, -1.6f}, {lx + 0.06f, 0.82f, -1.0f}, kWood, Surface::Wood);
    }

    // Side berms and backstop
    block(world, {-19, 0, -236}, {-12.5f, 4.0f, -2.0f}, kDirt, Surface::Dirt);
    block(world, {12.5f, 0, -236}, {19, 4.0f, -2.0f}, kDirt, Surface::Dirt);
    block(world, {-19, 0, -246}, {19, 10.0f, -236}, kDirt, Surface::Dirt);

    // Barriers near the line for cover drills (and to test the barrel/wall collision).
    block(world, {-9.2f, 0, -7.3f}, {-6.8f, 1.05f, -6.7f}, kConcrete, Surface::Concrete);        // low wall
    block(world, {6.8f, 0, -9.15f}, {9.2f, 2.2f, -8.85f}, kConcrete, Surface::Concrete);         // tall wall
    block(world, {5.0f, 0, -15.15f}, {9.0f, 1.1f, -14.85f}, kConcrete, Surface::Concrete);       // window wall
    block(world, {5.0f, 1.6f, -15.15f}, {9.0f, 2.3f, -14.85f}, kConcrete, Surface::Concrete);
    block(world, {5.0f, 1.1f, -15.15f}, {5.6f, 1.6f, -14.85f}, kConcrete, Surface::Concrete);
    block(world, {8.4f, 1.1f, -15.15f}, {9.0f, 1.6f, -14.85f}, kConcrete, Surface::Concrete);

    // Distance markers along both berms: height grows with range so they read at a glance.
    const float distances[] = {10, 25, 50, 100, 150, 200};
    for (int i = 0; i < 6; ++i) {
        float z = -distances[i];
        float h = 0.6f + 0.25f * i;
        glm::vec3 color = (i % 2) ? glm::vec3(0.95f, 0.85f, 0.15f) : glm::vec3(0.95f, 0.30f, 0.15f);
        for (float x : {-11.8f, 11.8f})
            block(world, {x - 0.15f, 0, z - 0.15f}, {x + 0.15f, h, z + 0.15f}, color, Surface::Wood);
    }

    // Stash locker and deploy gate along the back wall
    block(world, stashPos + glm::vec3(-0.6f, 0.0f, -0.3f), stashPos + glm::vec3(0.6f, 2.0f, 0.4f), glm::vec3(0.30f, 0.36f, 0.30f), Surface::Metal);
    block(world, stashPos + glm::vec3(-0.62f, 1.2f, -0.32f), stashPos + glm::vec3(0.62f, 1.3f, -0.29f), glm::vec3(0.9f, 0.8f, 0.3f), Surface::Metal, Pattern::Unlit, false);
    block(world, deployPos + glm::vec3(-0.9f, 0.0f, -0.1f), deployPos + glm::vec3(-0.7f, 2.4f, 0.3f), glm::vec3(0.25f, 0.25f, 0.27f), Surface::Metal);
    block(world, deployPos + glm::vec3(0.7f, 0.0f, -0.1f), deployPos + glm::vec3(0.9f, 2.4f, 0.3f), glm::vec3(0.25f, 0.25f, 0.27f), Surface::Metal);
    block(world, deployPos + glm::vec3(-0.9f, 2.4f, -0.1f), deployPos + glm::vec3(0.9f, 2.6f, 0.3f), glm::vec3(0.25f, 0.25f, 0.27f), Surface::Metal);
    block(world, deployPos + glm::vec3(-0.7f, 0.0f, 0.25f), deployPos + glm::vec3(0.7f, 2.4f, 0.3f), glm::vec3(0.2f, 0.5f, 0.3f), Surface::Metal, Pattern::Unlit, false);

    // Store counter in the middle of the back wall
    block(world, storePos + glm::vec3(-1.6f, 0.0f, -0.3f), storePos + glm::vec3(1.6f, 1.05f, 0.4f), glm::vec3(0.45f, 0.33f, 0.20f), Surface::Wood, Pattern::Planks);
    block(world, storePos + glm::vec3(-1.4f, 1.05f, 0.25f), storePos + glm::vec3(1.4f, 2.3f, 0.75f), glm::vec3(0.30f, 0.30f, 0.32f), Surface::Metal);
    block(world, storePos + glm::vec3(-1.0f, 2.35f, 0.4f), storePos + glm::vec3(1.0f, 2.6f, 0.45f), glm::vec3(0.2f, 0.45f, 0.9f), Surface::Metal, Pattern::Unlit, false);

    // Targets
    targets.addPaper(world, "Paper 10m", {-3.0f, 1.40f, -10.0f});
    targets.addPaper(world, "Paper 25m", {-1.0f, 1.40f, -25.0f});
    targets.addPaper(world, "Paper 50m", {2.5f, 1.40f, -50.0f});
    targets.addKnockdown(world, "Steel 100m", {-7.0f, 0.5f, -100.0f}, {0.30f, 0.50f});
    targets.addKnockdown(world, "Steel 150m", {5.0f, 0.5f, -150.0f}, {0.40f, 0.60f});
    targets.addGong(world, "Gong 200m", {0.0f, 1.9f, -200.0f}, {0.50f, 0.50f});
}

