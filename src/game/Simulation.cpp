#include "game/Simulation.h"

#include "game/WeaponDefs.h"
#include "render/Camera.h"
#include "world/MapBuilder.h"

#include <algorithm>
#include <cmath>

namespace {
float u01(std::mt19937& rng) { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }

const char* kCallsigns[] = {"Ash",   "Birch", "Cobalt", "Dune",  "Ember", "Flint", "Grim",  "Hawk",  "Ivory", "Jinx",
                            "Kodiak", "Lynx", "Mako",   "Nomad", "Onyx",  "Pike",  "Quill", "Raven", "Sable", "Talon",
                            "Umber", "Viper", "Wolf",   "Yeti",  "Zephyr"};

glm::vec2 xz(const glm::vec3& v) { return {v.x, v.z}; }

// Ray vs OBB distance along the ray (returns < 0 if missed).
float rayBox(const glm::vec3& o, const glm::vec3& d, float maxT, const OBB& box) {
    float t;
    glm::vec3 n;
    if (intersectSegmentOBB(o, o + d * maxT, box, t, n)) return t * maxT;
    return -1.0f;
}
} // namespace

void Simulation::reset() {
    world = World();
    terrain = Terrain();
    map.clear();
    terrainColors.clear();
    targets = Targets();
    chars.clear();
    ballistics = Ballistics();
    fx = Effects();
    sound.clear();
    sound.setTime(0.0);
    loot.clear();
    nav = NavGrid();
    bots.clear();
    impacts.clear();
    events.clear();
    drops.clear();
    time = 0.0;
    stats = RaidStats{};
    playerExtracted = false;
    playerDeathTime = -1.0;
    sentHome.clear();
    openLoot = {};
    openStash = requestDeploy = openStore = false;
    suppression = hitFlash = 0.0f;
    flinch = glm::vec2(0.0f);
    lastHitReport.clear();
    lastHitTimer = 0.0f;
    m_interact.clear();
    m_nearMiss.clear();
    m_feed.clear();
    m_respawnAt.clear();
}

void Simulation::addCharacter(bool firstPerson) {
    auto c = std::make_unique<Character>();
    c->create(world, int(chars.size()), firstPerson);
    chars.push_back(std::move(c));
    m_interact.emplace_back();
}

SimContext Simulation::context() { return SimContext{world, ballistics, impacts, events, fx, sound, drops, time, &map}; }

void Simulation::setupBase(Equipment& loadout) {
    reset();
    mode = Mode::Base;
    m_withPlayer = true;
    level.build(world, targets, map);
    addCharacter(true);
    Character& p = player();
    p.name = "You";
    p.equip = std::move(loadout);
    p.spawn(world, level.spawnPoint, level.spawnYaw);
}

void Simulation::setupRaid(unsigned seed, int botCount, Equipment& loadout, bool withPlayer) {
    reset();
    mode = Mode::Raid;
    m_withPlayer = withPlayer;
    rng.seed(seed * 31u + 7u);
    MapBuilder builder;
    builder.build(seed, world, terrain, map, terrainColors);
    loot.spawnContainers(map, world, map.blocks, rng);
    nav.build(world, map.halfSize);
    nav.addLevels(world, map);

    addCharacter(true);
    Character& p = player();
    p.name = "You";
    p.equip = std::move(loadout);
    if (withPlayer && !map.spawns.empty()) {
        const SpawnPoint& s = map.spawns[size_t(u01(rng) * map.spawns.size()) % map.spawns.size()];
        p.spawn(world, s.position, s.yaw);
    }
    botTarget = botCount;
    for (int i = 0; i < botCount; ++i) {
        BotKitRoll k = rollBotTier(rng);
        spawnBot(k.tier, k.quality);
    }
    stats.botRespawns = 0;
}

