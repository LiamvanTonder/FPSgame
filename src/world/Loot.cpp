#include "world/Loot.h"

#include "game/Attachments.h"
#include "game/WeaponDefs.h"
#include "physics/World.h"
#include "render/Renderer.h"
#include "world/Terrain.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace {
float u01(std::mt19937& rng) { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
int pick(std::mt19937& rng, int n) { return n <= 0 ? 0 : int(u01(rng) * n) % n; }

int rollTier(int bias, std::mt19937& rng) {
    std::normal_distribution<float> n(0.35f + 0.75f * bias, 0.85f);
    return std::clamp(int(std::round(n(rng))), 0, 3);
}

// Calibre of a random gun, weighted toward common ones (ammo boxes).
int randomCaliber(std::mt19937& rng) {
    const auto& w = weaponDefs();
    for (int tries = 0; tries < 10; ++tries) {
        const WeaponDef& d = w[pick(rng, int(w.size()))];
        if (u01(rng) < 1.0f / (1.0f + d.tier)) return d.caliber;
    }
    return Cal_9x19;
}

int ammoOfCaliber(int caliber, int tier, std::mt19937& rng) {
    const auto& a = ammoDefs();
    std::vector<int> options;
    for (int i = 0; i < int(a.size()); ++i)
        if (a[i].caliber == caliber) options.push_back(i);
    if (options.empty()) return -1;
    // Better loads are rarer: the first listed is the common one.
    int idx = 0;
    if (options.size() > 1 && u01(rng) < 0.25f + 0.15f * tier) idx = 1 + pick(rng, int(options.size()) - 1);
    return options[idx];
}

int medItem(std::mt19937& rng, int tier) {
    const char* common[] = {"med_bandage", "med_bandage", "med_tourniquet", "med_painkillers", "med_splint", "med_fieldkit"};
    const char* rare[] = {"med_hemostatic", "med_fieldkit", "med_surgical"};
    if (u01(rng) < 0.12f + 0.12f * tier) return findItemDef(rare[pick(rng, 3)]);
    return findItemDef(common[pick(rng, 6)]);
}

void placeOrDrop(Grid& g, ItemPtr it) {
    if (it) g.autoPlace(it);
}

// Value-weighted pick, like guns: the dearer, the rarer; better sources flatten the curve.
float valueWeight(int value, float quality) {
    float v = std::max(80.0f, float(value)) / 500.0f;
    return std::pow(v, -1.3f + 0.3f * std::clamp(quality, 0.0f, 3.5f));
}
} // namespace

int randomOptic(const WeaponDef& w, float quality, std::mt19937& rng, bool magnifiedOnly) {
    const auto& o = opticDefs();
    std::vector<std::pair<int, float>> opts;
    float total = 0.0f;
    for (int i = 1; i < int(o.size()); ++i) {
        if (!opticFits(w, i) || (magnifiedOnly && o[i].type != OpticType::Magnified)) continue;
        int item = opticItemDef(i);
        if (item < 0) continue;
        float wgt = valueWeight(itemDefs()[item].value, quality);
        opts.push_back({i, wgt});
        total += wgt;
    }
    if (opts.empty()) return 0;
    float r = u01(rng) * total;
    for (auto& p : opts)
        if ((r -= p.second) <= 0.0f) return p.first;
    return opts.back().first;
}

int randomAttachment(float quality, std::mt19937& rng, const Item* gun) {
    const auto& a = attachmentDefs();
    std::vector<std::pair<int, float>> opts;
    float total = 0.0f;
    for (int i = 0; i < int(a.size()); ++i) {
        if (gun && (gun->att[a[i].slot] >= 0 || !attachmentFits(weaponDefs()[gun->d().weapon], i, gun))) continue;
        float wgt = valueWeight(a[i].value, quality);
        opts.push_back({i, wgt});
        total += wgt;
    }
    if (opts.empty()) return -1;
    float r = u01(rng) * total;
    for (auto& p : opts)
        if ((r -= p.second) <= 0.0f) return p.first;
    return opts.back().first;
}

void rollAttachments(Item& gun, float quality, std::mt19937& rng, int count) {
    for (int k = 0; k < count; ++k) {
        int a = randomAttachment(quality, rng, &gun);
        if (a >= 0) gun.att[attachmentDefs()[a].slot] = a;
    }
    gun.attachmentsChanged();
}

