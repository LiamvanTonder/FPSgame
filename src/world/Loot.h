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
    bool lootZone = false;
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
// Random optic that fits the gun / random attachment (fitting 'gun' if given), weighted by value.
int randomOptic(const WeaponDef& w, float quality, std::mt19937& rng, bool magnifiedOnly);
int randomAttachment(float quality, std::mt19937& rng, const Item* gun);
void rollAttachments(Item& gun, float quality, std::mt19937& rng, int count);
int pickWeaponByValue(int quality, std::mt19937& rng, bool allowLong = true);
ItemPtr rollWeapon(int tier, std::mt19937& rng, bool allowLong = true);
void fillContainer(LootContainer& c, std::mt19937& rng);
// Raider kits: 35 % barebones pistol, 40 % cheap gun, 15 % solid, 10 % better (quality 0..1 within).
struct BotKitRoll {
    int tier = 0;
    float quality = 0.0f;
};
BotKitRoll rollBotTier(std::mt19937& rng);
// A gun in a value band [lo, hi], weighted by value (quality flattens the curve).
int pickWeaponBand(float quality, std::mt19937& rng, int lo, int hi, bool pistols);
// A full kit for a raider of the given tier (0..3).
void makeLoadout(Equipment& eq, int tier, std::mt19937& rng, float quality = 0.0f);
// The always-available starter kit.
void makeFreeKit(Equipment& eq);
// Compatible magazines (loaded) and loose ammo for a weapon.
void addAmmoFor(Grid& g, int weapon, int mags, int looseRounds, std::mt19937& rng, int ammoDef = -1);
