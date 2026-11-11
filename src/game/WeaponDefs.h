#pragma once

#include "game/Ballistics.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

// One placeholder shape of a weapon model, in weapon-local space.
struct Part {
    enum Flags : uint8_t {
        RearSight = 1, // hidden when an optic is mounted
        Action = 2,    // slides back with the slide / bolt / pump
        Hand = 4,
        Bolt = 8,      // turn-bolt: rotates about the bore axis and slides back while loading
    };
    glm::vec3 center, size, color;
    float tiltX = 0.0f; // rotation about local X (radians)
    bool cylinder = false;
    uint8_t flags = 0;
};

enum class ActionType { SemiAuto, Bolt, Pump };
// Magazine: detachable magazine + chamber. Tube: rounds loaded one at a time into the gun
// (tube, revolver cylinder, double barrel). SingleLoad: one round hand-fed into the chamber.
enum class ReloadType { Magazine, Tube, SingleLoad };
enum class CasingType { Pistol, Rifle, Magnum, Shell };
enum class WeaponClass { Pistol, Smg, Shotgun, Rifle, Marksman, Sniper, Lmg };

enum Caliber {
    Cal_9x19, Cal_45ACP, Cal_10mm, Cal_357, Cal_46x30, Cal_556, Cal_545, Cal_762x39, Cal_300BLK,
    Cal_762x51, Cal_762x54R, Cal_338, Cal_12ga, Cal_57x28, Cal_44Mag, Cal_65, Cal_50BMG, kCaliberCount
};
const char* caliberName(int caliber);

// Magazine families: a weapon accepts the families in its mask.
enum MagFamily {
    Mag_Pistol9, Mag_Pistol45, Mag_Pistol10, Mag_Smg9, Mag_Smg45, Mag_Pdw46, Mag_Stanag, Mag_545, Mag_762x39,
    Mag_308, Mag_54R, Mag_308Bolt, Mag_Box556, Mag_Pistol57, Mag_Pdw57, Mag_12Box, Mag_65, Mag_Box308, kMagFamilyCount
};
inline uint32_t magBit(int family) { return 1u << family; }

// Everything that makes one gun different from another. Local space: origin at the
// pivot (butt plate on the bore axis for long guns, rear of the slide for the pistol),
// barrel along -Z, +Y up.
struct WeaponDef {
    std::string id;
    std::string name;
    WeaponClass cls = WeaponClass::Rifle;
    int caliber = Cal_556;
    uint32_t magMask = 0;   // accepted magazine families (ReloadType::Magazine)
    int tier = 0;           // loot rarity 0 (common) .. 3 (rare)
    int value = 1000;
    int gridW = 4, gridH = 2;

    // Projectile (with standard ammunition; the loaded round modifies it)
    BallisticParams projectile;
    int pellets = 1;
    float pelletSpread = 0.0f;  // cone half-angle (rad)
    float dispersionMoa = 1.0f; // mechanical accuracy of the barrel
    float defaultZero = 100.0f; // m

    // Action / feeding
    ActionType action = ActionType::SemiAuto;
    bool canAuto = false;
    float cyclicRpm = 600.0f;
    float actionCycleTime = 0.0f; // manual bolt / pump cycle (s)
    float actionTravel = 0.0f;    // how far Action parts slide back (m)
    ReloadType reload = ReloadType::Magazine;
    int magazine = 30;            // Tube: internal capacity. Magazine: capacity of the issued magazine
    bool chamberSeparate = true;  // Tube guns: false for revolvers / double barrels (fire straight from the cylinder)
    bool ejects = true;           // spent cases leave the gun on every shot
    float reloadTime = 2.3f;      // tube: time per shell
    float emptyReloadTime = 2.9f; // tube: extra time to chamber the first shell
    CasingType casing = CasingType::Rifle;
    bool suppressed = false;
    float loudness = 160.0f;      // dB SPL at 1 m

    // Recoil (aimed values; hip fire is looser)
    float recoilRise = 1.25f; // rad/s muzzle-up kick
    float recoilYaw = 0.3f;   // rad/s random sideways kick
    float recoilBack = 0.5f;  // m/s rearward push into the shoulder
    float flashScale = 1.0f;
    float smokeScale = 1.0f;

    // Handling: relative to the carbine. Light guns swing faster; heavy guns are steadier.
    float armSpeedScale = 1.0f;
    float armAccelScale = 1.0f;
    float swayScale = 1.0f;
    float adsTime = 0.25f;
    bool oneHanded = false; // pistols: usable with one arm

    // Geometry
    std::vector<Part> parts;
    glm::vec3 muzzle{0.0f}, chamber{0.0f}, ejectionPort{0.0f};
    float ironSightHeight = 0.065f; // iron sight line above the bore
    float ironEyeZ = -0.19f;        // eye position along the weapon when aiming irons
    glm::vec3 hipOffset{0.17f, -0.23f, 0.10f};      // pivot relative to the eye, body frame
    glm::vec3 reloadOffset{0.10f, -0.30f, -0.12f};
    float length = 0.89f;  // pivot to muzzle
    float wallBack = 0.0f; // extra reach behind the pivot (arms, for the pistol)
    float railTopY = 0.033f;
    float railZ = -0.37f;  // centre of the optic mount
    int maxOptic = 0;      // highest compatible index into opticDefs()

    // Forearm attachment (weapon-local) and elbow offsets from the hands (body frame)
    glm::vec3 rightHand{0.0f}, leftHand{0.0f};
    glm::vec3 rightElbowFromHand{0.10f, -0.16f, 0.17f}, leftElbowFromHand{-0.15f, -0.19f, 0.22f};
    bool leftHandOnAction = false; // pump guns: support hand rides the fore-end
    float forearmThickness = 0.075f;
};

enum class OpticType { Iron, RedDot, Magnified };
enum class Reticle { None = 0, Duplex = 1, MilDot = 2, CrossDot = 3 };

struct OpticDef {
    std::string name;
    OpticType type = OpticType::Iron;
    float minMag = 1.0f, maxMag = 1.0f;
    Reticle reticle = Reticle::None;
    float length = 0.0f;
    float ocularRadius = 0.0f;   // eyepiece (or red-dot window) radius
    float objectiveRadius = 0.0f;
    float eyeRelief = 0.0f;
    float axisAboveRail = 0.0f;  // optical axis height over the rail top
};

// The original hand-built guns keep their indices.
enum WeaponId { kPistol = 0, kSmg, kShotgun, kRifle, kSniper };

const std::vector<WeaponDef>& weaponDefs();
int findWeapon(const std::string& id);
const std::vector<OpticDef>& opticDefs();

// Optic geometry for a weapon, in weapon-local space.
void buildOpticParts(const WeaponDef& w, const OpticDef& o, std::vector<Part>& out);
