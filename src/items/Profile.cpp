#include "items/Profile.h"

#include "game/WeaponDefs.h"
#include "world/Loot.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

bool Config::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#') continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        try {
            if (k == "bots") bots = std::max(0, std::min(40, std::stoi(v)));
            else if (k == "mouseSensitivity") sensitivity = std::stof(v);
            else if (k == "volume") volume = std::stof(v);
            else if (k == "seed") seed = unsigned(std::stoul(v));
        } catch (...) {
        }
    }
    return true;
}

bool Config::save(const std::string& path) const {
    std::ofstream out(path);
    if (!out) return false;
    out << "# Raid settings\n";
    out << "# Number of AI raiders kept alive in a raid (replacements arrive as others die or leave)\n";
    out << "bots=" << bots << "\n";
    out << "mouseSensitivity=" << sensitivity << "\n";
    out << "volume=" << volume << "\n";
    out << "# Fixed map seed (0 = a different map every raid)\n";
    out << "seed=" << seed << "\n";
    return true;
}

void Profile::starter(Equipment& loadout) {
    stash = Grid(10, 40);
    std::mt19937 rng(7);
    // Kit to start the first raid with.
    loadout.clear();
    loadout.slots[ES_Rig] = makeItem("rig_recon");
    loadout.slots[ES_Backpack] = makeItem("bp_day");
    loadout.slots[ES_Hand0] = makeLoadedWeapon(findWeapon("volk762"), 0);
    loadout.slots[ES_Hand1] = makeLoadedWeapon(kPistol, 0);
    addAmmoFor(*loadout.slots[ES_Rig]->grid, findWeapon("volk762"), 3, 0, rng);
    addAmmoFor(loadout.pockets, kPistol, 1, 0, rng);
    for (const char* m : {"med_bandage", "med_tourniquet", "med_fieldkit"}) {
        ItemPtr it = makeItem(m);
        loadout.stow(it);
    }
    // And a few things in the stash.
    addToStash(makeLoadedWeapon(kShotgun, 0));
    addToStash(makeAmmo(defaultAmmo(Cal_12ga), 20));
    addToStash(makeLoadedWeapon(kRifle, 1));
    addToStash(makeFullMagazine(findItemDef("m_stanag_30"), defaultAmmo(Cal_556)));
    addToStash(makeItem("m_stanag_30"));
    addToStash(makeAmmo(defaultAmmo(Cal_556), 60));
    addToStash(makeLoadedWeapon(kSniper, 4));
    addToStash(makeAmmo(defaultAmmo(Cal_762x51), 30));
    addToStash(makeAmmo(defaultAmmo(Cal_762x39), 60));
    for (const char* m : {"med_bandage", "med_bandage", "med_splint", "med_painkillers", "med_fieldkit"}) addToStash(makeItem(m));
    addToStash(makeItem("o_3"));
    addToStash(makeItem("bp_patrol"));
}

void Profile::addToStash(ItemPtr item) {
    if (!item) return;
    while (!stash.autoPlace(item)) stash.h += 5;
}

bool Profile::hasWeapon(const Equipment& loadout) const {
    for (const ItemPtr& s : loadout.slots)
        if (s && s->d().cat == ItemCat::Weapon) return true;
    for (const Grid::Entry& e : stash.items)
        if (e.item->d().cat == ItemCat::Weapon) return true;
    return false;
}

bool Profile::save(const std::string& path, const Equipment& loadout) const {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream out(path + ".tmp");
    if (!out) return false;
    out << "PROFILE 1\n";
    out << "STATS " << raids << ' ' << extractions << ' ' << deaths << ' ' << kills << "\n";
    out << "MONEY " << money << "\n";
    out << "STASH ";
    writeGrid(out, stash);
    out << "\nLOADOUT\n";
    for (int s = 0; s < kEquipSlots; ++s) {
        out << "SLOT " << s << ' ';
        if (loadout.slots[s]) writeItem(out, *loadout.slots[s]);
        else out << "-";
        out << "\n";
    }
    out << "POCKETS ";
    writeGrid(out, loadout.pockets);
    out << "\nEND\n";
    out.close();
    std::error_code ec;
    std::filesystem::rename(path + ".tmp", path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(path + ".tmp", path, ec);
    }
    return !ec;
}

bool Profile::load(const std::string& path, Equipment& loadout) {
    std::ifstream in(path);
    if (!in) return false;
    std::string tok;
    int version = 0;
    if (!(in >> tok >> version) || tok != "PROFILE") return false;
    loadout.clear();
    while (in >> tok) {
        if (tok == "STATS") in >> raids >> extractions >> deaths >> kills;
        else if (tok == "MONEY") in >> money;
        else if (tok == "STASH") {
            if (!readGrid(in, stash)) return false;
        } else if (tok == "SLOT") {
            int s;
            in >> s;
            std::streampos p = in.tellg();
            std::string next;
            in >> next;
            if (next == "-") continue;
            in.seekg(p);
            ItemPtr it = readItem(in);
            if (s >= 0 && s < kEquipSlots && it && loadout.accepts(s, *it)) loadout.slots[s] = std::move(it);
        } else if (tok == "POCKETS") {
            if (!readGrid(in, loadout.pockets)) return false;
        } else if (tok == "END") break;
    }
    return true;
}
