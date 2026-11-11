#include "items/ItemDefs.h"

#include "game/Attachments.h"
#include "game/WeaponDefs.h"

#include <algorithm>
#include <cstdlib>

namespace {

AmmoDef ammo(const char* id, const char* name, int cal, float mass, float diam, float cd, float vf, float dmg, float pen,
             int value, glm::vec3 tip, float bleed = 1.0f) {
    AmmoDef a;
    a.id = id;
    a.name = name;
    a.caliber = cal;
    a.mass = mass;
    a.diameter = diam;
    a.dragCoefficient = cd;
    a.velocityFactor = vf;
    a.damage = dmg;
    a.penetration = pen;
    a.value = value;
    a.tip = tip;
    a.bleed = bleed;
    return a;
}

const glm::vec3 kFmj(0.72f, 0.42f, 0.22f), kAp(0.12f, 0.12f, 0.12f), kSoft(0.75f, 0.70f, 0.60f), kRed(0.75f, 0.15f, 0.1f),
    kGreen(0.25f, 0.55f, 0.25f), kBlue(0.2f, 0.35f, 0.75f);

std::vector<AmmoDef> buildAmmo() {
    std::vector<AmmoDef> v;
    v.push_back(ammo("9_fmj", "9x19 FMJ", Cal_9x19, 0.0080f, 0.0090f, 0.30f, 1.00f, 1.00f, 0.10f, 2, kFmj));
    v.push_back(ammo("9_ap", "9x19 AP", Cal_9x19, 0.0060f, 0.0090f, 0.28f, 1.12f, 0.85f, 0.60f, 6, kAp));
    v.push_back(ammo("9_jhp", "9x19 JHP", Cal_9x19, 0.0075f, 0.0090f, 0.31f, 1.02f, 1.30f, 0.00f, 4, kSoft, 1.5f));
    v.push_back(ammo("45_fmj", ".45 ACP FMJ", Cal_45ACP, 0.0149f, 0.0115f, 0.30f, 1.00f, 1.00f, 0.10f, 3, kFmj));
    v.push_back(ammo("45_jhp", ".45 ACP JHP", Cal_45ACP, 0.0149f, 0.0115f, 0.31f, 1.00f, 1.30f, 0.00f, 5, kSoft, 1.5f));
    v.push_back(ammo("10_fmj", "10mm FMJ", Cal_10mm, 0.0117f, 0.0102f, 0.30f, 1.00f, 1.00f, 0.20f, 4, kFmj));
    v.push_back(ammo("357_jsp", ".357 JSP", Cal_357, 0.0102f, 0.0091f, 0.30f, 1.00f, 1.15f, 0.20f, 5, kSoft, 1.3f));
    v.push_back(ammo("46_ap", "4.6x30 AP", Cal_46x30, 0.0020f, 0.0046f, 0.24f, 1.00f, 0.90f, 0.70f, 7, kAp));
    v.push_back(ammo("556_fmj", "5.56 FMJ", Cal_556, 0.00356f, 0.0057f, 0.25f, 1.00f, 1.00f, 0.30f, 4, kFmj));
    v.push_back(ammo("556_ap", "5.56 AP", Cal_556, 0.0040f, 0.0057f, 0.24f, 0.97f, 0.85f, 0.75f, 10, kAp));
    v.push_back(ammo("556_sp", "5.56 Soft Point", Cal_556, 0.0040f, 0.0057f, 0.26f, 0.97f, 1.25f, 0.05f, 6, kSoft, 1.5f));
    v.push_back(ammo("545_fmj", "5.45 FMJ", Cal_545, 0.00343f, 0.0056f, 0.24f, 1.00f, 1.00f, 0.30f, 3, kGreen));
    v.push_back(ammo("545_ap", "5.45 AP", Cal_545, 0.0036f, 0.0056f, 0.23f, 0.98f, 0.85f, 0.75f, 9, kAp));
    v.push_back(ammo("762x39_fmj", "7.62x39 FMJ", Cal_762x39, 0.0079f, 0.0079f, 0.30f, 1.00f, 1.00f, 0.35f, 4, kFmj));
    v.push_back(ammo("762x39_ap", "7.62x39 AP", Cal_762x39, 0.0079f, 0.0079f, 0.29f, 1.00f, 0.85f, 0.80f, 11, kAp));
    v.push_back(ammo("762x39_sp", "7.62x39 Soft Point", Cal_762x39, 0.0080f, 0.0079f, 0.31f, 0.98f, 1.25f, 0.05f, 6, kSoft, 1.5f));
    v.push_back(ammo("300_super", ".300 BLK Supersonic", Cal_300BLK, 0.0081f, 0.0078f, 0.27f, 1.00f, 1.00f, 0.35f, 7, kFmj));
    v.push_back(ammo("300_sub", ".300 BLK Subsonic", Cal_300BLK, 0.0143f, 0.0078f, 0.30f, 0.47f, 1.10f, 0.25f, 9, kBlue));
    v.push_back(ammo("308_fmj", "7.62x51 FMJ", Cal_762x51, 0.0095f, 0.00782f, 0.27f, 1.06f, 1.00f, 0.45f, 8, kFmj));
    v.push_back(ammo("308_ap", "7.62x51 AP", Cal_762x51, 0.0097f, 0.00782f, 0.26f, 1.04f, 0.85f, 0.85f, 18, kAp));
    v.push_back(ammo("308_match", "7.62x51 Match", Cal_762x51, 0.0113f, 0.00782f, 0.24f, 1.00f, 1.05f, 0.45f, 14, kRed));
    v.push_back(ammo("54r_fmj", "7.62x54R FMJ", Cal_762x54R, 0.0096f, 0.0079f, 0.27f, 1.00f, 1.00f, 0.45f, 7, kFmj));
    v.push_back(ammo("54r_ap", "7.62x54R AP", Cal_762x54R, 0.0098f, 0.0079f, 0.26f, 0.99f, 0.85f, 0.85f, 16, kAp));
    v.push_back(ammo("338_fmj", ".338 Magnum FMJ", Cal_338, 0.0162f, 0.0086f, 0.23f, 1.00f, 1.00f, 0.70f, 30, kFmj));
    AmmoDef buck = ammo("12_buck", "12ga 00 Buckshot", Cal_12ga, 0.0035f, 0.0084f, 0.47f, 1.00f, 1.00f, 0.00f, 4, kRed);
    buck.pellets = 9;
    v.push_back(buck);
    AmmoDef slug = ammo("12_slug", "12ga Slug", Cal_12ga, 0.028f, 0.018f, 0.40f, 1.00f, 1.20f, 0.30f, 6, kGreen, 1.4f);
    slug.pellets = 1;
    slug.spread = 0.0f;
    v.push_back(slug);
    v.push_back(ammo("57_fmj", "5.7x28 FMJ", Cal_57x28, 0.0020f, 0.0057f, 0.25f, 1.00f, 1.00f, 0.40f, 5, kFmj));
    v.push_back(ammo("57_ap", "5.7x28 AP", Cal_57x28, 0.0021f, 0.0057f, 0.24f, 1.00f, 0.85f, 0.75f, 12, kAp));
    v.push_back(ammo("44_jsp", ".44 Magnum JSP", Cal_44Mag, 0.0156f, 0.0109f, 0.30f, 1.00f, 1.15f, 0.30f, 8, kSoft, 1.3f));
    v.push_back(ammo("65_fmj", "6.5mm FMJ", Cal_65, 0.0093f, 0.0067f, 0.21f, 1.00f, 1.00f, 0.50f, 10, kFmj));
    v.push_back(ammo("65_match", "6.5mm Match", Cal_65, 0.0094f, 0.0067f, 0.19f, 1.00f, 1.05f, 0.50f, 16, kRed));
    v.push_back(ammo("50_fmj", ".50 BMG FMJ", Cal_50BMG, 0.0420f, 0.0130f, 0.22f, 1.00f, 1.00f, 0.90f, 60, kFmj));
    v.push_back(ammo("50_ap", ".50 BMG AP", Cal_50BMG, 0.0430f, 0.0130f, 0.21f, 1.00f, 0.90f, 1.00f, 95, kAp));
    return v;
}

int ammoIndex(const std::vector<AmmoDef>& v, const char* id) {
    for (int i = 0; i < int(v.size()); ++i)
        if (v[i].id == id) return i;
    return -1;
}

ItemDef item(const std::string& id, const std::string& name, const std::string& shortName, ItemCat cat, int w, int h,
             int value, int tier, glm::vec3 color) {
    ItemDef d;
    d.id = id;
    d.name = name;
    d.shortName = shortName;
    d.cat = cat;
    d.w = w;
    d.h = h;
    d.value = value;
    d.tier = tier;
    d.color = color;
    return d;
}

std::vector<ItemDef> buildItems() {
    std::vector<ItemDef> v;
    const auto& weapons = weaponDefs();
    for (int i = 0; i < int(weapons.size()); ++i) {
        const WeaponDef& w = weapons[i];
        std::string shortName = w.name;
        size_t sp = shortName.find(' ');
        if (sp != std::string::npos) sp = shortName.find(' ', sp + 1);
        if (sp != std::string::npos) shortName = shortName.substr(0, sp);
        ItemDef d = item("w_" + w.id, w.name, shortName, ItemCat::Weapon, w.gridW, w.gridH, w.value, w.tier,
                         glm::vec3(0.30f, 0.31f, 0.33f));
        d.weapon = i;
        d.caliber = w.caliber;
        v.push_back(d);
    }

    // Magazines
    struct Mag { const char* id; const char* name; int family, cal, cap, h, value, tier; };
    auto magShort = [](int family) -> const char* {
        const char* n[kMagFamilyCount] = {"9mm", ".45", "10mm", "9mm", ".45", "4.6", "STANAG", "5.45", "7.62", ".308", "54R", ".308", "Box",
                                          "5.7", "5.7", "12ga", "6.5", "Belt", ".338"};
        return family >= 0 && family < kMagFamilyCount ? n[family] : "Mag";
    };
    const Mag mags[] = {
        {"pistol9_15", "9mm pistol mag 15", Mag_Pistol9, Cal_9x19, 15, 1, 40, 0},
        {"pistol9_21", "9mm extended mag 21", Mag_Pistol9, Cal_9x19, 21, 2, 90, 1},
        {"pistol45_8", ".45 pistol mag 8", Mag_Pistol45, Cal_45ACP, 8, 1, 40, 0},
        {"pistol10_15", "10mm pistol mag 15", Mag_Pistol10, Cal_10mm, 15, 1, 60, 1},
        {"smg9_30", "9mm SMG mag 30", Mag_Smg9, Cal_9x19, 30, 2, 80, 0},
        {"smg9_50", "9mm SMG drum 50", Mag_Smg9, Cal_9x19, 50, 2, 300, 2},
        {"smg45_25", ".45 SMG mag 25", Mag_Smg45, Cal_45ACP, 25, 2, 90, 1},
        {"pdw46_40", "4.6mm PDW mag 40", Mag_Pdw46, Cal_46x30, 40, 2, 150, 2},
        {"stanag_20", "STANAG mag 20", Mag_Stanag, -1, 20, 1, 60, 0},
        {"stanag_30", "STANAG mag 30", Mag_Stanag, -1, 30, 2, 90, 0},
        {"stanag_60", "STANAG quad mag 60", Mag_Stanag, -1, 60, 3, 450, 3},
        {"545_30", "5.45 mag 30", Mag_545, Cal_545, 30, 2, 80, 0},
        {"545_45", "5.45 mag 45", Mag_545, Cal_545, 45, 3, 250, 2},
        {"762x39_30", "7.62x39 mag 30", Mag_762x39, Cal_762x39, 30, 2, 90, 0},
        {"762x39_40", "7.62x39 mag 40", Mag_762x39, Cal_762x39, 40, 3, 260, 2},
        {"308_10", "7.62x51 mag 10", Mag_308, Cal_762x51, 10, 1, 120, 1},
        {"308_20", "7.62x51 mag 20", Mag_308, Cal_762x51, 20, 2, 220, 2},
        {"54r_10", "7.62x54R mag 10", Mag_54R, Cal_762x54R, 10, 1, 140, 1},
        {"308bolt_5", ".308 bolt mag 5", Mag_308Bolt, Cal_762x51, 5, 1, 70, 0},
        {"box556_100", "5.56 box mag 100", Mag_Box556, Cal_556, 100, 2, 700, 3},
        {"pistol57_20", "5.7 pistol mag 20", Mag_Pistol57, Cal_57x28, 20, 1, 150, 2},
        {"pdw57_50", "5.7 PDW mag 50", Mag_Pdw57, Cal_57x28, 50, 2, 320, 2},
        {"12box_8", "12ga box mag 8", Mag_12Box, Cal_12ga, 8, 2, 380, 3},
        {"65_10", "6.5mm mag 10", Mag_65, Cal_65, 10, 1, 160, 2},
        {"65_20", "6.5mm mag 20", Mag_65, Cal_65, 20, 2, 280, 2},
        {"box308_100", "7.62x51 belt box 100", Mag_Box308, Cal_762x51, 100, 2, 1100, 3},
        {"pistol9_33", "9mm stick mag 33", Mag_Pistol9, Cal_9x19, 33, 2, 220, 2},
        {"545_60", "5.45 quad mag 60", Mag_545, Cal_545, 60, 3, 520, 3},
        {"762x39_75", "7.62x39 drum 75", Mag_762x39, Cal_762x39, 75, 2, 780, 3},
        {"smg45_40", ".45 SMG stick 40", Mag_Smg45, Cal_45ACP, 40, 2, 260, 2},
        {"338_10", ".338 magnum mag 10", Mag_338, Cal_338, 10, 2, 420, 3},
    };
    for (const Mag& m : mags) {
        ItemDef d = item(std::string("m_") + m.id, m.name, magShort(m.family), ItemCat::Magazine, (m.family == Mag_Box556 || m.family == Mag_Box308) ? 2 : 1, m.h,
                         m.value, m.tier, glm::vec3(0.22f, 0.24f, 0.22f));
        d.magFamily = m.family;
        d.caliber = m.cal; // -1: STANAG takes 5.56 and .300 BLK
        d.capacity = m.cap;
        v.push_back(d);
    }

    // Ammunition stacks
    const auto& ammo = ammoDefs();
    for (int i = 0; i < int(ammo.size()); ++i) {
        const AmmoDef& a = ammo[i];
        int stack = a.caliber == Cal_50BMG ? 10 : a.caliber == Cal_12ga ? 20
                  : (a.caliber == Cal_338 || a.caliber == Cal_762x51 || a.caliber == Cal_762x54R || a.caliber == Cal_65) ? 30
                  : (a.caliber == Cal_9x19 || a.caliber == Cal_45ACP || a.caliber == Cal_10mm || a.caliber == Cal_357 || a.caliber == Cal_44Mag) ? 50 : 60;
        int tier = a.value >= 14 ? 3 : a.value >= 8 ? 2 : a.value >= 5 ? 1 : 0;
        std::string sn = a.name;
        size_t sp = sn.rfind(' ');
        if (sp != std::string::npos) sn = sn.substr(0, sp) + "\n" + sn.substr(sp + 1); // calibre over load
        ItemDef d = item("a_" + a.id, a.name, sn, ItemCat::Ammo, 1, 1, a.value, tier, glm::vec3(0.55f, 0.45f, 0.25f));
        d.ammo = i;
        d.caliber = a.caliber;
        d.stack = stack;
        v.push_back(d);
    }

    // Medical
    struct Med { const char* id; const char* name; const char* shortName; MedKind kind; int w, h, uses; float time, pool; int value, tier; };
    const Med meds[] = {
        {"bandage", "Bandage", "Bndg", MedKind::Bandage, 1, 1, 1, 3.0f, 0.0f, 80, 0},
        {"tourniquet", "Tourniquet", "Tourn", MedKind::Tourniquet, 1, 1, 1, 4.0f, 0.0f, 150, 0},
        {"hemostatic", "Hemostatic dressing", "Hemo", MedKind::Hemostatic, 1, 1, 1, 4.0f, 0.0f, 260, 1},
        {"splint", "Splint", "Splint", MedKind::Splint, 1, 1, 1, 5.0f, 0.0f, 200, 1},
        {"painkillers", "Painkillers", "Pills", MedKind::Painkillers, 1, 1, 4, 2.0f, 0.0f, 180, 1},
        {"fieldkit", "Field trauma kit", "Trauma kit", MedKind::FieldKit, 2, 1, 1, 2.5f, 400.0f, 650, 1},
        {"surgical", "Surgical kit", "Surgery", MedKind::Surgical, 2, 1, 1, 15.0f, 0.0f, 1600, 3},
    };
    for (const Med& m : meds) {
        ItemDef d = item(std::string("med_") + m.id, m.name, m.shortName, ItemCat::Med, m.w, m.h, m.value, m.tier,
                         glm::vec3(0.62f, 0.20f, 0.18f));
        d.med = m.kind;
        d.uses = m.uses;
        d.useTime = m.time;
        d.hpPool = m.pool;
        v.push_back(d);
    }

    // Optics (index 0 is iron sights, not an item)
    const int opticSize[] = {0, 1, 2, 2, 3, 3};
    const int opticValue[] = {0, 450, 900, 1600, 2800, 4200};
    const int opticTier[] = {0, 0, 1, 1, 2, 3};
    const auto& optics = opticDefs();
    for (int i = 1; i < int(optics.size()); ++i) {
        if (!optics[i].item) continue;
        const char* opticShort[] = {"", "Dot", "2x", "4x", "8x", "3-12x", "Holo", "3x", "1-6x", "4x C"};
        const int extraSize[] = {2, 2, 3, 2}, extraValue[] = {1300, 1900, 3900, 2400}, extraTier[] = {1, 1, 2, 2};
        bool extra = i > 5;
        int k = std::min(i - 6, 3);
        ItemDef d = item("o_" + std::to_string(i), optics[i].name, opticShort[std::min(i, 9)], ItemCat::Optic,
                         extra ? extraSize[k] : opticSize[i], 1, extra ? extraValue[k] : opticValue[i], extra ? extraTier[k] : opticTier[i],
                         glm::vec3(0.12f, 0.12f, 0.14f));
        d.optic = i;
        v.push_back(d);
    }

    // Weapon attachments
    const auto& atts = attachmentDefs();
    for (int i = 0; i < int(atts.size()); ++i) {
        const AttachmentDef& a = atts[i];
        ItemDef d = item("x_" + a.id, a.name, a.shortName, ItemCat::Attachment, a.w, a.h, a.value, a.tier, a.color);
        d.attach = i;
        d.mass = a.mass;
        d.caliber = -1;
        v.push_back(d);
    }

    // Body armour and helmets: class, condition points, coverage, mass
    struct Armor { const char* id; const char* name; const char* shortName; ItemCat cat; int w, h, cls; float dur; uint8_t cover; float mass; int value, tier; glm::vec3 col; };
    const Armor armors[] = {
        {"arm_soft", "Soft armour vest (class 1)", "Vest I", ItemCat::Armor, 3, 3, 1, 50.0f, AC_Thorax | AC_Stomach, 2.6f, 800, 0, {0.28f, 0.30f, 0.26f}},
        {"arm_patrol", "Patrol plate carrier (class 2)", "Plates II", ItemCat::Armor, 3, 3, 2, 60.0f, AC_Thorax, 4.8f, 2200, 1, {0.36f, 0.34f, 0.26f}},
        {"arm_assault", "Assault plates (class 3)", "Plates III", ItemCat::Armor, 3, 3, 3, 75.0f, AC_Thorax | AC_Stomach, 8.0f, 5500, 2, {0.22f, 0.26f, 0.22f}},
        {"arm_heavy", "Heavy plate armour (class 4)", "Plates IV", ItemCat::Armor, 3, 4, 4, 90.0f, AC_Thorax | AC_Stomach, 11.5f, 12000, 3, {0.16f, 0.17f, 0.18f}},
        {"helm_bump", "Bump helmet (class 1)", "Helm I", ItemCat::Helmet, 2, 2, 1, 30.0f, AC_Head, 0.8f, 500, 0, {0.30f, 0.30f, 0.28f}},
        {"helm_ballistic", "Ballistic helmet (class 2)", "Helm II", ItemCat::Helmet, 2, 2, 2, 40.0f, AC_Head, 1.4f, 1800, 1, {0.32f, 0.35f, 0.26f}},
        {"helm_heavy", "Heavy helmet (class 3)", "Helm III", ItemCat::Helmet, 2, 2, 3, 50.0f, AC_Head, 2.3f, 4800, 2, {0.20f, 0.22f, 0.20f}},
    };
    for (const Armor& a : armors) {
        ItemDef d = item(a.id, a.name, a.shortName, a.cat, a.w, a.h, a.value, a.tier, a.col);
        d.armorClass = a.cls;
        d.durability = a.dur;
        d.coverage = a.cover;
        d.mass = a.mass;
        v.push_back(d);
    }

    // Load-bearing gear
    struct Gear { const char* id; const char* name; ItemCat cat; int w, h, gw, gh, value, tier; };
    const Gear gear[] = {
        {"rig_belt", "Field belt", ItemCat::Rig, 2, 2, 4, 1, 150, 0},
        {"rig_recon", "Recon rig", ItemCat::Rig, 3, 3, 4, 2, 600, 1},
        {"rig_assault", "Assault chest rig", ItemCat::Rig, 3, 3, 5, 3, 1400, 2},
        {"bp_day", "Day sack", ItemCat::Backpack, 3, 3, 4, 4, 400, 0},
        {"bp_patrol", "Patrol pack", ItemCat::Backpack, 4, 4, 5, 5, 1100, 1},
        {"bp_ruck", "Long-range ruck", ItemCat::Backpack, 4, 5, 6, 6, 2400, 2},
    };
    for (const Gear& g : gear) {
        std::string sn = g.name;
        sn = sn.substr(0, sn.find(' '));
        ItemDef d = item(g.id, g.name, sn, g.cat, g.w, g.h, g.value, g.tier,
                         g.cat == ItemCat::Rig ? glm::vec3(0.30f, 0.33f, 0.22f) : glm::vec3(0.36f, 0.29f, 0.20f));
        d.gridW = g.gw;
        d.gridH = g.gh;
        v.push_back(d);
    }
    return v;
}

} // namespace

