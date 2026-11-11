#pragma once

#include "items/Item.h"

#include <string>
#include <vector>

struct Profile;

enum class StoreTab { Weapons, Ammo, Magazines, Medical, Optics, Gear, kCount };
const char* storeTabName(StoreTab t);

struct StoreOffer {
    int def = 0;   // item def
    int count = 1; // rounds for ammo
    int price = 0;
};

// The base trader. The very best guns (worth over kRaidOnlyValue) are never sold:
// they only come out of raids.
namespace store {
constexpr int kRaidOnlyValue = 9000;
std::vector<StoreOffer> offers(StoreTab tab);
// Weapons sell with an empty standard magazine; ammo by the stack.
ItemPtr make(const StoreOffer& o);
int sellPrice(const Item& it);
bool buy(Profile& p, const StoreOffer& o, std::string& message);
// Does this ammo / magazine work in the weapon (weapon index)?
bool fits(const ItemDef& d, int weapon);
} // namespace store
