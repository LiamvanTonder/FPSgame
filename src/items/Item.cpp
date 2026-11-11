#include "items/Item.h"

#include "game/WeaponDefs.h"

#include <algorithm>
#include <istream>
#include <ostream>
#include <string>

namespace {
uint32_t g_nextUid = 1;
}

uint32_t newItemUid() { return g_nextUid++; }

ItemPtr Item::clone() const {
    auto c = std::make_unique<Item>();
    c->uid = newItemUid();
    c->def = def;
    c->count = count;
    c->rounds = rounds;
    if (mag) c->mag = mag->clone();
    c->chambered = chambered;
    c->optic = optic;
    c->magnification = magnification;
    c->zeroRange = zeroRange;
    c->fullAuto = fullAuto;
    c->uses = uses;
    c->pool = pool;
    if (grid) c->grid = std::make_unique<Grid>(grid->clone());
    return c;
}

int Item::totalValue() const {
    const ItemDef& dd = d();
    int v = dd.cat == ItemCat::Ammo ? dd.value * count : dd.value;
    for (uint8_t r : rounds) v += ammoDefs()[r].value;
    if (mag) v += mag->totalValue();
    if (chambered >= 0) v += ammoDefs()[chambered].value;
    if (optic > 0) {
        int o = opticItemDef(optic);
        if (o >= 0) v += itemDefs()[o].value;
    }
    if (dd.cat == ItemCat::Med && dd.uses > 1) v = v * uses / dd.uses;
    if (grid)
        for (const Grid::Entry& e : grid->items) v += e.item->totalValue();
    return v;
}

std::string Item::label() const {
    const ItemDef& dd = d();
    switch (dd.cat) {
    case ItemCat::Ammo: return dd.shortName + "\n" + std::to_string(count);
    case ItemCat::Magazine: return dd.shortName + "\n" + std::to_string(rounds.size()) + "/" + std::to_string(dd.capacity);
    case ItemCat::Med:
        if (dd.hpPool > 0.0f) return dd.shortName + "\n" + std::to_string(int(pool)) + " hp";
        if (dd.uses > 1) return dd.shortName + "\n" + std::to_string(uses) + "x";
        return dd.shortName;
    case ItemCat::Weapon: return dd.shortName + "\n" + std::to_string(loadedRounds(*this)) + "/" + std::to_string(loadedCapacity(*this));
    default: return dd.shortName;
    }
}

ItemPtr makeItem(int def, int count) {
    auto it = std::make_unique<Item>();
    it->uid = newItemUid();
    it->def = def;
    const ItemDef& d = itemDefs()[def];
    it->count = d.cat == ItemCat::Ammo ? (count < 0 ? d.stack : std::min(count, d.stack)) : 1;
    it->uses = d.uses;
    it->pool = d.hpPool;
    if (d.cat == ItemCat::Rig || d.cat == ItemCat::Backpack) it->grid = std::make_unique<Grid>(d.gridW, d.gridH);
    if (d.cat == ItemCat::Weapon) {
        const WeaponDef& w = weaponDefs()[d.weapon];
        it->zeroRange = w.defaultZero;
        it->optic = 0;
        it->magnification = 1.0f;
    }
    return it;
}

ItemPtr makeItem(const std::string& id, int count) {
    int d = findItemDef(id);
    return d >= 0 ? makeItem(d, count) : nullptr;
}

ItemPtr makeAmmo(int ammoDef, int rounds) {
    const auto& defs = itemDefs();
    for (int i = 0; i < int(defs.size()); ++i)
        if (defs[i].cat == ItemCat::Ammo && defs[i].ammo == ammoDef) return makeItem(i, rounds);
    return nullptr;
}

ItemPtr makeFullMagazine(int magDef, int ammoDef) {
    ItemPtr m = makeItem(magDef);
    if (ammoDef >= 0) m->rounds.assign(itemDefs()[magDef].capacity, uint8_t(ammoDef));
    return m;
}

ItemPtr makeLoadedWeapon(int weapon, int optic, int ammoDef) {
    ItemPtr w = makeItem(weaponItemDef(weapon));
    const WeaponDef& wd = weaponDefs()[weapon];
    if (ammoDef < 0) ammoDef = defaultAmmo(wd.caliber);
    w->optic = std::min(optic, wd.maxOptic);
    w->magnification = opticDefs()[w->optic].minMag;
    switch (wd.reload) {
    case ReloadType::Magazine: {
        int m = magazineItemFor(weapon);
        if (m >= 0) w->mag = makeFullMagazine(m, ammoDef);
        if (w->mag && !w->mag->rounds.empty()) {
            w->chambered = w->mag->rounds.back();
            w->mag->rounds.pop_back();
        }
        break;
    }
    case ReloadType::Tube:
        w->rounds.assign(wd.magazine, uint8_t(ammoDef));
        if (wd.chamberSeparate) w->chambered = ammoDef;
        break;
    case ReloadType::SingleLoad: w->chambered = ammoDef; break;
    }
    return w;
}