int Simulation::spawnBot(int tier, float quality) {
    int slot = -1;
    for (int i = 1; i < int(chars.size()); ++i)
        if (!chars[i]->active) {
            slot = i;
            break;
        }
    if (slot < 0) {
        if (chars.size() >= 48) return -1;
        addCharacter(false);
        slot = int(chars.size()) - 1;
    }
    Character& c = *chars[slot];
    // As far from everyone as possible.
    glm::vec3 pos(0.0f, 0.05f, -60.0f);
    float yaw = 0.0f;
    if (!map.spawns.empty()) {
        float best = -1.0f;
        for (const SpawnPoint& s : map.spawns) {
            float nearest = 1e9f;
            for (auto& o : chars)
                if (o->alive()) nearest = std::min(nearest, glm::length(o->position - s.position));
            nearest += u01(rng) * 40.0f;
            if (nearest > best) {
                best = nearest;
                pos = s.position;
                yaw = s.yaw;
            }
        }
    } else {
        pos = glm::vec3((u01(rng) - 0.5f) * 18.0f, 0.05f, -60.0f - 90.0f * u01(rng));
        pos.y = world.groundHeight(pos + glm::vec3(0, 2, 0), pos.y + 3.0f, 0.0f);
        yaw = 0.0f;
    }
    c.name = kCallsigns[(slot * 7 + int(u01(rng) * 25)) % 25];
    c.tier = tier;
    makeLoadout(c.equip, tier, rng, quality);
    c.spawn(world, pos, yaw);
    bots.add(slot, unsigned(rng()), time);
    m_interact[slot] = InteractState{};
    return slot;
}

void Simulation::message(const std::string& s) {
    m_feed.push_back({s, 7.0f});
    if (m_feed.size() > 6) m_feed.erase(m_feed.begin());
}

// ------------------------------------------------------------------ interaction

Grid* Simulation::containerGrid(int index) {
    return index >= 0 && index < int(loot.containers.size()) ? &loot.containers[index].grid : nullptr;
}

Equipment* Simulation::bodyEquipment(int id) {
    if (id < 0 || id >= int(chars.size()) || !chars[id]->active || !chars[id]->dead) return nullptr;
    return &chars[id]->equip;
}

Interaction Simulation::focusFor(const Character& c) const {
    Interaction none;
    if (!c.alive()) return none;
    if (mode == Mode::Base) {
        if (glm::length(xz(c.position - level.stashPos)) < 1.8f) return {Interaction::Stash, 0};
        if (glm::length(xz(c.position - level.deployPos)) < 1.8f) return {Interaction::Deploy, 0};
        if (glm::length(xz(c.position - level.storePos)) < 2.0f) return {Interaction::Store, 0};
    }
    for (int i = 0; i < int(map.extracts.size()); ++i) {
        const ExtractPoint& e = map.extracts[i];
        bool backGuns = c.equip.slots[ES_Back0] || c.equip.slots[ES_Back1];
        if (backGuns && glm::length(xz(c.position - e.dropBox)) < 2.2f) return {Interaction::SendBack, i};
        if (glm::length(xz(c.position - e.position)) < e.radius) {
            // Looking at the uplink crate selects it, otherwise extract.
            return {Interaction::Extract, i};
        }
    }
    glm::vec3 eye = c.eye(), dir = c.viewDir();
    const float reach = 2.4f;
    float best = reach;
    Interaction pick;
    for (int i = 0; i < int(loot.containers.size()); ++i) {
        const LootContainer& lc = loot.containers[i];
        if (glm::length(lc.position - eye) > 4.0f) continue;
        OBB box{lc.position, lc.half + glm::vec3(0.08f), glm::mat3(1.0f)};
        float t = rayBox(eye, dir, reach, box);
        if (t >= 0.0f && t < best) {
            best = t;
            pick = {Interaction::Container, i};
        }
    }
    for (int i = 0; i < int(loot.items.size()); ++i) {
        const WorldItem& w = loot.items[i];
        glm::vec3 centre = w.position + w.rotation * glm::vec3(0.0f, 0.0f, w.item->d().cat == ItemCat::Weapon ? -0.4f : 0.0f);
        glm::vec3 to = centre - eye;
        float along = glm::dot(to, dir);
        if (along < 0.0f || along > reach) continue;
        float off = glm::length(to - dir * along);
        if (off < 0.3f && along < best) {
            best = along;
            pick = {Interaction::Item, i};
        }
    }
    for (const auto& o : chars) {
        if (!o->active || !o->dead || o->id == c.id) continue;
        if (glm::length(o->chest() - eye) > 4.0f) continue;
        for (int part = 0; part < kBodyPartCount; ++part) {
            const OBB& box = world.collider(o->colliderOf(part)).box;
            OBB fat = box;
            fat.half += glm::vec3(0.08f);
            float t = rayBox(eye, dir, reach, fat);
            if (t >= 0.0f && t < best) {
                best = t;
                pick = {Interaction::Body, o->id};
            }
        }
    }
    return pick;
}

