#pragma once

#include "game/WeaponDefs.h"
#include "items/ItemDefs.h"

#include <memory>
#include <string>
#include <vector>

struct Item;

// What an attachment does beyond its mass and shape.
enum class AttSpecial {
    None, Suppressor, Brake, Compensator, FlashHider, Choke,
    VerticalGrip, AngledGrip, Handstop, Bipod,
    Light, Laser, LaserLight,
    Stock, FoldingStock, PistolStock,
    Grip, OffsetSight, Magnifier,
};

// One attachment model. Modifiers multiply the gun's own figures; loudness adds (dB).
struct AttachmentDef {
    std::string id, name, shortName;
    int slot = AS_Muzzle;
    int mount = 0;          // Thread (muzzle) or StockMount (stock); unused otherwise
    float bore = 0.0f;      // suppressors: largest bullet diameter (m)
    int w = 1, h = 1, value = 100, tier = 0;
    float mass = 0.1f;      // kg
    float length = 0.0f;    // added at the muzzle (m)
    float loudnessDb = 0.0f, flashMul = 1.0f, smokeMul = 1.0f;
    float riseMul = 1.0f, yawMul = 1.0f, backMul = 1.0f, swayMul = 1.0f, adsMul = 1.0f;
    float velocityMul = 1.0f, spreadMul = 1.0f;
    AttSpecial special = AttSpecial::None;
    bool heavy = false;     // heavy bipod: LMGs and precision rifles only
    bool pistolOnly = false, longOnly = false;
    glm::vec3 color{0.12f, 0.12f, 0.13f};
};

const std::vector<AttachmentDef>& attachmentDefs();
int findAttachment(const std::string& id);
const char* attachSlotName(int slot);
const char* attachSpecialName(AttSpecial s);

// Can attachment 'att' go on this gun? 'weapon' (optional) is checked for slot-dependent
// requirements (a magnifier needs a red dot fitted). 'why' explains a refusal.
bool attachmentFits(const WeaponDef& base, int att, const Item* weapon = nullptr, std::string* why = nullptr);
// Index of the slot an attachment goes in.
inline int attachmentSlot(int att) { return attachmentDefs()[att].slot; }

// The gun as built: the base model with every fitted attachment's shape, mass and effect.
std::shared_ptr<const WeaponDef> buildEffectiveWeapon(const Item& weapon);

// What the gun weighs and how hard it is to swing (moment of inertia about the grip, kg m^2).
float weaponMass(const WeaponDef& w);
