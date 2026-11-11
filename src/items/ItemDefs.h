#pragma once

#include "game/Ballistics.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

enum class ItemCat { Weapon, Magazine, Ammo, Med, Optic, Rig, Backpack };
enum class MedKind { Bandage, Tourniquet, Hemostatic, Splint, Painkillers, FieldKit, Surgical };

// One kind of cartridge. Physical properties replace the weapon's standard load; the
// muzzle velocity is the gun's (barrel length) scaled by velocityFactor.
struct AmmoDef {
    std::string id, name;
    int caliber = 0;
    float mass = 0.0f, diameter = 0.0f, dragCoefficient = 0.25f;
    float velocityFactor = 1.0f;
    float damage = 1.0f;      // wound multiplier (expanding > 1, AP < 1)
    float penetration = 0.0f; // 0..1: AP passes through wood and thin cover
    float bleed = 1.0f;       // bleeding chance multiplier
    int pellets = 0;          // 0 = use the weapon's
    float spread = -1.0f;     // < 0 = use the weapon's
    int value = 1;            // per round
    glm::vec3 tip{0.7f, 0.45f, 0.25f};

    BallisticParams ballistics(float gunVelocity) const {
        BallisticParams p;
        p.muzzleVelocity = gunVelocity * velocityFactor;
        p.mass = mass;
        p.diameter = diameter;
        p.dragCoefficient = dragCoefficient;
        return p;
    }
};

struct ItemDef {
    std::string id, name, shortName;
    ItemCat cat = ItemCat::Ammo;
    int w = 1, h = 1;
    int value = 100;
    int tier = 0; // loot rarity
    glm::vec3 color{0.4f};

    int weapon = -1;    // Weapon: index into weaponDefs()
    int ammo = -1;      // Ammo: index into ammoDefs()
    int optic = -1;     // Optic: index into opticDefs()
    int caliber = -1;   // Ammo / Magazine
    int magFamily = -1; // Magazine
    int capacity = 0;   // Magazine rounds
    int stack = 1;      // Ammo: rounds per cell
    MedKind med = MedKind::Bandage;
    int uses = 1;
    float useTime = 3.0f;
    float hpPool = 0.0f;
    int gridW = 0, gridH = 0; // Rig / Backpack
};

const std::vector<AmmoDef>& ammoDefs();
const std::vector<ItemDef>& itemDefs();
int findItemDef(const std::string& id);
int findAmmo(const std::string& id);
int weaponItemDef(int weapon);        // item def of a weapon
int opticItemDef(int optic);          // item def of an optic (-1 for iron sights)
int defaultAmmo(int caliber);         // the common load of a calibre
int magazineItemFor(int weapon);      // the standard magazine item for a gun (-1 if none)
const char* categoryName(ItemCat c);