const std::vector<AmmoDef>& ammoDefs() {
    static const std::vector<AmmoDef> defs = buildAmmo();
    return defs;
}

const std::vector<ItemDef>& itemDefs() {
    static const std::vector<ItemDef> defs = buildItems();
    return defs;
}

int findItemDef(const std::string& id) {
    const auto& d = itemDefs();
    for (int i = 0; i < int(d.size()); ++i)
        if (d[i].id == id) return i;
    return -1;
}

int findAmmo(const std::string& id) { return ammoIndex(ammoDefs(), id.c_str()); }

int weaponItemDef(int weapon) {
    const auto& d = itemDefs();
    for (int i = 0; i < int(d.size()); ++i)
        if (d[i].cat == ItemCat::Weapon && d[i].weapon == weapon) return i;
    return -1;
}

int opticItemDef(int optic) {
    const auto& d = itemDefs();
    for (int i = 0; i < int(d.size()); ++i)
        if (d[i].cat == ItemCat::Optic && d[i].optic == optic) return i;
    return -1;
}

int attachmentItemDef(int attachment) {
    static std::vector<int> map;
    if (map.empty()) {
        map.assign(attachmentDefs().size(), -1);
        const auto& d = itemDefs();
        for (int i = 0; i < int(d.size()); ++i)
            if (d[i].cat == ItemCat::Attachment && d[i].attach >= 0 && d[i].attach < int(map.size())) map[d[i].attach] = i;
    }
    return attachment >= 0 && attachment < int(map.size()) ? map[attachment] : -1;
}

