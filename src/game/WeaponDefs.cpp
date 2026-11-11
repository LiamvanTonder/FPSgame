#include "game/WeaponDefs.h"

#include "items/ItemDefs.h"

#include <algorithm>
#include <cmath>

namespace {

const glm::vec3 kPolymer(0.10f, 0.10f, 0.11f);
const glm::vec3 kMetal(0.16f, 0.16f, 0.17f);
const glm::vec3 kMetalLight(0.22f, 0.22f, 0.23f);
const glm::vec3 kGlove(0.36f, 0.31f, 0.24f);
const glm::vec3 kWalnut(0.36f, 0.22f, 0.12f);
const glm::vec3 kOdGreen(0.24f, 0.27f, 0.20f);
const glm::vec3 kBead(0.95f, 0.85f, 0.55f);

Part box(glm::vec3 c, glm::vec3 s, glm::vec3 col, float tilt = 0.0f, uint8_t flags = 0) {
    return Part{c, s, col, tilt, false, flags};
}
Part cyl(glm::vec3 c, glm::vec3 s, glm::vec3 col, uint8_t flags = 0) { return Part{c, s, col, 0.0f, true, flags}; }

// Rear aperture ("peep") on a tower standing on the rail.
void addPeep(std::vector<Part>& p, float z, float sightY, float railTop) {
    const uint8_t f = Part::RearSight;
    const float hole = 0.0032f, outer = 0.0065f, t = outer - hole;
    float towerTop = sightY - outer;
    p.push_back(box({0, (railTop + towerTop) * 0.5f, z}, {0.008f, towerTop - railTop, 0.010f}, kMetal, 0, f));
    p.push_back(box({-(hole + t * 0.5f), sightY, z}, {t, outer * 2.0f, 0.004f}, kMetal, 0, f));
    p.push_back(box({(hole + t * 0.5f), sightY, z}, {t, outer * 2.0f, 0.004f}, kMetal, 0, f));
    p.push_back(box({0, sightY + hole + t * 0.5f, z}, {hole * 2.0f, t, 0.004f}, kMetal, 0, f));
    p.push_back(box({0, sightY - hole - t * 0.5f, z}, {hole * 2.0f, t, 0.004f}, kMetal, 0, f));
}

// Front post whose top sits exactly on the sight line, with protective ears.
void addPost(std::vector<Part>& p, float z, float sightY, float baseY, uint8_t flags = 0) {
    const float postH = 0.017f;
    float baseTop = sightY - postH;
    p.push_back(box({0, (baseY + baseTop) * 0.5f, z}, {0.014f, baseTop - baseY, 0.018f}, kMetal, 0, flags));
    p.push_back(box({0, sightY - postH * 0.5f, z}, {0.003f, postH, 0.003f}, kPolymer, 0, flags));
    p.push_back(box({-0.009f, sightY - 0.010f, z}, {0.003f, 0.014f, 0.012f}, kMetal, 0, flags));
    p.push_back(box({0.009f, sightY - 0.010f, z}, {0.003f, 0.014f, 0.012f}, kMetal, 0, flags));
}

// Open rear notch: two blocks with a gap whose bottom is the sight line.
void addNotch(std::vector<Part>& p, float z, float sightY, float baseY, uint8_t flags = Part::RearSight) {
    float top = sightY + 0.008f;
    p.push_back(box({-0.0055f, (baseY + top) * 0.5f, z}, {0.004f, top - baseY, 0.006f}, kMetal, 0, flags));
    p.push_back(box({0.0055f, (baseY + top) * 0.5f, z}, {0.004f, top - baseY, 0.006f}, kMetal, 0, flags));
    if (sightY > baseY + 0.0005f)
        p.push_back(box({0, (baseY + sightY) * 0.5f, z}, {0.007f, sightY - baseY, 0.006f}, kMetal, 0, flags));
}

BallisticParams nineMm(float velocity) { return {velocity, 0.0080f, 0.0090f, 0.30f}; }

WeaponDef makePistol() {
    WeaponDef w;
    w.name = "Vanta 9 Service";
    w.id = "vanta9";
    w.cls = WeaponClass::Pistol;
    w.caliber = Cal_9x19;
    w.magMask = magBit(Mag_Pistol9);
    w.tier = 0;
    w.value = 400;
    w.gridW = 2;
    w.gridH = 1;
    w.loudness = 157.0f;
    w.oneHanded = true;
    w.projectile = nineMm(360.0f);
    w.dispersionMoa = 3.0f;
    w.defaultZero = 25.0f;
    w.action = ActionType::SemiAuto;
    w.cyclicRpm = 450.0f;
    w.actionTravel = 0.025f; // slide recoil
    w.magazine = 15;
    w.reloadTime = 1.7f;
    w.emptyReloadTime = 2.1f;
    w.casing = CasingType::Pistol;
    w.recoilRise = 3.4f;
    w.recoilYaw = 0.6f;
    w.recoilBack = 0.35f;
    w.flashScale = 0.7f;
    w.smokeScale = 0.5f;
    w.armSpeedScale = 1.35f;
    w.armAccelScale = 1.9f;
    w.swayScale = 1.6f; // held out at arm's length with no stock
    w.adsTime = 0.20f;

    auto& p = w.parts;
    // Slide (moves) and frame
    p.push_back(box({0, 0.012f, -0.095f}, {0.028f, 0.030f, 0.19f}, kMetal, 0, Part::Action));
    p.push_back(box({0, -0.012f, -0.10f}, {0.026f, 0.022f, 0.17f}, kPolymer));
    p.push_back(box({0, -0.060f, -0.035f}, {0.030f, 0.100f, 0.048f}, kPolymer, -0.30f));
    // Sights ride on the slide
    addNotch(p, -0.012f, 0.032f, 0.027f, Part::RearSight | Part::Action);
    p.push_back(box({0, 0.0295f, -0.180f}, {0.0035f, 0.005f, 0.006f}, kPolymer, 0, Part::Action));
    // Two-handed grip
    p.push_back(box({0, -0.055f, -0.035f}, {0.055f, 0.080f, 0.070f}, kGlove, -0.30f, Part::Hand));
    p.push_back(box({-0.020f, -0.065f, -0.045f}, {0.030f, 0.070f, 0.075f}, kGlove, -0.30f, Part::Hand));

    w.muzzle = {0, 0, -0.19f};
    w.chamber = {0, 0, -0.06f};
    w.ejectionPort = {0.016f, 0.015f, -0.07f};
    w.ironSightHeight = 0.032f;
    w.ironEyeZ = 0.62f; // arms extended: eye far behind the rear sight
    w.hipOffset = {0.04f, -0.24f, -0.50f};
    w.reloadOffset = {0.02f, -0.25f, -0.30f};
    w.length = 0.19f;
    w.wallBack = 0.55f;
    w.railTopY = 0.027f;
    w.railZ = -0.05f;
    w.maxOptic = 1;
    w.rightHand = {0.0f, -0.07f, -0.03f};
    w.leftHand = {-0.02f, -0.07f, -0.04f};
    w.rightElbowFromHand = {0.12f, -0.17f, 0.26f};
    w.leftElbowFromHand = {-0.15f, -0.17f, 0.26f};
    w.forearmThickness = 0.06f;
    return w;
}

WeaponDef makeSmg() {
    WeaponDef w;
    w.name = "Hornet 9 SMG";
    w.id = "hornet9";
    w.cls = WeaponClass::Smg;
    w.caliber = Cal_9x19;
    w.magMask = magBit(Mag_Smg9);
    w.tier = 1;
    w.value = 1500;
    w.gridW = 3;
    w.gridH = 2;
    w.loudness = 158.0f;
    w.projectile = nineMm(400.0f);
    w.dispersionMoa = 2.5f;
    w.defaultZero = 50.0f;
    w.canAuto = true;
    w.cyclicRpm = 800.0f;
    w.magazine = 30;
    w.reloadTime = 2.1f;
    w.emptyReloadTime = 2.6f;
    w.casing = CasingType::Pistol;
    w.recoilRise = 0.85f;
    w.recoilYaw = 0.35f;
    w.recoilBack = 0.25f;
    w.flashScale = 0.7f;
    w.smokeScale = 0.6f;
    w.armSpeedScale = 1.15f;
    w.armAccelScale = 1.35f;
    w.swayScale = 1.0f;
    w.adsTime = 0.22f;

    auto& p = w.parts;
    p.push_back(box({0, -0.010f, -0.09f}, {0.040f, 0.070f, 0.18f}, kPolymer));            // stock
    p.push_back(box({0, 0.008f, -0.32f}, {0.050f, 0.058f, 0.28f}, kMetal));               // receiver
    p.push_back(box({0, 0.041f, -0.32f}, {0.020f, 0.008f, 0.20f}, kMetalLight));          // rail
    p.push_back(box({0, -0.070f, -0.25f}, {0.030f, 0.095f, 0.035f}, kPolymer, -0.30f));   // grip
    p.push_back(box({0, -0.105f, -0.36f}, {0.022f, 0.150f, 0.035f}, kMetal, 0.22f));      // magazine
    p.push_back(box({0, 0.000f, -0.53f}, {0.052f, 0.055f, 0.18f}, kPolymer));             // handguard
    p.push_back(cyl({0, 0, -0.65f}, {0.016f, 0.016f, 0.06f}, kMetal));                    // barrel
    addPost(p, -0.60f, 0.065f, 0.027f);
    addPeep(p, -0.24f, 0.065f, 0.045f);
    p.push_back(box({0, -0.065f, -0.25f}, {0.050f, 0.070f, 0.065f}, kGlove, -0.30f, Part::Hand));
    p.push_back(box({-0.004f, -0.015f, -0.53f}, {0.062f, 0.060f, 0.090f}, kGlove, 0, Part::Hand));

    w.muzzle = {0, 0, -0.68f};
    w.chamber = {0, 0, -0.30f};
    w.ejectionPort = {0.03f, 0.02f, -0.33f};
    w.ironSightHeight = 0.065f;
    w.ironEyeZ = -0.16f;
    w.length = 0.68f;
    w.railTopY = 0.045f;
    w.railZ = -0.32f;
    w.maxOptic = 3;
    w.rightHand = {0.02f, -0.08f, -0.24f};
    w.leftHand = {-0.02f, -0.03f, -0.53f};
    w.leftElbowFromHand = {-0.15f, -0.19f, 0.20f};
    return w;
}

WeaponDef makeShotgun() {
    WeaponDef w;
    w.name = "Brennan 12 Pump";
    w.id = "brennan12";
    w.cls = WeaponClass::Shotgun;
    w.caliber = Cal_12ga;
    w.tier = 0;
    w.value = 900;
    w.gridW = 5;
    w.gridH = 1;
    w.loudness = 161.0f;
    w.projectile = {400.0f, 0.0035f, 0.0084f, 0.47f}; // one 00 buckshot pellet
    w.pellets = 9;
    w.pelletSpread = 0.016f; // ~3 cm of spread per metre of range
    w.dispersionMoa = 4.0f;
    w.defaultZero = 25.0f;
    w.action = ActionType::Pump;
    w.cyclicRpm = 300.0f;
    w.actionCycleTime = 0.55f;
    w.actionTravel = 0.09f;
    w.reload = ReloadType::Tube;
    w.magazine = 6;
    w.reloadTime = 0.5f;
    w.emptyReloadTime = 0.45f;
    w.casing = CasingType::Shell;
    w.recoilRise = 3.0f;
    w.recoilYaw = 0.5f;
    w.recoilBack = 1.1f;
    w.flashScale = 1.5f;
    w.smokeScale = 2.0f;
    w.armSpeedScale = 0.95f;
    w.armAccelScale = 0.85f;
    w.swayScale = 1.0f;
    w.adsTime = 0.28f;

    auto& p = w.parts;
    p.push_back(box({0, -0.030f, -0.17f}, {0.042f, 0.100f, 0.34f}, kWalnut));            // stock
    p.push_back(box({0, -0.060f, -0.36f}, {0.035f, 0.060f, 0.060f}, kWalnut, -0.40f));   // wrist
    p.push_back(box({0, -0.012f, -0.44f}, {0.050f, 0.056f, 0.20f}, kMetal));             // receiver
    p.push_back(box({0, 0.017f, -0.475f}, {0.020f, 0.004f, 0.11f}, kMetalLight));        // rail
    p.push_back(cyl({0, 0, -0.77f}, {0.022f, 0.022f, 0.46f}, kMetal));                   // barrel
    p.push_back(cyl({0, -0.028f, -0.73f}, {0.024f, 0.024f, 0.38f}, kMetal));             // magazine tube
    p.push_back(box({0, -0.030f, -0.68f}, {0.050f, 0.050f, 0.18f}, kPolymer, 0, Part::Action)); // pump
    p.push_back(box({0, 0.012f, -0.985f}, {0.006f, 0.004f, 0.006f}, kMetal));            // bead base
    p.push_back(box({0, 0.017f, -0.985f}, {0.0035f, 0.006f, 0.0035f}, kBead));           // bead
    addNotch(p, -0.40f, 0.020f, 0.016f);
    p.push_back(box({0, -0.065f, -0.36f}, {0.050f, 0.070f, 0.065f}, kGlove, -0.40f, Part::Hand));
    p.push_back(box({-0.004f, -0.035f, -0.68f}, {0.062f, 0.060f, 0.090f}, kGlove, 0, Part::Hand | Part::Action));

    w.muzzle = {0, 0, -1.0f};
    w.chamber = {0, 0, -0.48f};
    w.ejectionPort = {0.03f, 0.0f, -0.45f};
    w.ironSightHeight = 0.020f;
    w.ironEyeZ = -0.12f;
    w.length = 1.0f;
    w.railTopY = 0.019f;
    w.railZ = -0.475f;
    w.maxOptic = 3;
    w.rightHand = {0.02f, -0.08f, -0.35f};
    w.leftHand = {-0.02f, -0.04f, -0.68f};
    w.leftElbowFromHand = {-0.15f, -0.19f, 0.24f};
    w.leftHandOnAction = true;
    return w;
}

WeaponDef makeRifle() {
    WeaponDef w;
    w.name = "Ridgeline 5.56 Carbine";
    w.id = "ridgeline556";
    w.cls = WeaponClass::Rifle;
    w.caliber = Cal_556;
    w.magMask = magBit(Mag_Stanag);
    w.tier = 1;
    w.value = 3200;
    w.gridW = 4;
    w.gridH = 2;
    w.loudness = 162.0f;
    w.projectile = {910.0f, 0.00356f, 0.0057f, 0.25f};
    w.dispersionMoa = 1.0f;
    w.defaultZero = 100.0f;
    w.canAuto = true;
    w.cyclicRpm = 750.0f;
    w.magazine = 30;
    w.reloadTime = 2.3f;
    w.emptyReloadTime = 2.9f;
    w.casing = CasingType::Rifle;
    w.recoilRise = 1.25f;
    w.recoilYaw = 0.30f;
    w.recoilBack = 0.5f;

    auto& p = w.parts;
    p.push_back(box({0, -0.012f, -0.11f}, {0.045f, 0.085f, 0.22f}, kPolymer));          // stock
    p.push_back(box({0, -0.022f, -0.37f}, {0.050f, 0.050f, 0.22f}, kMetal));            // lower receiver
    p.push_back(box({0, 0.012f, -0.37f}, {0.050f, 0.026f, 0.24f}, kMetalLight));        // upper receiver
    p.push_back(box({0, 0.029f, -0.37f}, {0.022f, 0.008f, 0.24f}, kMetal));             // top rail
    p.push_back(box({0, -0.075f, -0.31f}, {0.030f, 0.100f, 0.035f}, kPolymer, -0.35f)); // pistol grip
    p.push_back(box({0, -0.100f, -0.41f}, {0.026f, 0.150f, 0.070f}, kPolymer, 0.20f));  // magazine
    p.push_back(box({0, 0.008f, -0.60f}, {0.056f, 0.058f, 0.22f}, kPolymer));           // handguard
    p.push_back(cyl({0, 0, -0.78f}, {0.018f, 0.018f, 0.20f}, kMetal));                  // barrel
    p.push_back(cyl({0, 0, -0.875f}, {0.022f, 0.022f, 0.03f}, kMetal));                 // flash hider
    addPost(p, -0.775f, 0.065f, 0.008f);
    addPeep(p, -0.27f, 0.065f, 0.033f);
    p.push_back(box({0, -0.07f, -0.305f}, {0.050f, 0.070f, 0.065f}, kGlove, -0.35f, Part::Hand));
    p.push_back(box({-0.004f, -0.018f, -0.62f}, {0.064f, 0.062f, 0.095f}, kGlove, 0, Part::Hand));

    w.muzzle = {0, 0, -0.89f};
    w.chamber = {0, 0, -0.36f};
    w.ejectionPort = {0.03f, 0.015f, -0.38f};
    w.ironSightHeight = 0.065f;
    w.ironEyeZ = -0.19f;
    w.length = 0.89f;
    w.railTopY = 0.033f;
    w.railZ = -0.37f;
    w.maxOptic = 5;
    w.rightHand = {0.02f, -0.08f, -0.29f};
    w.leftHand = {-0.02f, -0.03f, -0.62f};
    return w;
}

WeaponDef makeSniper() {
    WeaponDef w;
    w.name = "Longbow .308";
    w.id = "longbow308";
    w.cls = WeaponClass::Sniper;
    w.caliber = Cal_762x51;
    w.tier = 2;
    w.value = 4200;
    w.gridW = 5;
    w.gridH = 2;
    w.loudness = 166.0f;
    w.projectile = {800.0f, 0.0113f, 0.00782f, 0.26f};
    w.dispersionMoa = 0.5f;
    w.defaultZero = 100.0f;
    w.action = ActionType::Bolt;
    w.cyclicRpm = 60.0f;
    w.actionCycleTime = 1.0f;
    w.actionTravel = 0.08f;
    w.reload = ReloadType::SingleLoad; // no magazine: one round is hand-fed after every shot
    w.magazine = 0;
    w.casing = CasingType::Magnum;
    w.recoilRise = 1.6f;
    w.recoilYaw = 0.30f;
    w.recoilBack = 0.9f;
    w.flashScale = 1.3f;
    w.smokeScale = 1.6f;
    w.armSpeedScale = 0.75f;
    w.armAccelScale = 0.6f;
    w.swayScale = 0.75f; // heavy rifle damps tremor
    w.adsTime = 0.38f;

    auto& p = w.parts;
    p.push_back(box({0, -0.030f, -0.17f}, {0.045f, 0.110f, 0.34f}, kOdGreen));           // stock
    p.push_back(box({0, 0.030f, -0.17f}, {0.035f, 0.020f, 0.16f}, kOdGreen));            // cheek riser
    p.push_back(box({0, 0.000f, -0.47f}, {0.040f, 0.045f, 0.24f}, kMetal));              // receiver
    p.push_back(box({0, 0.029f, -0.47f}, {0.022f, 0.012f, 0.26f}, kMetalLight));         // rail
    p.push_back(cyl({0, 0.0f, -0.40f}, {0.018f, 0.018f, 0.12f}, kMetalLight, Part::Bolt));            // bolt body
    p.push_back(box({0.035f, 0.005f, -0.38f}, {0.045f, 0.010f, 0.010f}, kMetal, 0, Part::Bolt));     // bolt handle
    p.push_back(box({0.058f, 0.005f, -0.38f}, {0.016f, 0.016f, 0.016f}, kPolymer, 0, Part::Bolt));   // bolt knob
    p.push_back(box({0, -0.070f, -0.36f}, {0.032f, 0.100f, 0.040f}, kOdGreen, -0.30f));  // grip
    p.push_back(box({0, -0.020f, -0.72f}, {0.060f, 0.060f, 0.36f}, kOdGreen));           // fore-end
    p.push_back(cyl({0, 0, -0.89f}, {0.022f, 0.022f, 0.52f}, kMetal));                   // barrel
    p.push_back(cyl({0, 0, -1.13f}, {0.028f, 0.028f, 0.045f}, kMetal));                  // muzzle brake
    addNotch(p, -0.36f, 0.053f, 0.035f);
    addPost(p, -1.08f, 0.053f, 0.010f);
    p.push_back(box({0, -0.065f, -0.36f}, {0.050f, 0.070f, 0.065f}, kGlove, -0.30f, Part::Hand));
    p.push_back(box({-0.004f, -0.060f, -0.68f}, {0.062f, 0.050f, 0.100f}, kGlove, 0, Part::Hand));

    w.muzzle = {0, 0, -1.15f};
    w.chamber = {0, 0, -0.45f};
    w.ejectionPort = {0.03f, 0.01f, -0.44f};
    w.ironSightHeight = 0.053f;
    w.ironEyeZ = -0.13f;
    w.length = 1.15f;
    w.railTopY = 0.035f;
    w.railZ = -0.47f;
    w.maxOptic = 5;
    w.rightHand = {0.02f, -0.08f, -0.35f};
    w.leftHand = {-0.02f, -0.07f, -0.68f};
    w.leftElbowFromHand = {-0.15f, -0.17f, 0.24f};
    return w;
}

#include "game/WeaponCatalog.inl"

const std::vector<OpticDef>& opticDefs() {
    //                 name               type                min    max  reticle            len    ocular  obj     relief axis
    static const std::vector<OpticDef> defs = {
        {"Iron sights",    OpticType::Iron,      1.0f,  1.0f,  Reticle::None,     0.0f,  0.0f,   0.0f,   0.0f,  0.0f},
        {"Red dot 1x",     OpticType::RedDot,    1.0f,  1.0f,  Reticle::None,     0.05f, 0.012f, 0.0f,   0.0f,  0.028f, 1},
        {"Prism 2x",       OpticType::Magnified, 2.0f,  2.0f,  Reticle::CrossDot, 0.14f, 0.018f, 0.016f, 0.07f, 0.034f, 2},
        {"Scope 4x",       OpticType::Magnified, 4.0f,  4.0f,  Reticle::Duplex,   0.26f, 0.020f, 0.020f, 0.08f, 0.036f, 3},
        {"Scope 8x",       OpticType::Magnified, 8.0f,  8.0f,  Reticle::MilDot,   0.32f, 0.021f, 0.025f, 0.085f, 0.040f, 3},
        {"Scope 3-12x",    OpticType::Magnified, 3.0f,  12.0f, Reticle::MilDot,   0.36f, 0.021f, 0.028f, 0.09f, 0.042f, 3},
        {"Holographic 1x", OpticType::RedDot,    1.0f,  1.0f,  Reticle::None,     0.09f, 0.018f, 0.0f,   0.0f,  0.032f, 2},
        {"Prism 3x",       OpticType::Magnified, 3.0f,  3.0f,  Reticle::CrossDot, 0.15f, 0.019f, 0.018f, 0.07f, 0.035f, 2},
        {"LPVO 1-6x",      OpticType::Magnified, 1.0f,  6.0f,  Reticle::Duplex,   0.27f, 0.020f, 0.021f, 0.09f, 0.038f, 2},
        {"Compact 4x",     OpticType::Magnified, 4.0f,  4.0f,  Reticle::CrossDot, 0.16f, 0.018f, 0.019f, 0.04f, 0.036f, 2},
        // Sight pictures without items of their own
        {"Offset dot",     OpticType::RedDot,    1.0f,  1.0f,  Reticle::None,     0.04f, 0.010f, 0.0f,   0.0f,  0.030f, 2, false},
        {"Red dot + 3x",   OpticType::Magnified, 3.0f,  3.0f,  Reticle::CrossDot, 0.20f, 0.016f, 0.014f, 0.07f, 0.028f, 2, false},
        {"Holo + 3x",      OpticType::Magnified, 3.0f,  3.0f,  Reticle::CrossDot, 0.20f, 0.016f, 0.016f, 0.07f, 0.032f, 2, false},
    };
    return defs;
}

bool opticFits(const WeaponDef& w, int optic) {
    const auto& o = opticDefs();
    if (optic <= 0) return true;
    if (optic >= int(o.size()) || !o[optic].item) return false;
    int mount = w.opticMount >= 0 ? w.opticMount : (w.maxOptic <= 0 ? 0 : w.maxOptic <= 1 ? 1 : w.maxOptic <= 3 ? 2 : 3);
    return o[optic].mount <= mount;
}

const char* threadName(int t) {
    static const char* n[kThreadCount] = {"none", "9mm pistol thread", ".45 thread", "5.7 thread", "5.56 / .300 thread",
                                          "5.45 thread", "7.62x39 thread", ".308 thread", ".338 thread", "12ga choke"};
    return t >= 0 && t < kThreadCount ? n[t] : "?";
}

const char* stockMountName(int m) {
    static const char* n[kStockMountCount] = {"none", "buffer tube", "folding hinge", "precision chassis", "pistol stock lug"};
    return m >= 0 && m < kStockMountCount ? n[m] : "?";
}

void deriveMounts(WeaponDef& w) {
    if (w.mountsDerived) return;
    w.mountsDerived = true;
    const bool pistol = w.cls == WeaponClass::Pistol;
    const bool revolver = w.reload == ReloadType::Tube && !w.chamberSeparate;
    const bool bullpup = !pistol && w.rightHand.z < -0.40f;

    // Mass: by class, stretched by length (catalogue may set it).
    if (w.mass <= 0.0f) {
        float base = 3.3f;
        switch (w.cls) {
        case WeaponClass::Pistol: base = 0.75f + 2.0f * std::max(0.0f, w.length - 0.15f); break;
        case WeaponClass::Smg: base = 2.6f; break;
        case WeaponClass::Shotgun: base = 3.3f; break;
        case WeaponClass::Rifle: base = 3.3f; break;
        case WeaponClass::Marksman: base = 4.4f; break;
        case WeaponClass::Sniper: base = 5.6f; break;
        case WeaponClass::Lmg: base = 7.2f; break;
        }
        if (!pistol) base *= std::clamp(w.length / 0.90f, 0.7f, 1.5f);
        if (w.caliber == Cal_50BMG) base = 13.5f;
        w.mass = base;
    }

    // Muzzle thread from the calibre.
    if (w.thread < 0) {
        int t = Th_None;
        switch (w.caliber) {
        case Cal_9x19: case Cal_46x30: t = Th_P9; break;
        case Cal_45ACP: case Cal_10mm: t = Th_P45; break;
        case Cal_57x28: t = Th_P57; break;
        case Cal_556: case Cal_300BLK: t = Th_R556; break;
        case Cal_545: t = Th_R545; break;
        case Cal_762x39: t = Th_R762; break;
        case Cal_762x51: case Cal_65: case Cal_762x54R: t = Th_R308; break;
        case Cal_338: t = Th_R338; break;
        case Cal_12ga: t = Th_S12; break;
        default: t = Th_None; break;
        }
        if (w.suppressed || revolver) t = Th_None; // integral can / revolver gap / twin barrels
        w.thread = t;
    }
    int ammo = -1;
    {
        const auto& a = ammoDefs();
        for (int i = 0; i < int(a.size()); ++i)
            if (a[i].caliber == w.caliber) { ammo = i; break; }
        w.bulletDiameter = ammo >= 0 ? a[ammo].diameter : w.projectile.diameter;
    }

    if (w.opticMount < 0) w.opticMount = w.maxOptic <= 0 ? 0 : w.maxOptic <= 1 ? 1 : w.maxOptic <= 3 ? 2 : 3;

    // Tag the built-in stock, grip and crown so fitted parts can replace them.
    float stockFront = 0.0f;
    bool hasStock = false;
    int gripPart = -1;
    float gripBest = 1e9f;
    for (int i = 0; i < int(w.parts.size()); ++i) {
        Part& p = w.parts[i];
        if (p.flags & Part::Hand) continue;
        if (!pistol && !bullpup && p.center.z > w.rightHand.z + 0.04f && !(p.flags & Part::RearSight)) {
            p.flags |= Part::Stock;
            hasStock = true;
            stockFront = std::min(stockFront, p.center.z - p.size.z * 0.5f);
        }
        if (p.cylinder && p.size.z < 0.08f && std::abs(p.center.z - w.muzzle.z) < 0.05f) p.flags |= Part::MuzzleCap;
        float dg = glm::length(p.center - w.rightHand);
        if (p.tiltX != 0.0f && !(p.flags & Part::Stock) && dg < 0.06f && dg < gripBest) {
            gripBest = dg;
            gripPart = i;
        }
    }
    if (gripPart >= 0) w.parts[gripPart].flags |= Part::Grip;
    w.gripMount = gripPart >= 0 && !pistol;
    w.gripPoint = gripPart >= 0 ? w.parts[gripPart].center : w.rightHand;
    w.stockFrontZ = hasStock ? stockFront : -0.25f;

    if (w.stockMount < 0) {
        int m = SM_None;
        if (pistol) m = w.canAuto ? SM_PistolAdapter : SM_None;
        else if (bullpup || !hasStock) m = SM_None;
        else if (w.cls == WeaponClass::Sniper || w.cls == WeaponClass::Marksman) m = SM_Precision;
        else if (w.caliber == Cal_545 || w.caliber == Cal_762x39 || w.caliber == Cal_762x54R || w.cls == WeaponClass::Smg ||
                 w.cls == WeaponClass::Shotgun)
            m = SM_Folder;
        else m = SM_Buffer;
        if (w.caliber == Cal_50BMG) m = SM_None;
        w.stockMount = m;
        w.foldingStock = m == SM_Folder;
    }

    // Rails
    bool twin = w.cls == WeaponClass::Shotgun && revolver;
    if (pistol) {
        w.underRail = !revolver;
        w.sideRail = false;
        w.underPoint = {0.0f, -0.020f, w.muzzle.z * 0.62f};
    } else {
        w.underRail = !w.leftHandOnAction && !twin;
        w.sideRail = !twin;
        w.underPoint = {0.0f, w.leftHand.y - 0.040f, std::max(w.leftHand.z - 0.05f, w.muzzle.z + 0.10f)};
        w.sidePoint = {0.034f, w.leftHand.y + 0.030f, std::max(w.leftHand.z - 0.10f, w.muzzle.z + 0.06f)};
    }
    w.offsetMount = !pistol && w.opticMount >= 2;
}

void buildOpticParts(const WeaponDef& w, const OpticDef& o, std::vector<Part>& out) {
    out.clear();
    if (o.type == OpticType::Iron) return;
    const float axisY = w.railTopY + o.axisAboveRail;
    const float z = w.railZ;

    if (o.type == OpticType::RedDot) {
        // Base and a square window frame around the optical axis.
        const float hole = o.ocularRadius, t = 0.004f, outer = hole + t;
        out.push_back(box({0, w.railTopY + 0.005f, z}, {0.030f, 0.010f, 0.040f}, kPolymer));
        out.push_back(box({-(hole + t * 0.5f), axisY, z}, {t, outer * 2.0f, 0.030f}, kPolymer));
        out.push_back(box({(hole + t * 0.5f), axisY, z}, {t, outer * 2.0f, 0.030f}, kPolymer));
        out.push_back(box({0, axisY + hole + t * 0.5f, z}, {hole * 2.0f, t, 0.030f}, kPolymer));
        out.push_back(box({0, axisY - hole - t * 0.5f, z}, {hole * 2.0f, t, 0.030f}, kPolymer));
        return;
    }

    const float half = o.length * 0.5f;
    if (o.maxMag <= 2.0f) {
        // Compact prism: boxy body
        out.push_back(box({0, axisY, z}, {0.040f, 0.044f, o.length}, kPolymer));
        out.push_back(cyl({0, axisY, z + half - 0.015f}, {o.ocularRadius * 2.2f, o.ocularRadius * 2.2f, 0.03f}, kMetal));
        out.push_back(box({0, (w.railTopY + axisY - 0.022f) * 0.5f, z}, {0.030f, axisY - 0.022f - w.railTopY, 0.06f}, kMetal));
        return;
    }

    const float tube = 0.0127f; // 1" tube
    out.push_back(cyl({0, axisY, z}, {tube * 2.0f, tube * 2.0f, o.length - 0.10f}, kPolymer));
    out.push_back(cyl({0, axisY, z - half + 0.035f}, {o.objectiveRadius * 2.2f, o.objectiveRadius * 2.2f, 0.07f}, kPolymer));
    out.push_back(cyl({0, axisY, z + half - 0.03f}, {o.ocularRadius * 2.2f, o.ocularRadius * 2.2f, 0.06f}, kPolymer));
    // Turrets
    out.push_back(box({0, axisY + tube + 0.009f, z}, {0.022f, 0.018f, 0.022f}, kMetal));
    out.push_back(box({tube + 0.009f, axisY, z}, {0.018f, 0.022f, 0.022f}, kMetal));
    // Rings down to the rail
    for (float dz : {-0.25f, 0.25f}) {
        float rz = z + o.length * dz * 0.8f;
        out.push_back(box({0, (w.railTopY + axisY) * 0.5f, rz}, {0.030f, axisY - w.railTopY, 0.014f}, kMetal));
    }
}