std::string Simulation::describe(const Interaction& i, const Character& c) const {
    switch (i.type) {
    case Interaction::Container: {
        const LootContainer& lc = loot.containers[i.index];
        return std::string(lc.searched(c.id) ? "F  Open " : "Hold F  Search ") + containerName(lc.type);
    }
    case Interaction::Body: return std::string(chars[i.index]->searched ? "F  Loot body of " : "Hold F  Search body of ") + chars[i.index]->name;
    case Interaction::Item: return "F  Pick up " + loot.items[i.index].item->d().name;
    case Interaction::Extract: return "Hold F  Extract at " + map.extracts[i.index].name + " (8 s, stay in the zone)";
    case Interaction::SendBack: return "Hold F  Send the guns on your back home (25 s, loud)";
    case Interaction::Stash: return "F  Open stash";
    case Interaction::Deploy: return "F  Deploy to raid";
    case Interaction::Store: return "F  Store: buy and sell";
    default: return "";
    }
}

float Simulation::interactProgress(int id) const {
    if (id < 0 || id >= int(m_interact.size())) return 0.0f;
    const InteractState& s = m_interact[id];
    const Character& c = *chars[id];
    if (c.action() == Character::Action::Search || c.action() == Character::Action::Extract || c.action() == Character::Action::SendBack)
        return c.actionProgress();
    (void)s;
    return 0.0f;
}

bool Simulation::pickUp(Character& c, int idx) {
    if (idx < 0 || idx >= int(loot.items.size())) return false;
    ItemPtr& it = loot.items[idx].item;
    bool ok = false;
    if (it->d().cat == ItemCat::Weapon) {
        for (int s : {ES_Hand0, ES_Hand1, ES_Back0, ES_Back1})
            if (!c.equip.slots[s]) {
                c.equip.slots[s] = std::move(it);
                ok = true;
                break;
            }
    } else if (it->d().cat == ItemCat::Rig || it->d().cat == ItemCat::Backpack) {
        int s = it->d().cat == ItemCat::Rig ? ES_Rig : ES_Backpack;
        if (!c.equip.slots[s]) {
            c.equip.slots[s] = std::move(it);
            ok = true;
        }
    } else {
        ok = c.equip.stow(it);
    }
    if (ok) {
        loot.items.erase(loot.items.begin() + idx);
        if (c.id == 0) ++stats.itemsLooted;
        SoundEvent e;
        e.type = SoundType::Loot;
        e.position = c.position;
        e.db = 58.0f;
        e.owner = c.id;
        sound.emit(e);
    } else if (c.id == 0) {
        message("No room for that");
    }
    return ok;
}

void Simulation::dropFromCharacter(Character& c, ItemPtr item) {
    if (!item) return;
    DroppedItem d;
    d.item = std::move(item);
    d.position = c.chest() + c.flatForward() * 0.4f;
    d.velocity = c.flatForward() * 1.0f + glm::vec3(0, 0.5f, 0);
    d.rotation = glm::angleAxis(c.yaw, glm::vec3(0, 1, 0));
    drops.push_back(std::move(d));
}