const char* containerName(ContainerType t) {
    switch (t) {
    case ContainerType::WeaponCrate: return "Weapon crate";
    case ContainerType::AmmoBox: return "Ammo box";
    case ContainerType::MedCabinet: return "Medical cabinet";
    case ContainerType::Duffel: return "Duffel bag";
    case ContainerType::Toolbox: return "Toolbox";
    }
    return "Container";
}

bool LootContainer::searched(int who) const { return std::find(searchedBy.begin(), searchedBy.end(), who) != searchedBy.end(); }

float weaponWeight(int weapon, int quality) {
    // Rarity is value: the more a gun is worth, the less often it turns up.
    // Better sources (hotspots, well-equipped raiders) flatten the curve.
    float v = std::max(200.0f, float(weaponDefs()[weapon].value)) / 1000.0f;
    return std::pow(v, -1.4f + 0.3f * float(std::clamp(quality, 0, 3)));
}

int pickWeaponByValue(int quality, std::mt19937& rng, bool allowLong) {
    const auto& w = weaponDefs();
    float total = 0.0f;
    for (int i = 0; i < int(w.size()); ++i)
        if (allowLong || w[i].cls == WeaponClass::Pistol) total += weaponWeight(i, quality);
    float r = u01(rng) * total;
    for (int i = 0; i < int(w.size()); ++i) {
        if (!allowLong && w[i].cls != WeaponClass::Pistol) continue;
        r -= weaponWeight(i, quality);
        if (r <= 0.0f) return i;
    }
    return allowLong ? int(w.size()) - 1 : kPistol;
}

ItemPtr rollWeapon(int tier, std::mt19937& rng, bool allowLong) {
    const auto& w = weaponDefs();
    int wi = pickWeaponByValue(tier, rng, allowLong);
    if (wi < 0) return nullptr;
    const WeaponDef& d = w[wi];
    // Optics: more likely on better guns; bolt rifles nearly always carry a scope.
    int optic = 0;
    bool precision = d.cls == WeaponClass::Sniper || d.cls == WeaponClass::Marksman;
    if (d.opticMount > 0) {
        float chance = d.cls == WeaponClass::Sniper ? 0.95f : precision ? 0.8f : 0.2f + 0.2f * tier;
        if (u01(rng) < chance) optic = randomOptic(d, float(tier), rng, precision);
    }
    int ammo = ammoOfCaliber(d.caliber, tier, rng);
    ItemPtr item = makeLoadedWeapon(wi, optic, ammo);
    // Found guns sometimes carry a few attachments.
    int n = 0;
    for (int k = 0; k < 3; ++k)
        if (u01(rng) < 0.18f + 0.1f * tier) ++n;
    if (n) rollAttachments(*item, float(tier), rng, n);
    // Loot guns are not always full.
    if (item->mag && u01(rng) < 0.5f) item->mag->rounds.resize(size_t(item->mag->rounds.size() * u01(rng)));
    return item;
}

void addAmmoFor(Grid& g, int weapon, int mags, int loose, std::mt19937& rng, int ammoDef) {
    const WeaponDef& d = weaponDefs()[weapon];
    if (ammoDef < 0) ammoDef = ammoOfCaliber(d.caliber, 0, rng);
    int magDef = magazineItemFor(weapon);
    for (int i = 0; i < mags && magDef >= 0; ++i) placeOrDrop(g, makeFullMagazine(magDef, ammoDef));
    while (loose > 0) {
        int stack = itemDefs()[findItemDef("a_" + ammoDefs()[ammoDef].id)].stack;
        int n = std::min(loose, stack);
        placeOrDrop(g, makeAmmo(ammoDef, n));
        loose -= n;
    }
}

