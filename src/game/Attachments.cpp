#include "game/Attachments.h"

#include "items/Item.h"

#include <algorithm>
#include <cmath>

namespace {

const glm::vec3 kBlack(0.09f, 0.09f, 0.10f);
const glm::vec3 kDark(0.14f, 0.14f, 0.15f);
const glm::vec3 kSteel(0.24f, 0.24f, 0.25f);
const glm::vec3 kTanA(0.50f, 0.44f, 0.32f);
const glm::vec3 kOd(0.25f, 0.28f, 0.21f);
const glm::vec3 kLens(0.85f, 0.88f, 0.70f);
const glm::vec3 kLaserRed(0.75f, 0.10f, 0.08f);

Part box(glm::vec3 c, glm::vec3 s, glm::vec3 col, float tilt = 0.0f) { return Part{c, s, col, tilt, false, 0}; }
Part cyl(glm::vec3 c, glm::vec3 s, glm::vec3 col) { return Part{c, s, col, 0.0f, true, 0}; }

AttachmentDef muzzle(const char* id, const char* name, const char* sn, AttSpecial sp, int thread, float bore, float len, float mass,
                     int value, int tier) {
    AttachmentDef a;
    a.id = id;
    a.name = name;
    a.shortName = sn;
    a.slot = AS_Muzzle;
    a.special = sp;
    a.mount = thread;
    a.bore = bore;
    a.length = len;
    a.mass = mass;
    a.value = value;
    a.tier = tier;
    a.w = len > 0.12f ? 2 : 1;
    switch (sp) {
    case AttSpecial::Suppressor:
        a.flashMul = 0.06f;
        a.smokeMul = 0.6f;
        a.velocityMul = 1.015f;
        a.backMul = 0.88f; // the can traps gas: a little less push
        break;
    case AttSpecial::Brake:
        a.backMul = 0.65f;
        a.riseMul = 0.85f;
        a.loudnessDb = 3.0f;
        a.flashMul = 1.4f;
        break;
    case AttSpecial::Compensator:
        a.riseMul = 0.75f;
        a.yawMul = 0.9f;
        a.loudnessDb = 1.5f;
        a.flashMul = 1.15f;
        break;
    case AttSpecial::FlashHider: a.flashMul = 0.3f; break;
    default: break;
    }
    return a;
}

AttachmentDef other(const char* id, const char* name, const char* sn, int slot, AttSpecial sp, float mass, int value, int tier,
                    int w = 1, int h = 1) {
    AttachmentDef a;
    a.id = id;
    a.name = name;
    a.shortName = sn;
    a.slot = slot;
    a.special = sp;
    a.mass = mass;
    a.value = value;
    a.tier = tier;
    a.w = w;
    a.h = h;
    return a;
}

std::vector<AttachmentDef> build() {
    std::vector<AttachmentDef> v;
    auto sup = [&](const char* id, const char* name, int th, float boreMm, float len, float mass, float db, int value, int tier) {
        AttachmentDef a = muzzle(id, name, "Supp", AttSpecial::Suppressor, th, boreMm * 0.001f, len, mass, value, tier);
        a.loudnessDb = db;
        a.color = kDark;
        v.push_back(a);
    };
    // ---------- Suppressors: thread, largest bullet (mm), length, mass, dB
    sup("sup_p9", "Hush 9 pistol suppressor", Th_P9, 9.2f, 0.16f, 0.26f, -26.0f, 1200, 1);
    sup("sup_p45", "Hush 45 suppressor", Th_P45, 11.7f, 0.18f, 0.32f, -24.0f, 1400, 1);
    sup("sup_p57", "Hush 5.7 suppressor", Th_P57, 5.9f, 0.15f, 0.21f, -25.0f, 1300, 2);
    sup("sup_r556", "Quell 556 suppressor", Th_R556, 5.9f, 0.17f, 0.45f, -27.0f, 2200, 2);
    sup("sup_r30", "Quell 30 multi-calibre suppressor", Th_R556, 7.9f, 0.19f, 0.55f, -26.0f, 3200, 2);
    sup("sup_r545", "Quell 545 suppressor", Th_R545, 5.8f, 0.17f, 0.45f, -27.0f, 2100, 2);
    sup("sup_r762", "Quell 762 suppressor", Th_R762, 8.0f, 0.19f, 0.55f, -26.0f, 2600, 2);
    sup("sup_r308", "Quell 308 suppressor", Th_R308, 8.0f, 0.21f, 0.65f, -25.0f, 3600, 3);
    sup("sup_r338", "Quell 338 magnum suppressor", Th_R338, 8.7f, 0.23f, 0.78f, -24.0f, 5200, 3);
    // ---------- Brakes, compensators, flash hiders
    auto dev = [&](const char* id, const char* name, const char* sn, AttSpecial sp, int th, float len, float mass, int value, int tier) {
        AttachmentDef a = muzzle(id, name, sn, sp, th, 0.0f, len, mass, value, tier);
        a.color = sp == AttSpecial::Brake ? kSteel : kBlack;
        v.push_back(a);
    };
    dev("brk_r556", "Bulwark 556 muzzle brake", "Brake", AttSpecial::Brake, Th_R556, 0.06f, 0.09f, 350, 0);
    dev("brk_r762", "Bulwark 762 muzzle brake", "Brake", AttSpecial::Brake, Th_R762, 0.07f, 0.11f, 380, 0);
    dev("brk_r308", "Bulwark 308 muzzle brake", "Brake", AttSpecial::Brake, Th_R308, 0.07f, 0.13f, 500, 1);
    dev("brk_r338", "Bulwark 338 muzzle brake", "Brake", AttSpecial::Brake, Th_R338, 0.08f, 0.16f, 750, 1);
    dev("cmp_p9", "Slipstream 9 compensator", "Comp", AttSpecial::Compensator, Th_P9, 0.035f, 0.05f, 300, 0);
    dev("cmp_r556", "Slipstream 556 compensator", "Comp", AttSpecial::Compensator, Th_R556, 0.055f, 0.08f, 320, 0);
    dev("cmp_r545", "Slipstream 545 compensator", "Comp", AttSpecial::Compensator, Th_R545, 0.055f, 0.08f, 300, 0);
    dev("fh_r556", "Birdcage 556 flash hider", "Flash", AttSpecial::FlashHider, Th_R556, 0.055f, 0.06f, 150, 0);
    dev("fh_r762", "Birdcage 762 flash hider", "Flash", AttSpecial::FlashHider, Th_R762, 0.06f, 0.07f, 160, 0);
    dev("fh_r308", "Birdcage 308 flash hider", "Flash", AttSpecial::FlashHider, Th_R308, 0.065f, 0.08f, 220, 0);
    // ---------- Shotgun chokes
    auto choke = [&](const char* id, const char* name, const char* sn, float spread, int value) {
        AttachmentDef a = muzzle(id, name, sn, AttSpecial::Choke, Th_S12, 0.0f, 0.03f, 0.05f, value, 0);
        a.spreadMul = spread;
        a.color = kSteel;
        v.push_back(a);
    };
    choke("chk_full", "Full choke", "Full", 0.70f, 180);
    choke("chk_mod", "Modified choke", "Mod", 0.85f, 140);
    choke("chk_cyl", "Cylinder choke", "Cyl", 1.20f, 120);

    // ---------- Underbarrel
    {
        AttachmentDef a = other("vg", "Vertical foregrip", "V grip", AS_Under, AttSpecial::VerticalGrip, 0.10f, 250, 0);
        a.yawMul = 0.85f;
        a.swayMul = 0.92f;
        a.longOnly = true;
        v.push_back(a);
        a = other("ag", "Angled foregrip", "A grip", AS_Under, AttSpecial::AngledGrip, 0.07f, 280, 0);
        a.adsMul = 0.90f;
        a.riseMul = 0.93f;
        a.longOnly = true;
        v.push_back(a);
        a = other("hs", "Handstop", "Stop", AS_Under, AttSpecial::Handstop, 0.03f, 120, 0);
        a.backMul = 0.92f;
        a.longOnly = true;
        v.push_back(a);
        a = other("bp_light", "Folding bipod", "Bipod", AS_Under, AttSpecial::Bipod, 0.30f, 700, 1, 2, 1);
        a.longOnly = true;
        v.push_back(a);
        a = other("bp_heavy", "Heavy precision bipod", "H bipod", AS_Under, AttSpecial::Bipod, 0.50f, 1400, 2, 2, 1);
        a.heavy = true;
        a.longOnly = true;
        v.push_back(a);
        a = other("pl_light", "Pistol weapon light", "P light", AS_Under, AttSpecial::Light, 0.10f, 380, 0);
        a.pistolOnly = true;
        v.push_back(a);
        a = other("pl_combo", "Pistol laser-light", "P combo", AS_Under, AttSpecial::LaserLight, 0.12f, 650, 1);
        a.pistolOnly = true;
        v.push_back(a);
    }
    // ---------- Side rail
    {
        AttachmentDef a = other("sl_light", "Rail flashlight", "Light", AS_Side, AttSpecial::Light, 0.14f, 420, 0);
        v.push_back(a);
        a = other("sl_laser", "Visible laser module", "Laser", AS_Side, AttSpecial::Laser, 0.09f, 520, 1);
        v.push_back(a);
        a = other("sl_combo", "Laser-light combo", "Combo", AS_Side, AttSpecial::LaserLight, 0.20f, 950, 2);
        v.push_back(a);
    }
    // ---------- Stocks
    auto stock = [&](const char* id, const char* name, const char* sn, int mount, AttSpecial sp, float mass, int value, int tier) {
        AttachmentDef a = other(id, name, sn, AS_Stock, sp, mass, value, tier, 2, 1);
        a.mount = mount;
        a.color = mount == SM_Precision ? kOd : kBlack;
        v.push_back(a);
        return &v.back();
    };
    stock("st_carbine", "Collapsible carbine stock", "Stock", SM_Buffer, AttSpecial::Stock, 0.26f, 220, 0)->adsMul = 0.95f;
    {
        AttachmentDef* a = stock("st_precision", "Adjustable precision stock", "P stock", SM_Buffer, AttSpecial::Stock, 0.55f, 900, 1);
        a->swayMul = 0.82f;
        a->backMul = 0.92f;
        a = stock("st_pad", "Recoil-pad stock", "Pad", SM_Buffer, AttSpecial::Stock, 0.40f, 600, 1);
        a->backMul = 0.80f;
        a->riseMul = 0.93f;
        a = stock("st_skeleton", "Skeleton folding stock", "Skel", SM_Folder, AttSpecial::FoldingStock, 0.20f, 300, 0);
        a->adsMul = 0.92f;
        a->swayMul = 1.05f;
        a = stock("st_folder", "Polymer folding stock", "Fold", SM_Folder, AttSpecial::FoldingStock, 0.35f, 420, 0);
        a->backMul = 0.90f;
        a = stock("st_chassis", "Precision chassis stock", "Chassis", SM_Precision, AttSpecial::Stock, 0.90f, 1800, 2);
        a->swayMul = 0.75f;
        a->backMul = 0.85f;
        a->color = kTanA;
        a = stock("st_pistol", "Pistol stock adapter", "P stock", SM_PistolAdapter, AttSpecial::PistolStock, 0.30f, 700, 1);
        a->backMul = 0.55f;
        a->riseMul = 0.65f;
        a->swayMul = 0.55f;
    }
    // ---------- Pistol grips
    {
        AttachmentDef a = other("gr_ergo", "Ergonomic pistol grip", "Ergo", AS_Grip, AttSpecial::Grip, 0.07f, 160, 0);
        a.adsMul = 0.95f;
        v.push_back(a);
        a = other("gr_target", "Target pistol grip", "Target", AS_Grip, AttSpecial::Grip, 0.10f, 240, 1);
        a.swayMul = 0.92f;
        v.push_back(a);
        a = other("gr_rubber", "Rubberised grip", "Rubber", AS_Grip, AttSpecial::Grip, 0.08f, 180, 0);
        a.riseMul = 0.95f;
        a.yawMul = 0.95f;
        v.push_back(a);
    }
    // ---------- Top rail extras
    {
        AttachmentDef a = other("offset_dot", "45-degree offset red dot", "Offset", AS_Offset, AttSpecial::OffsetSight, 0.06f, 650, 1);
        a.longOnly = true;
        v.push_back(a);
        a = other("magnifier", "3x flip magnifier", "Magnif", AS_Magnifier, AttSpecial::Magnifier, 0.30f, 1100, 2);
        a.longOnly = true;
        v.push_back(a);
    }
    return v;
}

float opticMass(int optic) {
    const float m[] = {0.0f, 0.12f, 0.40f, 0.50f, 0.65f, 0.75f, 0.32f, 0.42f, 0.60f, 0.45f};
    return optic >= 0 && optic < 10 ? m[optic] : 0.3f;
}

} // namespace