bool magazineFits(const ItemDef& mag, int weapon) {
    const WeaponDef& w = weaponDefs()[weapon];
    if (mag.cat != ItemCat::Magazine || w.reload != ReloadType::Magazine) return false;
    if (!(w.magMask & magBit(mag.magFamily))) return false;
    return mag.caliber < 0 || mag.caliber == w.caliber;
}

int loadedRounds(const Item& weapon) {
    int n = weapon.chambered >= 0 ? 1 : 0;
    if (weapon.mag) n += int(weapon.mag->rounds.size());
    n += int(weapon.rounds.size());
    return n;
}

int loadedCapacity(const Item& weapon) {
    const ItemDef& d = weapon.d();
    if (d.cat != ItemCat::Weapon) return 0;
    const WeaponDef& w = weaponDefs()[d.weapon];
    switch (w.reload) {
    case ReloadType::Magazine: return 1 + (weapon.mag ? weapon.mag->d().capacity : 0);
    case ReloadType::Tube: return w.magazine + (w.chamberSeparate ? 1 : 0);
    case ReloadType::SingleLoad: return 1;
    }
    return 0;
}

// ---------------------------------------------------------------- Grid

bool Grid::fits(int iw, int ih, int x, int y, const Item* ignore) const {
    if (x < 0 || y < 0 || x + iw > w || y + ih > h) return false;
    for (const Entry& e : items) {
        if (e.item.get() == ignore) continue;
        int ew = e.item->width(e.rot), eh = e.item->height(e.rot);
        if (x < e.x + ew && x + iw > e.x && y < e.y + eh && y + ih > e.y) return false;
    }
    return true;
}

bool Grid::place(ItemPtr& it, int x, int y, bool rot) {
    if (!it || !fits(*it, x, y, rot)) return false;
    items.push_back({std::move(it), x, y, rot});
    return true;
}

bool Grid::findSpace(const Item& it, int& x, int& y, bool& rot) const {
    for (int r = 0; r < 2; ++r) {
        bool ro = r == 1;
        if (ro && it.d().w == it.d().h) break;
        for (int yy = 0; yy < h; ++yy)
            for (int xx = 0; xx < w; ++xx)
                if (fits(it, xx, yy, ro)) {
                    x = xx;
                    y = yy;
                    rot = ro;
                    return true;
                }
    }
    return false;
}

bool Grid::autoPlace(ItemPtr& it) {
    if (!it) return false;
    const ItemDef& d = it->d();
    if (d.cat == ItemCat::Ammo) {
        for (Entry& e : items) {
            if (e.item->def != it->def || e.item->count >= d.stack) continue;
            int move = std::min(d.stack - e.item->count, it->count);
            e.item->count += move;
            it->count -= move;
            if (it->count <= 0) {
                it.reset();
                return true;
            }
        }
    }
    int x, y;
    bool rot;
    if (!findSpace(*it, x, y, rot)) return false;
    return place(it, x, y, rot);
}

int Grid::indexAt(int x, int y) const {
    for (int i = 0; i < int(items.size()); ++i) {
        const Entry& e = items[i];
        if (x >= e.x && x < e.x + e.item->width(e.rot) && y >= e.y && y < e.y + e.item->height(e.rot)) return i;
    }
    return -1;
}

ItemPtr Grid::take(int index) {
    if (index < 0 || index >= int(items.size())) return nullptr;
    ItemPtr it = std::move(items[index].item);
    items.erase(items.begin() + index);
    return it;
}

Grid Grid::clone() const {
    Grid g(w, h);
    for (const Entry& e : items) g.items.push_back({e.item->clone(), e.x, e.y, e.rot});
    return g;
}

// ---------------------------------------------------------------- Equipment

const char* equipSlotName(int slot) {
    static const char* names[kEquipSlots] = {"Primary", "Secondary", "Back", "Back", "Rig", "Backpack"};
    return slot >= 0 && slot < kEquipSlots ? names[slot] : "?";
}