namespace {
// Loot-zone fills: mid-tier gear as a rule, with a real chance of the good stuff. Everything is
// still picked by value, so the dearest items stay rare here too.
float zoneQuality(std::mt19937& rng) { return u01(rng) < 0.1f ? 3.0f : 1.8f; }

ItemPtr zoneGun(std::mt19937& rng, bool allowLong) {
    float q = zoneQuality(rng);
    int wi = allowLong ? pickWeaponBand(q, rng, 0, 1000000, u01(rng) < 0.15f) : pickWeaponBand(q, rng, 0, 1000000, true);
    const WeaponDef& d = weaponDefs()[wi];
    bool precision = d.cls == WeaponClass::Sniper || d.cls == WeaponClass::Marksman;
    int optic = d.opticMount > 0 && (precision || u01(rng) < 0.55f) ? randomOptic(d, q, rng, precision) : 0;
    ItemPtr g = makeLoadedWeapon(wi, optic, ammoOfCaliber(d.caliber, int(q), rng));
    int n = 0;
    for (int k = 0; k < 3; ++k) n += u01(rng) < 0.35f ? 1 : 0;
    if (n) rollAttachments(*g, q, rng, n);
    if (g->mag && u01(rng) < 0.4f) g->mag->rounds.resize(size_t(g->mag->rounds.size() * u01(rng)));
    return g;
}

ItemPtr zoneArmor(std::mt19937& rng) {
    // Armour and helmets weighted by value (class 4 plates are rare even here).
    const auto& defs = itemDefs();
    float q = zoneQuality(rng), total = 0.0f;
    std::vector<std::pair<int, float>> opts;
    for (int i = 0; i < int(defs.size()); ++i)
        if (defs[i].cat == ItemCat::Armor || defs[i].cat == ItemCat::Helmet) {
            float w = valueWeight(defs[i].value, q);
            opts.push_back({i, w});
            total += w;
        }
    float r = u01(rng) * total;
    for (auto& o : opts)
        if ((r -= o.second) <= 0.0f) {
            ItemPtr a = makeItem(o.first);
            a->condition = a->d().durability * (0.55f + 0.45f * u01(rng));
            return a;
        }
    return makeItem(opts.back().first);
}

void fillLootZone(LootContainer& c, std::mt19937& rng) {
    Grid& g = c.grid;
    switch (c.type) {
    case ContainerType::WeaponCrate: {
        g = Grid(6, 4);
        int n = 1 + (u01(rng) < 0.6f ? 1 : 0);
        for (int i = 0; i < n; ++i) {
            ItemPtr w = zoneGun(rng, true);
            int wi = w->d().weapon;
            placeOrDrop(g, std::move(w));
            addAmmoFor(g, wi, 1 + pick(rng, 2), int(u01(rng) * 50.0f), rng, ammoOfCaliber(weaponDefs()[wi].caliber, 2, rng));
        }
        for (int k = 0; k < 2; ++k)
            if (u01(rng) < 0.5f) {
                int a = randomAttachment(zoneQuality(rng), rng, nullptr);
                if (a >= 0) placeOrDrop(g, makeItem(attachmentItemDef(a)));
            }
        break;
    }
    case ContainerType::AmmoBox: {
        g = Grid(4, 2);
        int n = 3 + pick(rng, 3);
        for (int i = 0; i < n; ++i) {
            int a = ammoOfCaliber(randomCaliber(rng), 2 + pick(rng, 2), rng);
            if (a >= 0) placeOrDrop(g, makeAmmo(a, 20 + pick(rng, 40)));
        }
        const auto& defs = itemDefs();
        std::vector<int> mags;
        for (int i = 0; i < int(defs.size()); ++i)
            if (defs[i].cat == ItemCat::Magazine) mags.push_back(i);
        for (int k = 0; k < 2; ++k)
            if (u01(rng) < 0.6f) placeOrDrop(g, makeItem(mags[pick(rng, int(mags.size()))]));
        break;
    }
    case ContainerType::MedCabinet: {
        g = Grid(3, 3);
        int n = 2 + pick(rng, 3);
        for (int i = 0; i < n; ++i) placeOrDrop(g, makeItem(medItem(rng, 2)));
        if (u01(rng) < 0.25f) placeOrDrop(g, makeItem("med_surgical"));
        break;
    }
    default: {
        g = c.type == ContainerType::Toolbox ? Grid(3, 2) : Grid(5, 4);
        int n = 2 + pick(rng, 3);
        for (int i = 0; i < n; ++i) {
            float k = u01(rng);
            if (k < 0.3f) {
                ItemPtr a = zoneArmor(rng);
                if (a->d().w <= g.w && a->d().h <= g.h) placeOrDrop(g, std::move(a));
            } else if (k < 0.55f) {
                int a = randomAttachment(zoneQuality(rng), rng, nullptr);
                if (a >= 0) placeOrDrop(g, makeItem(attachmentItemDef(a)));
            } else if (k < 0.7f) {
                int o = opticItemDef(randomOptic(weaponDefs()[kSniper], zoneQuality(rng), rng, false));
                if (o >= 0) placeOrDrop(g, makeItem(o));
            } else if (k < 0.85f) {
                ItemPtr w = zoneGun(rng, u01(rng) < 0.4f);
                if (w->d().w <= g.w && w->d().h <= g.h) placeOrDrop(g, std::move(w));
            } else {
                placeOrDrop(g, makeItem(medItem(rng, 2)));
            }
        }
        break;
    }
    }
}
} // namespace