int defaultAmmo(int caliber) {
    const auto& a = ammoDefs();
    for (int i = 0; i < int(a.size()); ++i)
        if (a[i].caliber == caliber) return i;
    return -1;
}

int magazineItemFor(int weapon) {
    const WeaponDef& w = weaponDefs()[weapon];
    if (w.reload != ReloadType::Magazine) return -1;
    const auto& d = itemDefs();
    int best = -1;
    for (int i = 0; i < int(d.size()); ++i) {
        if (d[i].cat != ItemCat::Magazine || !(w.magMask & magBit(d[i].magFamily))) continue;
        if (d[i].caliber >= 0 && d[i].caliber != w.caliber) continue;
        // Prefer the issue magazine: the one closest to the gun's nominal capacity.
        if (best < 0 || std::abs(d[i].capacity - w.magazine) < std::abs(d[best].capacity - w.magazine)) best = i;
    }
    return best;
}

const char* categoryName(ItemCat c) {
    switch (c) {
    case ItemCat::Weapon: return "Weapon";
    case ItemCat::Magazine: return "Magazine";
    case ItemCat::Ammo: return "Ammunition";
    case ItemCat::Med: return "Medical";
    case ItemCat::Optic: return "Optic";
    case ItemCat::Rig: return "Rig";
    case ItemCat::Backpack: return "Backpack";
    case ItemCat::Attachment: return "Attachment";
    case ItemCat::Armor: return "Body armour";
    case ItemCat::Helmet: return "Helmet";
    }
    return "?";
}