bool Equipment::accepts(int slot, const Item& it) const {
    ItemCat c = it.d().cat;
    switch (slot) {
    case ES_Hand0: case ES_Hand1: case ES_Back0: case ES_Back1: return c == ItemCat::Weapon;
    case ES_Rig: return c == ItemCat::Rig;
    case ES_Backpack: return c == ItemCat::Backpack;
    }
    return false;
}

bool Equipment::stowable(const Item& it) {
    ItemCat c = it.d().cat;
    return c != ItemCat::Weapon && c != ItemCat::Rig && c != ItemCat::Backpack;
}

std::vector<Grid*> Equipment::grids() {
    std::vector<Grid*> g;
    if (slots[ES_Rig] && slots[ES_Rig]->grid) g.push_back(slots[ES_Rig]->grid.get());
    g.push_back(&pockets);
    if (slots[ES_Backpack] && slots[ES_Backpack]->grid) g.push_back(slots[ES_Backpack]->grid.get());
    return g;
}

std::vector<const Grid*> Equipment::grids() const {
    std::vector<const Grid*> g;
    if (slots[ES_Rig] && slots[ES_Rig]->grid) g.push_back(slots[ES_Rig]->grid.get());
    g.push_back(&pockets);
    if (slots[ES_Backpack] && slots[ES_Backpack]->grid) g.push_back(slots[ES_Backpack]->grid.get());
    return g;
}

bool Equipment::stow(ItemPtr& it) {
    if (!it || !stowable(*it)) return false;
    for (Grid* g : grids())
        if (g->autoPlace(it)) return true;
    return false;
}

static Item* findIn(Item& it, uint32_t uid) {
    if (it.uid == uid) return &it;
    if (it.mag) {
        if (Item* f = findIn(*it.mag, uid)) return f;
    }
    if (it.grid)
        for (Grid::Entry& e : it.grid->items)
            if (Item* f = findIn(*e.item, uid)) return f;
    return nullptr;
}

Item* Equipment::find(uint32_t uid) {
    for (ItemPtr& s : slots)
        if (s)
            if (Item* f = findIn(*s, uid)) return f;
    for (Grid::Entry& e : pockets.items)
        if (Item* f = findIn(*e.item, uid)) return f;
    return nullptr;
}

static ItemPtr removeFromGrid(Grid& g, uint32_t uid) {
    for (int i = 0; i < int(g.items.size()); ++i) {
        Item& it = *g.items[i].item;
        if (it.uid == uid) return g.take(i);
        if (it.grid)
            if (ItemPtr r = removeFromGrid(*it.grid, uid)) return r;
    }
    return nullptr;
}

ItemPtr Equipment::remove(uint32_t uid) {
    for (ItemPtr& s : slots) {
        if (!s) continue;
        if (s->uid == uid) return std::move(s);
        if (s->mag && s->mag->uid == uid) return std::move(s->mag);
        if (s->grid)
            if (ItemPtr r = removeFromGrid(*s->grid, uid)) return r;
    }
    return removeFromGrid(pockets, uid);
}

int Equipment::roundsAvailable(int caliber) const {
    int n = 0;
    for (const Grid* g : grids())
        for (const Grid::Entry& e : g->items)
            if (e.item->d().cat == ItemCat::Ammo && e.item->d().caliber == caliber) n += e.item->count;
    return n;
}

int Equipment::takeRounds(int caliber, int n, std::vector<uint8_t>& out, int ammoPreference) {
    int taken = 0;
    for (int pass = 0; pass < 2 && taken < n; ++pass) {
        for (Grid* g : grids()) {
            for (int i = 0; i < int(g->items.size()) && taken < n;) {
                Item& it = *g->items[i].item;
                const ItemDef& d = it.d();
                bool match = d.cat == ItemCat::Ammo && d.caliber == caliber && (pass == 1 || d.ammo == ammoPreference);
                if (!match) {
                    ++i;
                    continue;
                }
                int k = std::min(n - taken, it.count);
                for (int r = 0; r < k; ++r) out.push_back(uint8_t(d.ammo));
                it.count -= k;
                taken += k;
                if (it.count <= 0) g->take(i);
                else ++i;
            }
        }
    }
    return taken;
}