void fillContainer(LootContainer& c, std::mt19937& rng) {
    if (c.lootZone) {
        fillLootZone(c, rng);
        return;
    }
    Grid& g = c.grid;
    switch (c.type) {
    case ContainerType::WeaponCrate: {
        g = Grid(6, 4);
        int n = 1 + (u01(rng) < 0.45f ? 1 : 0);
        for (int i = 0; i < n; ++i) {
            ItemPtr w = rollWeapon(rollTier(c.tier, rng), rng);
            if (!w) continue;
            int wi = w->d().weapon;
            placeOrDrop(g, std::move(w));
            addAmmoFor(g, wi, pick(rng, 2), int(u01(rng) * 40.0f), rng);
        }
        if (u01(rng) < 0.35f) {
            int o = opticItemDef(randomOptic(weaponDefs()[kSniper], float(rollTier(c.tier, rng)), rng, false));
            if (o >= 0) placeOrDrop(g, makeItem(o));
        }
        for (int k = 0; k < 2; ++k)
            if (u01(rng) < 0.35f) {
                int a = randomAttachment(float(rollTier(c.tier, rng)), rng, nullptr);
                if (a >= 0) placeOrDrop(g, makeItem(attachmentItemDef(a)));
            }
        break;
    }
    case ContainerType::AmmoBox: {
        g = Grid(4, 2);
        int n = 2 + pick(rng, 3);
        for (int i = 0; i < n; ++i) {
            int a = ammoOfCaliber(randomCaliber(rng), rollTier(c.tier, rng), rng);
            if (a >= 0) placeOrDrop(g, makeAmmo(a, 10 + pick(rng, 40)));
        }
        if (u01(rng) < 0.4f) {
            const auto& defs = itemDefs();
            std::vector<int> mags;
            for (int i = 0; i < int(defs.size()); ++i)
                if (defs[i].cat == ItemCat::Magazine && defs[i].tier <= rollTier(c.tier, rng)) mags.push_back(i);
            if (!mags.empty()) placeOrDrop(g, makeItem(mags[pick(rng, int(mags.size()))]));
        }
        break;
    }
    case ContainerType::MedCabinet: {
        g = Grid(3, 3);
        int n = 1 + pick(rng, 3);
        for (int i = 0; i < n; ++i) placeOrDrop(g, makeItem(medItem(rng, rollTier(c.tier, rng))));
        break;
    }
    case ContainerType::Toolbox: {
        g = Grid(3, 2);
        if (u01(rng) < 0.3f) {
            int o = opticItemDef(randomOptic(weaponDefs()[kRifle], float(rollTier(c.tier, rng)), rng, false));
            if (o >= 0) placeOrDrop(g, makeItem(o));
        }
        if (u01(rng) < 0.45f) {
            int a = randomAttachment(float(rollTier(c.tier, rng)), rng, nullptr);
            if (a >= 0) placeOrDrop(g, makeItem(attachmentItemDef(a)));
        }
        int a = ammoOfCaliber(randomCaliber(rng), 0, rng);
        if (a >= 0) placeOrDrop(g, makeAmmo(a, 5 + pick(rng, 25)));
        if (u01(rng) < 0.3f) placeOrDrop(g, makeItem(medItem(rng, 0)));
        break;
    }
    case ContainerType::Duffel: {
        g = Grid(5, 3);
        int n = 1 + pick(rng, 3);
        for (int i = 0; i < n; ++i) {
            float k = u01(rng);
            if (k < 0.18f) {
                ItemPtr w = rollWeapon(rollTier(c.tier, rng), rng, u01(rng) < 0.3f);
                if (w && w->d().w <= 5 && w->d().h <= 3) placeOrDrop(g, std::move(w));
            } else if (k < 0.5f) {
                int a = ammoOfCaliber(randomCaliber(rng), rollTier(c.tier, rng), rng);
                if (a >= 0) placeOrDrop(g, makeAmmo(a, 10 + pick(rng, 30)));
            } else if (k < 0.75f) {
                placeOrDrop(g, makeItem(medItem(rng, rollTier(c.tier, rng))));
            } else if (k < 0.8f && u01(rng) < 0.5f) {
                const char* armor[] = {"arm_soft", "helm_bump", "arm_soft", "helm_bump", "arm_patrol", "helm_ballistic"};
                ItemPtr a = makeItem(armor[std::min(5, pick(rng, 4) + rollTier(c.tier, rng))]);
                a->condition = a->d().durability * (0.4f + 0.6f * u01(rng));
                if (a->d().w <= 5 && a->d().h <= 3) placeOrDrop(g, std::move(a));
            } else if (k < 0.9f) {
                const char* gear[] = {"rig_belt", "rig_recon", "bp_day", "bp_patrol"};
                ItemPtr it = makeItem(gear[pick(rng, 4)]);
                if (it && it->d().w <= 5 && it->d().h <= 3) placeOrDrop(g, std::move(it));
            } else if (k < 0.95f) {
                int a = randomAttachment(float(rollTier(c.tier, rng)), rng, nullptr);
                if (a >= 0) placeOrDrop(g, makeItem(attachmentItemDef(a)));
            } else {
                int o = opticItemDef(randomOptic(weaponDefs()[kRifle], float(rollTier(c.tier, rng)), rng, false));
                if (o >= 0) placeOrDrop(g, makeItem(o));
            }
        }
        break;
    }
    }
}

