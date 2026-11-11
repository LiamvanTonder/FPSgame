#include "items/Store.h"

#include "game/Attachments.h"
#include "game/WeaponDefs.h"
#include "items/Profile.h"

#include <algorithm>
#include <cmath>

const char* storeTabName(StoreTab t) {
    switch (t) {
    case StoreTab::Weapons: return "Weapons";
    case StoreTab::Ammo: return "Ammo";
    case StoreTab::Magazines: return "Magazines";
    case StoreTab::Medical: return "Medical";
    case StoreTab::Optics: return "Optics";
    case StoreTab::Attachments: return "Attachments";
    case StoreTab::Armor: return "Armour";
    case StoreTab::Gear: return "Gear";
    default: return "?";
    }
}

namespace store {

std::vector<StoreOffer> offers(StoreTab tab) {
    std::vector<StoreOffer> out;
    const auto& defs = itemDefs();
    for (int i = 0; i < int(defs.size()); ++i) {
        const ItemDef& d = defs[i];
        StoreOffer o;
        o.def = i;
        switch (d.cat) {
        case ItemCat::Weapon:
            if (tab != StoreTab::Weapons || d.value > kRaidOnlyValue) continue;
            o.price = int(std::round(d.value * 1.2f / 10.0f)) * 10;
            break;
        case ItemCat::Ammo:
            if (tab != StoreTab::Ammo) continue;
            o.count = d.stack;
            o.price = std::max(10, int(std::round(d.value * d.stack * 1.2f)));
            break;
        case ItemCat::Magazine:
            if (tab != StoreTab::Magazines) continue;
            o.price = int(std::round(d.value * 1.2f));
            break;
        case ItemCat::Med:
            if (tab != StoreTab::Medical) continue;
            o.price = int(std::round(d.value * 1.2f));
            break;
        case ItemCat::Optic:
            if (tab != StoreTab::Optics) continue;
            o.price = int(std::round(d.value * 1.2f));
            break;
        case ItemCat::Attachment:
            if (tab != StoreTab::Attachments) continue;
            o.price = int(std::round(d.value * 1.2f));
            break;
        case ItemCat::Armor: case ItemCat::Helmet:
            if (tab != StoreTab::Armor || d.value > kRaidOnlyValue) continue;
            o.price = int(std::round(d.value * 1.2f / 10.0f)) * 10;
            break;
        default:
            if (tab != StoreTab::Gear) continue;
            o.price = int(std::round(d.value * 1.2f));
            break;
        }
        out.push_back(o);
    }
    std::sort(out.begin(), out.end(), [&](const StoreOffer& a, const StoreOffer& b) {
        const ItemDef &da = defs[a.def], &db = defs[b.def];
        if (tab == StoreTab::Attachments && da.attach >= 0 && db.attach >= 0 &&
            attachmentDefs()[da.attach].slot != attachmentDefs()[db.attach].slot)
            return attachmentDefs()[da.attach].slot < attachmentDefs()[db.attach].slot;
        if (tab == StoreTab::Armor && da.cat != db.cat) return da.cat < db.cat;
        if (da.caliber != db.caliber && tab != StoreTab::Weapons) return da.caliber < db.caliber;
        return a.price < b.price;
    });
    return out;
}

ItemPtr make(const StoreOffer& o) {
    const ItemDef& d = itemDefs()[o.def];
    if (d.cat == ItemCat::Weapon) {
        ItemPtr w = makeItem(o.def);
        int mag = magazineItemFor(d.weapon);
        if (mag >= 0) w->mag = makeItem(mag);
        return w;
    }
    return makeItem(o.def, o.count);
}

int sellPrice(const Item& it) { return std::max(1, it.totalValue() / 2); }

bool buy(Profile& p, const StoreOffer& o, std::string& message) {
    if (p.money < o.price) {
        message = "Not enough credits";
        return false;
    }
    p.money -= o.price;
    p.addToStash(make(o));
    message = "Bought " + itemDefs()[o.def].name + " (in your stash)";
    return true;
}

bool fits(const ItemDef& d, int weapon) {
    const WeaponDef& w = weaponDefs()[weapon];
    if (d.cat == ItemCat::Ammo) return d.caliber == w.caliber;
    if (d.cat == ItemCat::Magazine) return magazineFits(d, weapon);
    if (d.cat == ItemCat::Optic) return opticFits(w, d.optic);
    if (d.cat == ItemCat::Attachment) return attachmentFits(w, d.attach);
    return false;
}

} // namespace store
