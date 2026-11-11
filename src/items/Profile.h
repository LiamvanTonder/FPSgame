#pragma once

#include "items/Item.h"

#include <string>

struct Config {
    int bots = 8;
    float sensitivity = 0.0022f;
    float volume = 0.8f;
    unsigned seed = 0; // 0: a new map every raid
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

// What persists between raids: the stash, the kit you deploy with, and a few numbers.
struct Profile {
    Grid stash{10, 40};
    int raids = 0, extractions = 0, deaths = 0, kills = 0;
    int money = 10000; // credits for the base store

    void starter(Equipment& loadout);
    // Adds rows if the stash is full: nothing that makes it home is ever lost.
    void addToStash(ItemPtr item);
    bool hasWeapon(const Equipment& loadout) const;

    bool load(const std::string& path, Equipment& loadout);
    bool save(const std::string& path, const Equipment& loadout) const;
};