BotKitRoll rollBotTier(std::mt19937& rng) {
    // 35 % barebones pistol, 40 % cheap gun, 15 % solid, 10 % better and better.
    float r = u01(rng);
    BotKitRoll k;
    k.tier = r < 0.35f ? 0 : r < 0.75f ? 1 : r < 0.90f ? 2 : 3;
    float u = u01(rng);
    k.quality = k.tier == 3 ? u * u : 0.0f; // skewed low: the very best kits are rare
    return k;
}

int pickWeaponBand(float quality, std::mt19937& rng, int lo, int hi, bool pistols) {
    const auto& w = weaponDefs();
    auto weight = [&](int i) {
        float v = std::max(200.0f, float(w[i].value)) / 1000.0f;
        return std::pow(v, -1.4f + 0.3f * std::clamp(quality, 0.0f, 3.5f));
    };
    float total = 0.0f;
    for (int i = 0; i < int(w.size()); ++i)
        if ((w[i].cls == WeaponClass::Pistol) == pistols && w[i].value >= lo && w[i].value <= hi) total += weight(i);
    if (total <= 0.0f) return pistols ? kPistol : kRifle;
    float r = u01(rng) * total;
    for (int i = 0; i < int(w.size()); ++i) {
        if ((w[i].cls == WeaponClass::Pistol) != pistols || w[i].value < lo || w[i].value > hi) continue;
        if ((r -= weight(i)) <= 0.0f) return i;
    }
    return pistols ? kPistol : kRifle;
}

namespace {
// A raider's gun: full magazine of the chosen load, an optic and attachments by quality.
ItemPtr kitGun(int wi, float quality, int optChance100, int attachments, int ammoDef, std::mt19937& rng, bool suppressorBias = false) {
    const WeaponDef& d = weaponDefs()[wi];
    bool precision = d.cls == WeaponClass::Sniper || d.cls == WeaponClass::Marksman;
    int optic = 0;
    if (d.opticMount > 0 && (precision || int(u01(rng) * 100.0f) < optChance100)) optic = randomOptic(d, quality, rng, precision);
    ItemPtr g = makeLoadedWeapon(wi, optic, ammoDef);
    if (suppressorBias && d.thread != Th_None) {
        const auto& a = attachmentDefs();
        std::vector<int> cans;
        for (int i = 0; i < int(a.size()); ++i)
            if (a[i].special == AttSpecial::Suppressor && attachmentFits(d, i)) cans.push_back(i);
        if (!cans.empty()) g->att[AS_Muzzle] = cans[pick(rng, int(cans.size()))];
    }
    if (attachments > 0) rollAttachments(*g, quality, rng, attachments);
    g->attachmentsChanged();
    return g;
}

void giveAmmo(Equipment& eq, Grid& g, int wi, int mags, int loose, int ammoDef, std::mt19937& rng) {
    const WeaponDef& wd = weaponDefs()[wi];
    if (wd.reload == ReloadType::Magazine) addAmmoFor(g, wi, mags, loose, rng, ammoDef);
    else addAmmoFor(g, wi, 0, wd.reload == ReloadType::SingleLoad ? 12 + loose / 3 : 14 + loose / 2, rng, ammoDef);
    (void)eq;
}
} // namespace