const std::vector<AttachmentDef>& attachmentDefs() {
    static const std::vector<AttachmentDef> defs = build();
    return defs;
}

int findAttachment(const std::string& id) {
    const auto& d = attachmentDefs();
    for (int i = 0; i < int(d.size()); ++i)
        if (d[i].id == id) return i;
    return -1;
}

const char* attachSlotName(int slot) {
    static const char* n[kAttachSlots] = {"Muzzle", "Underbarrel", "Side rail", "Stock", "Pistol grip", "Offset rail", "Magnifier"};
    return slot >= 0 && slot < kAttachSlots ? n[slot] : "?";
}

const char* attachSpecialName(AttSpecial s) {
    switch (s) {
    case AttSpecial::Suppressor: return "suppressor";
    case AttSpecial::Brake: return "muzzle brake";
    case AttSpecial::Compensator: return "compensator";
    case AttSpecial::FlashHider: return "flash hider";
    case AttSpecial::Choke: return "choke";
    case AttSpecial::VerticalGrip: return "vertical grip";
    case AttSpecial::AngledGrip: return "angled grip";
    case AttSpecial::Handstop: return "handstop";
    case AttSpecial::Bipod: return "bipod";
    case AttSpecial::Light: return "weapon light";
    case AttSpecial::Laser: return "laser";
    case AttSpecial::LaserLight: return "laser-light";
    case AttSpecial::Stock: return "stock";
    case AttSpecial::FoldingStock: return "folding stock";
    case AttSpecial::PistolStock: return "pistol stock";
    case AttSpecial::Grip: return "pistol grip";
    case AttSpecial::OffsetSight: return "offset sight";
    case AttSpecial::Magnifier: return "magnifier";
    default: return "attachment";
    }
}