bool Simulation::interact(Character& c, const Interaction& i, bool held, float dt) {
    InteractState& st = m_interact[c.id];
    Character::Action act = c.action();
    bool busyWithIt = act == Character::Action::Search || act == Character::Action::Extract || act == Character::Action::SendBack;
    if (i != st.target) {
        if (busyWithIt) c.cancelAction();
        st = InteractState{};
        st.target = i;
        busyWithIt = false;
    }
    if (i.type == Interaction::None || !c.alive()) return false;

    switch (i.type) {
    case Interaction::Stash:
    case Interaction::Deploy:
    case Interaction::Store:
    case Interaction::Item: {
        if (!held) {
            st.progress = 0.0f;
            return false;
        }
        if (st.progress > 0.0f) return false; // one action per press
        st.progress = 1.0f;
        if (i.type == Interaction::Item) return pickUp(c, i.index);
        if (c.id == 0) (i.type == Interaction::Stash ? openStash : i.type == Interaction::Store ? openStore : requestDeploy) = true;
        return true;
    }
    case Interaction::Container:
    case Interaction::Body: {
        bool known = i.type == Interaction::Container ? loot.containers[i.index].searched(c.id) : chars[i.index]->searched;
        if (!busyWithIt) {
            if (!held) {
                st.progress = 0.0f;
                return false;
            }
            if (known) {
                if (st.progress > 0.0f) return false;
                st.progress = 1.0f;
                return true; // already searched: opens straight away
            }
            int items = 0;
            if (i.type == Interaction::Container) items = int(loot.containers[i.index].grid.items.size());
            else {
                Equipment& e = chars[i.index]->equip;
                for (const ItemPtr& s : e.slots) items += s ? 1 : 0;
                for (Grid* g : e.grids()) items += int(g->items.size());
            }
            c.setBusy(Character::Action::Search, 1.2f + 0.25f * float(items));
            st.beaconTimer = 0.0f;
        }
        st.beaconTimer -= dt;
        if (st.beaconTimer <= 0.0f) {
            SoundEvent e;
            e.type = SoundType::Loot;
            e.position = c.position;
            e.db = 60.0f;
            e.owner = c.id;
            sound.emit(e);
            st.beaconTimer = 0.9f;
        }
        if (c.action() == Character::Action::Search && c.actionProgress() >= 1.0f) {
            c.cancelAction();
            if (i.type == Interaction::Container) loot.containers[i.index].searchedBy.push_back(c.id);
            else chars[i.index]->searched = true;
            st.progress = 1.0f;
            return true;
        }
        return false;
    }
    case Interaction::Extract: {
        const ExtractPoint& e = map.extracts[i.index];
        if (!busyWithIt) {
            if (!held) return false;
            c.setBusy(Character::Action::Extract, 8.0f);
            SoundEvent s;
            s.type = SoundType::Flare;
            s.position = e.position + glm::vec3(0, 6, 0);
            s.db = 105.0f;
            s.owner = c.id;
            sound.emit(s);
        }
        if (glm::length(xz(c.position - e.position)) > e.radius) {
            c.cancelAction();
            return false;
        }
        if (c.action() == Character::Action::Extract && c.actionProgress() >= 1.0f) {
            c.cancelAction();
            if (c.id == 0) {
                playerExtracted = true;
            } else {
                ++stats.botExtracts;
                message(c.name + " extracted at " + e.name);
                c.despawn(world);
                bots.remove(c.id);
            }
            return true;
        }
        return false;
    }
    case Interaction::SendBack: {
        const ExtractPoint& e = map.extracts[i.index];
        if (!c.equip.slots[ES_Back0] && !c.equip.slots[ES_Back1]) return false;
        if (!busyWithIt) {
            if (!held) return false;
            c.setBusy(Character::Action::SendBack, 25.0f);
            st.beaconTimer = 0.0f;
        }
        if (glm::length(xz(c.position - e.dropBox)) > 3.0f) {
            c.cancelAction();
            return false;
        }
        // The uplink beeps loudly: everyone nearby knows someone is here.
        st.beaconTimer -= dt;
        if (st.beaconTimer <= 0.0f) {
            SoundEvent s;
            s.type = SoundType::Beacon;
            s.position = e.dropBox;
            s.db = 118.0f;
            s.owner = c.id;
            sound.emit(s);
            st.beaconTimer = 1.0f;
        }
        if (c.action() == Character::Action::SendBack && c.actionProgress() >= 1.0f) {
            c.cancelAction();
            int n = 0;
            for (int s : {ES_Back0, ES_Back1}) {
                if (!c.equip.slots[s]) continue;
                ++n;
                if (c.id == 0) sentHome.push_back(std::move(c.equip.slots[s]));
                else c.equip.slots[s].reset();
            }
            if (c.id == 0) {
                stats.sentHome += n;
                message(std::to_string(n) + (n == 1 ? " weapon" : " weapons") + " sent home");
            } else {
                ++stats.botSendBacks;
            }
            return true;
        }
        return false;
    }
    default: return false;
    }
}