void makeLoadout(Equipment& eq, int tier, std::mt19937& rng, float q) {
    eq.clear();
    tier = std::clamp(tier, 0, 3);
    auto meds = [&](int n, int medTier) {
        for (int i = 0; i < n; ++i) {
            ItemPtr m = makeItem(medItem(rng, medTier));
            eq.stow(m);
        }
    };
    ItemPtr bandage = makeItem("med_bandage");

    if (tier == 0) {
        // Barebones: a cheap pistol, a belt and a bandage.
        eq.slots[ES_Rig] = makeItem("rig_belt");
        int wi = pickWeaponBand(0.0f, rng, 0, 1500, true);
        int ammo = ammoOfCaliber(weaponDefs()[wi].caliber, 0, rng);
        eq.slots[ES_Hand0] = kitGun(wi, 0.0f, 0, 0, ammo, rng);
        giveAmmo(eq, *eq.slots[ES_Rig]->grid, wi, 1 + pick(rng, 2), 0, ammo, rng);
        eq.stow(bandage);
        return;
    }
    if (tier == 1) {
        // A cheap real gun, a bag and basic gear.
        eq.slots[ES_Rig] = makeItem(u01(rng) < 0.5f ? "rig_belt" : "rig_recon");
        eq.slots[ES_Backpack] = makeItem("bp_day");
        if (u01(rng) < 0.25f) eq.slots[ES_Armor] = makeItem("arm_soft");
        if (u01(rng) < 0.20f) eq.slots[ES_Helmet] = makeItem("helm_bump");
        int wi = pickWeaponBand(0.3f, rng, 0, 2500, false);
        int ammo = ammoOfCaliber(weaponDefs()[wi].caliber, 0, rng);
        eq.slots[ES_Hand0] = kitGun(wi, 0.3f, 25, u01(rng) < 0.15f ? 1 : 0, ammo, rng);
        giveAmmo(eq, *eq.slots[ES_Rig]->grid, wi, 2 + pick(rng, 2), 20 + pick(rng, 20), ammo, rng);
        if (u01(rng) < 0.20f) {
            int si = pickWeaponBand(0.0f, rng, 0, 800, true);
            eq.slots[ES_Hand1] = kitGun(si, 0.0f, 0, 0, -1, rng);
            addAmmoFor(eq.pockets, si, 1, 0, rng);
        }
        eq.stow(bandage);
        meds(1 + pick(rng, 2), 0);
        return;
    }
    if (tier == 2) {
        // Solid: two cheap guns, or one lower-medium gun; good gear.
        eq.slots[ES_Rig] = makeItem(u01(rng) < 0.5f ? "rig_recon" : "rig_assault");
        eq.slots[ES_Backpack] = makeItem("bp_patrol");
        eq.slots[ES_Armor] = makeItem(u01(rng) < 0.25f ? "arm_assault" : "arm_patrol");
        eq.slots[ES_Helmet] = makeItem(u01(rng) < 0.5f ? "helm_ballistic" : "helm_bump");
        Grid& rig = *eq.slots[ES_Rig]->grid;
        Grid& pack = *eq.slots[ES_Backpack]->grid;
        if (u01(rng) < 0.5f) {
            int a = pickWeaponBand(0.5f, rng, 0, 2500, false), b = pickWeaponBand(0.5f, rng, 0, 2500, false);
            int ammoA = ammoOfCaliber(weaponDefs()[a].caliber, 1, rng), ammoB = ammoOfCaliber(weaponDefs()[b].caliber, 1, rng);
            eq.slots[ES_Hand0] = kitGun(a, 1.0f, 60, 1 + pick(rng, 2), ammoA, rng);
            eq.slots[ES_Back0] = kitGun(b, 1.0f, 40, pick(rng, 2), ammoB, rng);
            giveAmmo(eq, rig, a, 2 + pick(rng, 2), 20 + pick(rng, 30), ammoA, rng);
            giveAmmo(eq, pack, b, 1 + pick(rng, 2), 10 + pick(rng, 20), ammoB, rng);
        } else {
            int wi = pickWeaponBand(1.0f, rng, 2500, 6500, false);
            int ammo = ammoOfCaliber(weaponDefs()[wi].caliber, 1 + pick(rng, 2), rng);
            eq.slots[ES_Hand0] = kitGun(wi, 1.5f, 75, 1 + pick(rng, 3), ammo, rng);
            giveAmmo(eq, rig, wi, 3 + pick(rng, 2), 30 + pick(rng, 30), ammo, rng);
        }
        if (u01(rng) < 0.5f) {
            int si = pickWeaponBand(0.5f, rng, 0, 2500, true);
            eq.slots[ES_Hand1] = kitGun(si, 0.5f, 20, pick(rng, 2), -1, rng);
            addAmmoFor(eq.pockets, si, 1, 0, rng);
        }
        eq.stow(bandage);
        meds(2 + pick(rng, 2), 1);
        return;
    }
    // Tier 3: better and better with q. The dearest guns stay rare even here (value curve).
    eq.slots[ES_Rig] = makeItem("rig_assault");
    eq.slots[ES_Backpack] = makeItem(q > 0.3f ? "bp_ruck" : "bp_patrol");
    eq.slots[ES_Armor] = makeItem(q > 0.8f ? "arm_heavy" : "arm_assault");
    eq.slots[ES_Helmet] = makeItem(q > 0.5f ? "helm_heavy" : "helm_ballistic");
    float quality = 1.5f + 1.5f * q;
    int wi = pickWeaponBand(quality, rng, 4500, 1000000, false);
    int ammo = ammoOfCaliber(weaponDefs()[wi].caliber, 3, rng);
    eq.slots[ES_Hand0] = kitGun(wi, quality, 95, 2 + pick(rng, 3), ammo, rng, u01(rng) < 0.25f + 0.5f * q);
    giveAmmo(eq, *eq.slots[ES_Rig]->grid, wi, 3 + pick(rng, 2), 40 + pick(rng, 40), ammo, rng);
    int si = pickWeaponBand(1.0f + q, rng, 0, 100000, true);
    eq.slots[ES_Hand1] = kitGun(si, 1.0f + q, 50, 1, -1, rng);
    addAmmoFor(eq.pockets, si, 1, 0, rng);
    eq.stow(bandage);
    meds(2 + pick(rng, 2), 2);
    ItemPtr surgical = makeItem("med_surgical");
    if (q > 0.4f) eq.stow(surgical);
}