bool attachmentFits(const WeaponDef& w, int att, const Item* weapon, std::string* why) {
    auto no = [&](const char* r) {
        if (why) *why = r;
        return false;
    };
    if (att < 0 || att >= int(attachmentDefs().size())) return no("Not an attachment");
    const AttachmentDef& a = attachmentDefs()[att];
    const bool pistol = w.cls == WeaponClass::Pistol;
    if (a.pistolOnly && !pistol) return no("Made for pistols");
    if (a.longOnly && pistol) return no("Doesn't fit a pistol");
    switch (a.slot) {
    case AS_Muzzle:
        if (w.thread == Th_None) return no("This barrel isn't threaded");
        if (a.mount != w.thread) return no("Wrong muzzle thread");
        if (a.bore > 0.0f && w.bulletDiameter > a.bore + 1e-5f) return no("Bore too small for this calibre");
        return true;
    case AS_Under:
        if (!w.underRail) return no("No underbarrel rail");
        if (a.heavy && w.cls != WeaponClass::Lmg && w.cls != WeaponClass::Sniper && w.cls != WeaponClass::Marksman)
            return no("Heavy bipod: machine guns and precision rifles only");
        return true;
    case AS_Side: return w.sideRail ? true : no("No side rail");
    case AS_Stock:
        if (w.stockMount == SM_None) return no("The stock can't be changed");
        if (a.mount != w.stockMount) return no("Different stock mount");
        return true;
    case AS_Grip: return w.gripMount ? true : no("No separate pistol grip");
    case AS_Offset: return w.offsetMount ? true : no("Top rail too short");
    case AS_Magnifier:
        if (!w.offsetMount) return no("Top rail too short");
        if (weapon && weapon->optic > 0 && opticDefs()[weapon->optic].type != OpticType::RedDot) return no("Needs a red dot or holographic sight");
        return true;
    }
    return no("?");
}