ItemPtr Equipment::takeBestMagazine(uint32_t magMask, int caliber, int minRounds) {
    Grid* bestGrid = nullptr;
    int bestIndex = -1, bestRounds = minRounds - 1;
    for (Grid* g : grids()) {
        for (int i = 0; i < int(g->items.size()); ++i) {
            const Item& it = *g->items[i].item;
            const ItemDef& d = it.d();
            if (d.cat != ItemCat::Magazine || !(magMask & magBit(d.magFamily))) continue;
            if (d.caliber >= 0 && d.caliber != caliber) continue;
            // STANAG mags may hold either 5.56 or .300 BLK: only take one loaded for this gun.
            if (!it.rounds.empty() && ammoDefs()[it.rounds.back()].caliber != caliber) continue;
            if (int(it.rounds.size()) > bestRounds) {
                bestRounds = int(it.rounds.size());
                bestGrid = g;
                bestIndex = i;
            }
        }
    }
    return bestGrid ? bestGrid->take(bestIndex) : nullptr;
}

int Equipment::magazinesAvailable(uint32_t magMask, int caliber) const {
    int n = 0;
    for (const Grid* g : grids())
        for (const Grid::Entry& e : g->items) {
            const ItemDef& d = e.item->d();
            if (d.cat == ItemCat::Magazine && (magMask & magBit(d.magFamily)) && (d.caliber < 0 || d.caliber == caliber) &&
                !e.item->rounds.empty() && ammoDefs()[e.item->rounds.back()].caliber == caliber)
                ++n;
        }
    return n;
}

int Equipment::totalValue() const {
    int v = 0;
    for (const ItemPtr& s : slots)
        if (s) v += s->totalValue();
    for (const Grid::Entry& e : pockets.items) v += e.item->totalValue();
    return v;
}

void Equipment::clear() {
    for (ItemPtr& s : slots) s.reset();
    pockets.items.clear();
}

// ---------------------------------------------------------------- Serialisation

namespace {
std::string ammoId(int a) { return a >= 0 && a < int(ammoDefs().size()) ? ammoDefs()[a].id : "-"; }
int ammoFromId(const std::string& s) { return s == "-" ? -1 : findAmmo(s); }
} // namespace

void writeItem(std::ostream& out, const Item& it) {
    out << "{ " << it.d().id << ' ' << it.count << ' ' << ammoId(it.chambered) << ' ' << it.optic << ' ' << it.magnification
        << ' ' << it.zeroRange << ' ' << (it.fullAuto ? 1 : 0) << ' ' << it.uses << ' ' << it.pool << ' ' << it.rounds.size();
    for (uint8_t r : it.rounds) out << ' ' << ammoId(r);
    if (it.mag) {
        out << " M ";
        writeItem(out, *it.mag);
    } else {
        out << " N";
    }
    if (it.grid) {
        out << " G ";
        writeGrid(out, *it.grid);
    } else {
        out << " N";
    }
    out << " }";
}

ItemPtr readItem(std::istream& in) {
    std::string tok;
    if (!(in >> tok) || tok != "{") return nullptr;
    std::string id, chambered;
    auto it = std::make_unique<Item>();
    int fa = 0;
    size_t n = 0;
    if (!(in >> id >> it->count >> chambered >> it->optic >> it->magnification >> it->zeroRange >> fa >> it->uses >> it->pool >> n))
        return nullptr;
    it->def = findItemDef(id);
    it->uid = newItemUid();
    it->chambered = ammoFromId(chambered);
    it->fullAuto = fa != 0;
    for (size_t i = 0; i < n; ++i) {
        std::string r;
        in >> r;
        int a = ammoFromId(r);
        if (a >= 0) it->rounds.push_back(uint8_t(a));
    }
    in >> tok;
    if (tok == "M") it->mag = readItem(in);
    in >> tok;
    if (tok == "G") {
        it->grid = std::make_unique<Grid>();
        if (!readGrid(in, *it->grid)) return nullptr;
    }
    in >> tok; // "}"
    if (it->def < 0) return nullptr; // unknown item (removed from the game): dropped
    if (it->d().cat == ItemCat::Weapon) it->optic = std::clamp(it->optic, 0, weaponDefs()[it->d().weapon].maxOptic);
    return it;
}

void writeGrid(std::ostream& out, const Grid& g) {
    out << g.w << ' ' << g.h << ' ' << g.items.size();
    for (const Grid::Entry& e : g.items) {
        out << '\n' << e.x << ' ' << e.y << ' ' << (e.rot ? 1 : 0) << ' ';
        writeItem(out, *e.item);
    }
}

bool readGrid(std::istream& in, Grid& g) {
    size_t n = 0;
    if (!(in >> g.w >> g.h >> n)) return false;
    g.items.clear();
    for (size_t i = 0; i < n; ++i) {
        int x, y, r;
        if (!(in >> x >> y >> r)) return false;
        ItemPtr it = readItem(in);
        if (it) g.items.push_back({std::move(it), x, y, r != 0});
    }
    return true;
}
