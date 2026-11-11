#pragma once

#include "items/ItemDefs.h"

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

struct Grid;

// A concrete item. Magazines and internally-fed guns hold their rounds in order
// (back() is the next round to feed); weapons carry their own mag, chamber and optic.
struct Item {
    uint32_t uid = 0;
    int def = 0;
    int count = 1; // ammo: rounds in the stack

    std::vector<uint8_t> rounds; // ammo def indices

    // Weapon state
    std::unique_ptr<Item> mag;
    int chambered = -1;          // ammo def in the chamber, -1 empty
    int optic = 0;               // opticDefs() index
    float magnification = 1.0f;
    float zeroRange = 100.0f;
    bool fullAuto = false;
    float loadT = -1.0f, loadDelay = 0.0f; // single-load sequence
    bool caseInChamber = false;

    // Medical
    int uses = 1;
    float pool = 0.0f;

    // Rig / backpack contents
    std::unique_ptr<Grid> grid;

    const ItemDef& d() const { return itemDefs()[def]; }
    int width(bool rot) const { return rot ? d().h : d().w; }
    int height(bool rot) const { return rot ? d().w : d().h; }
    std::unique_ptr<Item> clone() const;
    // What it is worth, including contents.
    int totalValue() const;
    // Short label for UI cells.
    std::string label() const;
};

using ItemPtr = std::unique_ptr<Item>;

uint32_t newItemUid();
ItemPtr makeItem(int def, int count = -1); // count < 0: a full stack / defaults
ItemPtr makeItem(const std::string& id, int count = -1);
ItemPtr makeAmmo(int ammoDef, int rounds);
// A weapon with an inserted full magazine (or loaded tube) of its default ammunition.
ItemPtr makeLoadedWeapon(int weapon, int optic = 0, int ammoDef = -1);
ItemPtr makeFullMagazine(int magDef, int ammoDef);

// Rectangular inventory grid.
struct Grid {
    struct Entry {
        ItemPtr item;
        int x = 0, y = 0;
        bool rot = false;
    };
    int w = 0, h = 0;
    std::vector<Entry> items;

    Grid() = default;
    Grid(int width, int height) : w(width), h(height) {}

    bool fits(int iw, int ih, int x, int y, const Item* ignore = nullptr) const;
    bool fits(const Item& it, int x, int y, bool rot, const Item* ignore = nullptr) const {
        return fits(it.width(rot), it.height(rot), x, y, ignore);
    }
    // Places 'it' (moved on success).
    bool place(ItemPtr& it, int x, int y, bool rot);
    // First free spot, trying both orientations; ammo merges into partial stacks first.
    bool autoPlace(ItemPtr& it);
    bool findSpace(const Item& it, int& x, int& y, bool& rot) const;
    int indexAt(int x, int y) const;
    ItemPtr take(int index);
    Grid clone() const;
};

enum EquipSlot { ES_Hand0, ES_Hand1, ES_Back0, ES_Back1, ES_Rig, ES_Backpack, kEquipSlots };
const char* equipSlotName(int slot);

// What a character carries.
struct Equipment {
    ItemPtr slots[kEquipSlots];
    Grid pockets{4, 1};

    bool accepts(int slot, const Item& it) const;
    // Carried grids in search order: rig, pockets, backpack.
    std::vector<Grid*> grids();
    std::vector<const Grid*> grids() const;
    // Into the carried grids (weapons and load-bearing gear are not stowable).
    bool stow(ItemPtr& it);
    static bool stowable(const Item& it);

    Item* find(uint32_t uid);
    ItemPtr remove(uint32_t uid);

    // Ammunition helpers used by the weapon.
    int roundsAvailable(int caliber) const;
    // Removes up to n loose rounds of the calibre (preferring ammoPreference); returns the ammo defs.
    int takeRounds(int caliber, int n, std::vector<uint8_t>& out, int ammoPreference = -1);
    // Fullest compatible loaded magazine in the carried grids (removed from its grid).
    ItemPtr takeBestMagazine(uint32_t magMask, int caliber, int minRounds = 1);
    int magazinesAvailable(uint32_t magMask, int caliber) const;

    int totalValue() const;
    void clear();
};

// Can a magazine (def) be used by this weapon?
bool magazineFits(const ItemDef& mag, int weapon);
// Rounds currently loaded in a weapon (chamber + magazine / tube).
int loadedRounds(const Item& weapon);
int loadedCapacity(const Item& weapon);

// Text serialisation (save files).
void writeItem(std::ostream& out, const Item& it);
ItemPtr readItem(std::istream& in);
void writeGrid(std::ostream& out, const Grid& g);
bool readGrid(std::istream& in, Grid& g);