void makeFreeKit(Equipment& eq) {
    eq.clear();
    eq.slots[ES_Rig] = makeItem("rig_belt");
    eq.slots[ES_Hand0] = makeLoadedWeapon(kPistol, 0);
    std::mt19937 rng(1);
    addAmmoFor(*eq.slots[ES_Rig]->grid, kPistol, 2, 0, rng);
    ItemPtr b = makeItem("med_bandage");
    eq.stow(b);
}

// ------------------------------------------------------------------ world containers

void Loot::clear() {
    containers.clear();
    items.clear();
}

void Loot::spawnContainers(const MapData& map, World& world, std::vector<SceneBlock>& blocks, std::mt19937& rng) {
    for (const ContainerSpawn& s : map.containers) {
        LootContainer c;
        c.type = s.type;
        c.yaw = s.yaw;
        c.tier = s.tier;
        c.hotspot = s.hotspot;
        c.lootZone = s.lootZone;
        glm::vec3 size, color;
        switch (s.type) {
        case ContainerType::WeaponCrate: size = {1.3f, 0.5f, 0.6f}; color = {0.30f, 0.34f, 0.20f}; break;
        case ContainerType::AmmoBox: size = {0.55f, 0.35f, 0.3f}; color = {0.24f, 0.30f, 0.20f}; break;
        case ContainerType::MedCabinet: size = {0.6f, 0.8f, 0.3f}; color = {0.85f, 0.85f, 0.82f}; break;
        case ContainerType::Toolbox: size = {0.55f, 0.28f, 0.28f}; color = {0.65f, 0.15f, 0.12f}; break;
        default: size = {0.85f, 0.38f, 0.42f}; color = {0.18f, 0.20f, 0.22f}; break;
        }
        c.half = size * 0.5f;
        c.position = s.position + glm::vec3(0.0f, c.half.y, 0.0f);
        OBB box;
        box.center = c.position;
        box.half = c.half;
        // Axis-aligned (quarter turns) so the movement AABB stays tight.
        int q = int(std::round(s.yaw / 1.5708f)) & 3;
        if (q & 1) std::swap(box.half.x, box.half.z);
        c.half = box.half;
        SceneBlock b;
        b.box = box;
        b.color = color;
        blocks.push_back(b);
        if (s.type == ContainerType::MedCabinet) {
            SceneBlock cross = b;
            cross.box.half = glm::vec3(box.half.x * 0.3f, box.half.y * 0.08f, box.half.z + 0.005f);
            cross.box.center.y += box.half.y * 0.4f;
            cross.color = {0.8f, 0.1f, 0.1f};
            blocks.push_back(cross);
        }
        Collider col;
        col.box = box;
        col.surface = s.type == ContainerType::Duffel ? Surface::Wood : Surface::Metal;
        col.blocksPlayer = true;
        c.collider = world.add(col);
        fillContainer(c, rng);
        containers.push_back(std::move(c));
    }
}