// ------------------------------------------------------------------ tick

void Simulation::handleEvents() {
    for (const WeaponEvent& e : events) {
        fx.onWeaponEvent(e);
        SoundEvent s;
        s.position = e.position;
        s.owner = e.owner;
        switch (e.type) {
        case WeaponEvent::Shot:
            s.type = SoundType::Shot;
            s.db = e.loudness;
            s.variant = e.weapon;
            s.suppressed = e.suppressed;
            if (e.owner >= 0 && e.owner < int(chars.size())) chars[e.owner]->lastShot = e.suppressed ? chars[e.owner]->lastShot : time;
            break;
        case WeaponEvent::MagOut: s.type = SoundType::Reload; s.variant = 0; s.db = 68.0f; break;
        case WeaponEvent::MagIn: s.type = SoundType::Reload; s.variant = 1; s.db = 70.0f; break;
        case WeaponEvent::Cycle: s.type = SoundType::Reload; s.variant = 2; s.db = 70.0f; break;
        case WeaponEvent::ShellIn: s.type = SoundType::Reload; s.variant = 3; s.db = 64.0f; break;
        case WeaponEvent::DryFire: s.type = SoundType::DryFire; s.db = 60.0f; break;
        default: continue;
        }
        sound.emit(s);
    }
    events.clear();
}

void Simulation::detectNearMisses(float dt) {
    for (const Ballistics::Bullet& b : ballistics.bullets()) {
        glm::vec3 a = b.position - b.velocity * dt;
        glm::vec3 seg = b.position - a;
        float speed = glm::length(b.velocity);
        for (auto& cp : chars) {
            Character& c = *cp;
            if (!c.alive() || c.id == b.owner) continue;
            glm::vec3 head = c.head();
            float t = std::clamp(glm::dot(head - a, seg) / std::max(glm::dot(seg, seg), 1e-6f), 0.0f, 1.0f);
            if (t >= 1.0f) continue;
            glm::vec3 closest = a + seg * t;
            float d = glm::length(closest - head);
            float radius = c.id == 0 ? 8.0f : 3.0f;
            if (d > radius) continue;
            auto key = std::make_pair(c.id, b.id);
            if (std::find(m_nearMiss.begin(), m_nearMiss.end(), key) != m_nearMiss.end()) continue;
            m_nearMiss.push_back(key);
            if (m_nearMiss.size() > 256) m_nearMiss.erase(m_nearMiss.begin(), m_nearMiss.begin() + 64);
            if (speed > 343.0f && c.id == 0) {
                SoundEvent s;
                s.type = SoundType::Crack;
                s.position = closest;
                s.db = 150.0f;
                s.owner = b.owner;
                sound.emit(s);
            }
            if (d < (c.id == 0 ? 1.5f : 2.5f)) {
                float k = 1.0f - d / 2.5f;
                c.suppression = std::min(1.0f, c.suppression + 0.35f + 0.4f * k);
                c.weapon.arm.addAngularVelocity(glm::vec3(u01(rng) - 0.5f, u01(rng) - 0.5f, 0.0f) * 0.4f);
                if (c.id == 0) {
                    suppression = std::min(1.0f, suppression + 0.35f + 0.4f * k);
                    flinch += glm::vec2(u01(rng) - 0.5f, u01(rng) - 0.5f) * (0.008f + 0.02f * k);
                } else {
                    bots.onNearMiss(c.id, b.owner, b.origin, *this);
                }
            }
        }
    }
}

