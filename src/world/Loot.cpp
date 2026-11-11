#include "world/Loot.h"

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
} // namespace

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
    if (d.maxOptic > 0) {
        float chance = d.cls == WeaponClass::Sniper ? 0.95f : 0.2f + 0.2f * tier;
        if (u01(rng) < chance) optic = 1 + pick(rng, d.maxOptic);
        if (d.cls == WeaponClass::Sniper || d.cls == WeaponClass::Marksman) optic = std::max(optic, std::min(d.maxOptic, 3 + pick(rng, 3)));
    }
    int ammo = ammoOfCaliber(d.caliber, tier, rng);
    ItemPtr item = makeLoadedWeapon(wi, optic, ammo);
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

void fillContainer(LootContainer& c, std::mt19937& rng) {
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
            int o = opticItemDef(1 + pick(rng, 5));
            if (o >= 0) placeOrDrop(g, makeItem(o));
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
            int o = opticItemDef(1 + pick(rng, std::min(5, 2 + rollTier(c.tier, rng))));
            if (o >= 0) placeOrDrop(g, makeItem(o));
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
            } else if (k < 0.9f) {
                const char* gear[] = {"rig_belt", "rig_recon", "bp_day", "bp_patrol"};
                ItemPtr it = makeItem(gear[pick(rng, 4)]);
                if (it && it->d().w <= 5 && it->d().h <= 3) placeOrDrop(g, std::move(it));
            } else {
                int o = opticItemDef(1 + pick(rng, 3));
                if (o >= 0) placeOrDrop(g, makeItem(o));
            }
        }
        break;
    }
    }
}

void makeLoadout(Equipment& eq, int tier, std::mt19937& rng) {
    eq.clear();
    const char* rigs[] = {"rig_belt", "rig_recon", "rig_assault", "rig_assault"};
    eq.slots[ES_Rig] = makeItem(rigs[std::clamp(tier + (u01(rng) < 0.3f ? 1 : 0), 0, 3)]);
    if (u01(rng) < 0.45f + 0.15f * tier) {
        const char* packs[] = {"bp_day", "bp_day", "bp_patrol", "bp_ruck"};
        eq.slots[ES_Backpack] = makeItem(packs[std::clamp(tier, 0, 3)]);
    }
    // Primary: a long gun most of the time.
    ItemPtr primary = rollWeapon(tier, rng, u01(rng) < 0.92f);
    int wi = primary->d().weapon;
    int ammoDef = primary->chambered >= 0 ? primary->chambered : (primary->rounds.empty() ? -1 : primary->rounds.back());
    if (primary->mag && !primary->mag->rounds.empty()) primary->mag->rounds.assign(primary->mag->d().capacity, primary->mag->rounds.back());
    eq.slots[ES_Hand0] = std::move(primary);
    Grid& rig = *eq.slots[ES_Rig]->grid;
    const WeaponDef& wd = weaponDefs()[wi];
    if (wd.reload == ReloadType::Magazine) addAmmoFor(rig, wi, 2 + pick(rng, 2), 20 + pick(rng, 30), rng, ammoDef);
    else addAmmoFor(rig, wi, 0, wd.reload == ReloadType::SingleLoad ? 15 + pick(rng, 15) : 16 + pick(rng, 16), rng, ammoDef);
    // Sidearm
    if (wd.cls != WeaponClass::Pistol && u01(rng) < 0.4f) {
        ItemPtr side = rollWeapon(std::min(tier, 1), rng, false);
        int si = side->d().weapon;
        eq.slots[ES_Hand1] = std::move(side);
        addAmmoFor(eq.pockets, si, 1, 0, rng);
    }
    // Medical
    ItemPtr bandage = makeItem("med_bandage");
    eq.stow(bandage);
    int meds = 1 + pick(rng, 2 + tier / 2);
    for (int i = 0; i < meds; ++i) {
        ItemPtr m = makeItem(medItem(rng, tier));
        eq.stow(m);
    }
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