void Loot::drop(DroppedItem&& d, double time) {
    if (!d.item) return;
    WorldItem w;
    w.item = std::move(d.item);
    w.position = d.position;
    w.velocity = d.velocity;
    w.rotation = d.rotation;
    w.spin = glm::vec3(1.5f, 2.0f, 0.5f);
    w.dropTime = time;
    items.push_back(std::move(w));
}

void Loot::update(float dt, const World& world) {
    for (WorldItem& w : items) {
        if (w.resting) continue;
        w.velocity.y -= 9.81f * dt;
        glm::vec3 next = w.position + w.velocity * dt;
        SegmentQuery q;
        q.staticOnly = true;
        q.skipPenetrable = true;
        WorldHit hit;
        if (world.segment(w.position + glm::vec3(0, 0.02f, 0), next, hit, q)) {
            w.resting = true;
            bool gun = w.item->d().cat == ItemCat::Weapon;
            float yaw = std::atan2(w.velocity.x + 0.3f, w.velocity.z + 0.1f) + float(w.dropTime);
            // Lies on its side, flat on whatever it landed on.
            w.rotation = glm::angleAxis(yaw, glm::vec3(0, 1, 0)) * (gun ? glm::angleAxis(1.5708f, glm::vec3(0, 0, 1)) : glm::quat(1, 0, 0, 0));
            w.position = hit.point + hit.normal * (gun ? 0.03f : 0.02f);
            if (gun) w.position -= w.rotation * glm::vec3(0.0f, 0.0f, -0.4f); // centre the gun on the landing point
        } else {
            w.position = next;
            float s = glm::length(w.spin);
            if (s > 1e-3f) w.rotation = glm::normalize(glm::angleAxis(s * dt, w.spin / s) * w.rotation);
        }
        if (w.position.y < -50.0f) w.resting = true;
    }
}

void Loot::draw(Renderer& r, const glm::vec3& cam, float maxDistance) const {
    for (const WorldItem& w : items) {
        if (glm::length(w.position - cam) > maxDistance) continue;
        glm::mat4 m = glm::mat4_cast(w.rotation);
        m[3] = glm::vec4(w.position, 1.0f);
        const ItemDef& d = w.item->d();
        if (d.cat == ItemCat::Weapon) {
            drawWeaponItem(r, *w.item, m);
        } else {
            glm::vec3 size(0.06f * d.w + 0.03f, 0.05f, 0.08f * d.h + 0.03f);
            r.draw(r.box, glm::scale(glm::translate(m, glm::vec3(0, size.y * 0.5f, 0)), size), d.color);
        }
    }
}