void Simulation::handleImpacts() {
    SimContext sc = context();
    glm::vec3 viewer = m_withPlayer ? player().eye() : glm::vec3(0.0f);
    for (const Impact& imp : impacts) {
        const Collider& c = world.collider(imp.collider);
        float v = glm::length(imp.velocity);
        float energy = 0.5f * imp.mass * v * v;
        SoundEvent s;
        s.type = SoundType::Impact;
        s.position = imp.point;
        s.db = 85.0f + 10.0f * std::log10(std::max(energy, 10.0f) / 100.0f);
        s.variant = int(c.surface);
        s.owner = -1;
        sound.emit(s);
        // Rounds landing near people make them duck.
        for (auto& cp : chars) {
            if (!cp->alive() || cp->id == imp.shooter || cp->id == c.owner) continue;
            if (glm::length(cp->chest() - imp.point) < 2.5f) {
                cp->suppression = std::min(1.0f, cp->suppression + 0.3f);
                if (cp->id != 0) bots.onNearMiss(cp->id, imp.shooter, imp.point - glm::normalize(imp.velocity) * 50.0f, *this);
            }
        }

        if (c.owner >= 0 && c.owner < int(chars.size())) {
            Character& victim = *chars[c.owner];
            bool wasAlive = victim.alive();
            float hpBefore = victim.health.totalHp();
            std::string desc = victim.onHit(c.bodyPart, imp, sc);
            fx.onImpact(imp, c, viewer, world);
            glm::vec3 from = imp.shooter >= 0 && imp.shooter < int(chars.size()) ? chars[imp.shooter]->eye() : imp.point - imp.velocity;
            if (c.owner == 0) {
                hitFlash = 1.0f;
                suppression = 1.0f;
                hitFrom = from;
                stats.damageTaken += std::max(0.0f, hpBefore - victim.health.totalHp());
            } else if (wasAlive) {
                bots.onDamaged(c.owner, imp.shooter, from, *this);
            }
            if (wasAlive) bots.onHitLanded(imp.shooter, c.owner, *this);
            if (imp.shooter == 0 && c.owner != 0) {
                lastHitReport = desc;
                lastHitTimer = 6.0f;
            }
            continue;
        }
        if (c.paperTarget >= 0) {
            targets.hitPaper(c.paperTarget, imp.point);
        } else if (c.steelPlate >= 0) {
            targets.hitPlate(c.steelPlate, imp.point, imp.velocity * imp.mass * 1.3f);
        }
        fx.onImpact(imp, c, viewer, world);
    }
    impacts.clear();
}

void Simulation::updateRespawns(float dt) {
    (void)dt;
    if (mode != Mode::Raid) return;
    int alive = 0;
    for (int i = 1; i < int(chars.size()); ++i) alive += chars[i]->alive() ? 1 : 0;
    int pending = int(m_respawnAt.size());
    while (alive + pending < botTarget) {
        m_respawnAt.push_back(time + 20.0 + 40.0 * u01(rng));
        ++pending;
    }
    for (size_t i = 0; i < m_respawnAt.size();) {
        if (time >= m_respawnAt[i]) {
            BotKitRoll k = rollBotTier(rng);
            if (spawnBot(k.tier, k.quality) >= 0) {
                ++stats.botRespawns;
                message("A new raider entered the area");
            }
            m_respawnAt.erase(m_respawnAt.begin() + i);
        } else {
            ++i;
        }
    }
}

void Simulation::cleanupBodies() {
    std::vector<int> bodies;
    for (int i = 1; i < int(chars.size()); ++i)
        if (chars[i]->active && chars[i]->dead) bodies.push_back(i);
    if (bodies.size() <= 20) return;
    // Remove the oldest bodies far from the player.
    std::sort(bodies.begin(), bodies.end(), [&](int a, int b) { return chars[a]->deathTime < chars[b]->deathTime; });
    size_t remaining = bodies.size();
    for (int id : bodies) {
        if (remaining <= 20) break;
        if (m_withPlayer && glm::length(chars[id]->chest() - player().eye()) < 60.0f) continue;
        chars[id]->despawn(world);
        --remaining;
    }
}