float weaponMass(const WeaponDef& w) { return w.mass; }

std::shared_ptr<const WeaponDef> buildEffectiveWeapon(const Item& item) {
    const WeaponDef& base = weaponDefs()[item.d().weapon];
    auto out = std::make_shared<WeaponDef>(base);
    WeaponDef& w = *out;
    const auto& defs = attachmentDefs();

    // Mass and moment of inertia about the pivot, starting from the bare gun.
    const bool pistol = base.cls == WeaponClass::Pistol;
    float comZ = pistol ? -0.05f : -0.45f * base.length;
    float M = base.mass;
    float I = M * comZ * comZ + M * base.length * base.length / 12.0f;
    const float baseM = M, baseI = I;
    auto addMass = [&](float m, float z) {
        M += m;
        I += m * z * z;
    };
    if (item.optic > 0) addMass(opticMass(item.optic), base.railZ);

    uint8_t hide = 0;
    std::vector<Part> extra;
    float rise = 1.0f, yaw = 1.0f, back = 1.0f, sway = 1.0f, ads = 1.0f;
    glm::vec3 leftHand = base.leftHand;
    bool foldable = base.foldingStock;
    bool stockFitted = false;

    for (int s = 0; s < kAttachSlots; ++s) {
        int ai = item.att[s];
        if (ai < 0 || ai >= int(defs.size())) continue;
        const AttachmentDef& a = defs[ai];
        rise *= a.riseMul;
        yaw *= a.yawMul;
        back *= a.backMul;
        sway *= a.swayMul;
        ads *= a.adsMul;
        w.loudness += a.loudnessDb;
        w.flashScale *= a.flashMul;
        w.smokeScale *= a.smokeMul;
        w.projectile.muzzleVelocity *= a.velocityMul;
        w.pelletSpread *= a.spreadMul;
        switch (s) {
        case AS_Muzzle: {
            float z0 = base.muzzle.z, len = a.length;
            float r = std::max(0.010f, base.bulletDiameter * 1.6f);
            hide |= Part::MuzzleCap;
            switch (a.special) {
            case AttSpecial::Suppressor: {
                float R = pistol ? 0.016f : 0.021f;
                extra.push_back(cyl({0, 0, z0 - len * 0.5f}, {R * 2.0f, R * 2.0f, len}, a.color));
                extra.push_back(cyl({0, 0, z0 - 0.01f}, {R * 1.4f, R * 1.4f, 0.02f}, kSteel)); // mount
                w.suppressed = true;
                break;
            }
            case AttSpecial::Brake:
                extra.push_back(cyl({0, 0, z0 - len * 0.5f}, {r * 2.4f, r * 2.4f, len}, a.color));
                extra.push_back(box({0, 0, z0 - len * 0.5f}, {r * 3.4f, r * 0.9f, len * 0.5f}, kDark)); // side ports
                break;
            case AttSpecial::Compensator:
                extra.push_back(cyl({0, 0, z0 - len * 0.5f}, {r * 2.2f, r * 2.2f, len}, a.color));
                extra.push_back(box({0, r * 1.0f, z0 - len * 0.5f}, {r * 0.8f, r * 0.6f, len * 0.7f}, kDark)); // top ports
                break;
            case AttSpecial::FlashHider:
                extra.push_back(cyl({0, 0, z0 - len * 0.5f}, {r * 2.0f, r * 2.0f, len}, a.color));
                break;
            default: // choke ring
                extra.push_back(cyl({0, 0, z0 - len * 0.5f}, {0.026f, 0.026f, len}, a.color));
                break;
            }
            w.muzzle.z -= len;
            w.length += len;
            addMass(a.mass, z0 - len * 0.5f);
            break;
        }
        case AS_Under: {
            glm::vec3 p = base.underPoint;
            switch (a.special) {
            case AttSpecial::VerticalGrip:
                extra.push_back(box(p + glm::vec3(0, -0.045f, 0), {0.030f, 0.085f, 0.034f}, a.color));
                leftHand = p + glm::vec3(-0.002f, -0.060f, 0.0f);
                break;
            case AttSpecial::AngledGrip:
                extra.push_back(box(p + glm::vec3(0, -0.018f, 0.015f), {0.026f, 0.030f, 0.075f}, a.color, -0.55f));
                leftHand.z = p.z + 0.035f;
                break;
            case AttSpecial::Handstop:
                extra.push_back(box(p + glm::vec3(0, -0.012f, -0.03f), {0.022f, 0.024f, 0.030f}, a.color));
                break;
            case AttSpecial::Bipod: {
                float zz = std::max(base.muzzle.z + 0.12f, p.z - 0.10f);
                float legL = a.heavy ? 0.22f : 0.18f;
                extra.push_back(box({0, p.y + 0.004f, zz}, {0.030f, 0.020f, 0.040f}, kSteel));
                extra.push_back(box({-0.012f, p.y - 0.004f, zz + legL * 0.5f}, {0.010f, 0.010f, legL}, a.color));
                extra.push_back(box({0.012f, p.y - 0.004f, zz + legL * 0.5f}, {0.010f, 0.010f, legL}, a.color));
                w.bipod = true;
                w.heavyBipod = a.heavy;
                w.bipodPoint = {0.0f, p.y - (a.heavy ? 0.26f : 0.22f), zz};
                break;
            }
            case AttSpecial::Light: case AttSpecial::LaserLight:
                extra.push_back(box(p, {0.026f, 0.026f, 0.060f}, a.color));
                extra.push_back(cyl({0, p.y, p.z - 0.031f}, {0.020f, 0.020f, 0.004f}, kLens));
                if (a.special == AttSpecial::LaserLight) extra.push_back(box({0.008f, p.y - 0.010f, p.z - 0.032f}, {0.005f, 0.005f, 0.004f}, kLaserRed));
                w.light |= a.special == AttSpecial::LaserLight ? 3 : 1;
                w.lightPoint = {0.0f, p.y, p.z - 0.034f};
                break;
            default: break;
            }
            addMass(a.mass, p.z);
            break;
        }
        case AS_Side: {
            glm::vec3 p = base.sidePoint;
            extra.push_back(box(p + glm::vec3(0.006f, 0, 0), {0.016f, 0.020f, 0.012f}, kBlack)); // rail clamp
            if (a.special == AttSpecial::Laser) {
                extra.push_back(box(p + glm::vec3(0.020f, 0, 0), {0.020f, 0.022f, 0.050f}, a.color));
                extra.push_back(box(p + glm::vec3(0.020f, 0, -0.026f), {0.006f, 0.006f, 0.004f}, kLaserRed));
                w.light |= 2;
            } else {
                extra.push_back(cyl(p + glm::vec3(0.024f, 0, 0), {0.026f, 0.026f, 0.110f}, a.color));
                extra.push_back(cyl(p + glm::vec3(0.024f, 0, -0.056f), {0.030f, 0.030f, 0.004f}, kLens));
                if (a.special == AttSpecial::LaserLight) extra.push_back(box(p + glm::vec3(0.024f, 0.018f, -0.050f), {0.010f, 0.010f, 0.030f}, kLaserRed));
                w.light |= a.special == AttSpecial::LaserLight ? 3 : 1;
            }
            w.lightPoint = p + glm::vec3(0.024f, 0.0f, -0.058f);
            addMass(a.mass, p.z);
            break;
        }
        case AS_Stock: {
            hide |= Part::Stock;
            stockFitted = true;
            float z0 = base.stockFrontZ, L = -z0;
            if (a.special == AttSpecial::PistolStock) {
                // Shoulder stock on the grip lug: the pistol becomes a short carbine.
                extra.push_back(box({0, -0.040f, 0.13f}, {0.022f, 0.020f, 0.26f}, a.color));
                extra.push_back(box({0, -0.060f, 0.26f}, {0.034f, 0.090f, 0.022f}, a.color));
                w.oneHanded = false;
                w.ironEyeZ = 0.20f;
                w.hipOffset = {0.17f, -0.20f, -0.16f};
                w.wallBack = 0.0f;
                w.armSpeedScale *= 0.80f;
                addMass(a.mass, 0.14f);
                break;
            }
            switch (a.special) {
            case AttSpecial::FoldingStock:
                if (std::string(a.id) == "st_skeleton") {
                    extra.push_back(box({0, 0.008f, z0 * 0.5f}, {0.016f, 0.014f, L}, a.color));
                    extra.push_back(box({0, -0.050f, z0 * 0.5f}, {0.016f, 0.014f, L}, a.color));
                    extra.push_back(box({0, -0.020f, -0.012f}, {0.040f, 0.085f, 0.022f}, a.color));
                } else {
                    extra.push_back(box({0, -0.016f, z0 * 0.5f}, {0.040f, 0.075f, L}, a.color));
                }
                foldable = true;
                break;
            default:
                if (a.mount == SM_Buffer) {
                    extra.push_back(cyl({0, 0.0f, z0 * 0.55f}, {0.030f, 0.030f, L * 0.9f}, kBlack)); // buffer tube
                    extra.push_back(box({0, -0.010f, z0 * 0.35f}, {0.040f, 0.070f, L * 0.62f}, a.color));
                    extra.push_back(box({0, -0.020f, -0.012f}, {0.044f, 0.120f, 0.024f}, a.color)); // butt pad
                } else {
                    extra.push_back(box({0, -0.025f, z0 * 0.5f}, {0.046f, 0.100f, L}, a.color));
                    extra.push_back(box({0, 0.030f, z0 * 0.6f}, {0.034f, 0.020f, L * 0.5f}, kBlack)); // cheek riser
                }
                foldable = false;
                break;
            }
            addMass(a.mass, z0 * 0.5f);
            break;
        }
        case AS_Grip:
            hide |= Part::Grip;
            extra.push_back(box(base.gripPoint, {0.032f, 0.105f, 0.040f}, a.color, -0.30f));
            addMass(a.mass, base.gripPoint.z);
            break;
        case AS_Offset: {
            float r = base.railTopY + 0.030f;
            float c = 0.7071f;
            glm::vec3 p{r * c, r * c, base.railZ + 0.10f};
            extra.push_back(box({0.016f, base.railTopY + 0.006f, p.z}, {0.030f, 0.012f, 0.030f}, kBlack, 0.0f));
            extra.push_back(box(p, {0.022f, 0.022f, 0.026f}, a.color));
            w.offsetSight = true;
            w.offsetPoint = p;
            addMass(a.mass, p.z);
            break;
        }
        case AS_Magnifier: {
            float y = base.railTopY + 0.032f;
            float z = base.railZ + 0.10f;
            extra.push_back(box({0, base.railTopY + 0.006f, z}, {0.030f, 0.012f, 0.030f}, kBlack));
            extra.push_back(cyl({0, y, z}, {0.032f, 0.032f, 0.090f}, a.color));
            w.magnifier = true;
            addMass(a.mass, z);
            break;
        }
        }
    }

    // A folded stock: tucked along the receiver; the gun is shorter to swing but has no shoulder.
    w.foldable = foldable && !(stockFitted && !foldable);
    w.folded = item.folded && w.foldable;
    if (w.folded) {
        hide |= Part::Stock;
        for (auto it = extra.begin(); it != extra.end();) // fitted stock parts sit behind the receiver
            it = it->center.z > base.stockFrontZ - 0.002f && it->center.z < 0.0f && !it->cylinder ? extra.erase(it) : it + 1;
        float L = -base.stockFrontZ;
        extra.push_back(box({0.034f, -0.010f, base.stockFrontZ - L * 0.45f}, {0.012f, 0.050f, L * 0.9f}, kBlack));
        back *= 1.6f;
        rise *= 1.3f;
        sway *= 1.5f;
        ads *= 0.85f;
        I *= 0.8f;
    }

    // Replace the hidden built-in parts, move the support hand onto a fitted foregrip.
    if (hide) {
        std::vector<Part> keep;
        keep.reserve(w.parts.size());
        for (const Part& p : w.parts)
            if (!(p.flags & hide)) keep.push_back(p);
        w.parts.swap(keep);
    }
    if (leftHand != base.leftHand) {
        int best = -1;
        float bd = 1e9f;
        for (int i = 0; i < int(w.parts.size()); ++i) {
            if (!(w.parts[i].flags & Part::Hand)) continue;
            float d = glm::length(w.parts[i].center - base.leftHand);
            if (d < bd) {
                bd = d;
                best = i;
            }
        }
        if (best >= 0) w.parts[best].center += leftHand - base.leftHand;
        w.leftHand = leftHand;
    }
    w.parts.insert(w.parts.end(), extra.begin(), extra.end());

    // Physics of the extra weight: recoil velocity falls with mass (momentum), the arms
    // swing a heavier, front-loaded gun more slowly, and a heavier gun trembles less.
    float massRatio = baseM / M;
    float inertiaRatio = baseI / std::max(I, 1e-4f);
    w.recoilRise *= rise * massRatio;
    w.recoilYaw *= yaw * massRatio;
    w.recoilBack *= back * massRatio;
    w.armAccelScale *= std::pow(inertiaRatio, 0.8f);
    w.armSpeedScale *= std::pow(inertiaRatio, 0.35f);
    w.swayScale *= sway * std::pow(massRatio, 0.3f);
    w.adsTime *= ads * std::pow(1.0f / massRatio, 0.35f);
    w.mass = M;
    w.inertia = I;
    return out;
}

const WeaponDef& Item::weaponDef() const {
    const ItemDef& dd = d();
    if (dd.cat != ItemCat::Weapon) return weaponDefs()[0];
    unsigned long long k = 1469598103934665603ull;
    auto mix = [&](long long v) { k = (k ^ (unsigned long long)(v + 7)) * 1099511628211ull; };
    mix(def);
    mix(optic);
    mix(folded ? 1 : 0);
    for (int a : att) mix(a);
    if (!eff || k != effKey) {
        // Keep the previous build alive for anyone still holding a reference this tick.
        static thread_local std::vector<std::shared_ptr<const WeaponDef>> graveyard;
        if (eff) {
            graveyard.push_back(eff);
            if (graveyard.size() > 64) graveyard.erase(graveyard.begin());
        }
        eff = buildEffectiveWeapon(*this);
        effKey = k;
    }
    return *eff;
}
