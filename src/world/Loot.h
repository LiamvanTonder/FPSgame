#pragma once

#include "game/Weapon.h"
#include "items/Item.h"
#include "world/MapData.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <random>
#include <vector>

class World;
class Renderer;

struct LootContainer {
    ContainerType type = ContainerType::Duffel;
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    glm::vec3 half{0.4f};
    Grid grid;
    int tier = 0;
    int hotspot = -1;
    int collider = -1;
    std::vector<int> searchedBy; // character ids who have gone through it
    bool searched(int who) const;
};

// Something lying on the ground (dropped magazine, a dead man's rifle).
struct WorldItem {
    ItemPtr item;
    glm::vec3 position{0.0f}, velocity{0.0f}, spin{0.0f};
    glm::quat rotation{1, 0, 0, 0};
    bool resting = false;
    double dropTime = 0.0;
};

const char* containerName(ContainerType t);

class Loot {
public:
    void clear();
    // Creates containers (geometry + collider + contents) from the map's spawn list.
    void spawnContainers(const MapData& map, World& world, std::vector<SceneBlock>& blocks, std::mt19937& rng);
    void drop(DroppedItem&& d, double time);
    void update(float dt, const World& world);
    void draw(Renderer& r, const glm::vec3& camera, float maxDistance) const;

    std::vector<LootContainer> containers;
    std::vector<WorldItem> items;
};

// Loot generation
float weaponWeight(int weapon, int quality);
int pickWeaponByValue(int quality, std::mt19937& rng, bool allowLong = true);
ItemPtr rollWeapon(int tier, std::mt19937& rng, bool allowLong = true);
void fillContainer(LootContainer& c, std::mt19937& rng);
// A full kit for a raider of the given tier (0..3).
void makeLoadout(Equipment& eq, int tier, std::mt19937& rng);
// The always-available starter kit.
void makeFreeKit(Equipment& eq);
// Compatible magazines (loaded) and loose ammo for a weapon.
void addAmmoFor(Grid& g, int weapon, int mags, int looseRounds, std::mt19937& rng, int ammoDef = -1);