void Simulation::tick(float dt, const CharacterInput& playerIn, bool interactHeld) {
    time += dt;
    sound.setTime(time);
    openLoot = {};

    bots.update(dt, *this);

    std::vector<char> wasAlive(chars.size());
    for (size_t i = 0; i < chars.size(); ++i) wasAlive[i] = chars[i]->alive();

    SimContext sc = context();
    CharacterInput idle;
    for (auto& cp : chars) {
        Character& c = *cp;
        if (!c.active) continue;
        const CharacterInput* in = nullptr;
        if (c.id == 0) in = m_withPlayer ? &playerIn : nullptr;
        else in = bots.input(c.id);
        if (!in) {
            idle = CharacterInput{};
            idle.yaw = c.yaw;
            idle.pitch = c.pitch;
            in = &idle;
        }
        c.update(*in, dt, sc);
    }
    // People don't walk through each other.
    for (size_t i = 0; i < chars.size(); ++i)
        for (size_t j = i + 1; j < chars.size(); ++j) {
            Character &a = *chars[i], &b = *chars[j];
            if (!a.alive() || !b.alive()) continue;
            glm::vec2 d = xz(a.position - b.position);
            float l = glm::length(d);
            if (l < 0.56f && l > 1e-4f && std::abs(a.position.y - b.position.y) < 1.5f) {
                glm::vec2 push = d / l * (0.56f - l) * 0.5f;
                a.position.x += push.x;
                a.position.z += push.y;
                b.position.x -= push.x;
                b.position.z -= push.y;
            }
        }

    // The player's hands-on interactions
    if (m_withPlayer && player().alive()) {
        Interaction f = focusFor(player());
        // Keep working on a started action even if the eyes wander.
        Character::Action a = player().action();
        Interaction cur = m_interact[0].target;
        if ((a == Character::Action::Search || a == Character::Action::Extract || a == Character::Action::SendBack) && cur.type != Interaction::None) f = cur;
        if (interact(player(), f, interactHeld, dt) && (f.type == Interaction::Container || f.type == Interaction::Body)) openLoot = f;
    }

    handleEvents();
    ballistics.update(dt, world, impacts);
    detectNearMisses(dt);
    handleImpacts();
    handleEvents(); // sounds from hits (death drops etc.)
    for (DroppedItem& d : drops) loot.drop(std::move(d), time);
    drops.clear();
    loot.update(dt, world);
    if (mode == Mode::Base) targets.update(dt, world);
    fx.update(dt, world, ballistics);

    // Deaths (shot, bled out, fell)
    for (size_t i = 0; i < chars.size() && i < wasAlive.size(); ++i) {
        Character& c = *chars[i];
        if (!wasAlive[i] || !c.dead) continue;
        std::string killer = c.killer >= 0 && c.killer < int(chars.size()) ? chars[c.killer]->name : "";
        std::string how = c.causeOfDeath;
        if (c.killer >= 0 && c.killer < int(chars.size()) && c.killer != int(i)) {
            chars[c.killer]->kills++;
            if (Bot* kb = bots.find(c.killer)) kb->myKills.push_back(int(i));
            message(killer + " killed " + c.name + " (" + how + ")");
        } else {
            message(c.name + " died (" + how + ")");
        }
        if (c.killer == 0 && i != 0) ++stats.kills;
        if (i != 0) {
            ++stats.botsKilled;
            bots.remove(int(i));
        } else {
            playerDeathTime = time;
            stats.causeOfDeath = how;
            stats.killedBy = killer;
        }
    }
    updateRespawns(dt);
    cleanupBodies();

    for (FeedLine& l : m_feed) l.timer -= dt;
    m_feed.erase(std::remove_if(m_feed.begin(), m_feed.end(), [](const FeedLine& l) { return l.timer <= 0.0f; }), m_feed.end());
    hitFlash = std::max(0.0f, hitFlash - dt * 2.5f);
    suppression = std::max(0.0f, suppression - dt * 0.8f);
    flinch *= std::exp(-dt * 8.0f);
    lastHitTimer = std::max(0.0f, lastHitTimer - dt);
    if (mode == Mode::Raid && !playerExtracted && playerDeathTime < 0.0) stats.duration = time;
}
