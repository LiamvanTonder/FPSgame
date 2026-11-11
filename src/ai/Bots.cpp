#include "ai/Bots.h"

#include "game/Attachments.h"
#include "game/Simulation.h"
#include "game/WeaponDefs.h"
#include "render/Camera.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

float u01(std::mt19937& rng) { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
float u11(std::mt19937& rng) { return std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng); }
float wrap(float a) {
    while (a > float(M_PI)) a -= 2.0f * float(M_PI);
    while (a < -float(M_PI)) a += 2.0f * float(M_PI);
    return a;
}
float yawOf(const glm::vec3& d) { return std::atan2(-d.x, -d.z); }
glm::vec2 xz(const glm::vec3& v) { return {v.x, v.z}; }

float effectiveRange(const WeaponDef& w) {
    switch (w.cls) {
    case WeaponClass::Pistol: return 45.0f;
    case WeaponClass::Smg: return 90.0f;
    case WeaponClass::Shotgun: return 40.0f;
    case WeaponClass::Rifle: return 280.0f;
    case WeaponClass::Lmg: return 300.0f;
    case WeaponClass::Marksman: return 450.0f;
    case WeaponClass::Sniper: return 600.0f;
    }
    return 100.0f;
}

bool lineOfSight(const Simulation& sim, const glm::vec3& a, const glm::vec3& b, int self, int target) {
    SegmentQuery q;
    q.vision = true;
    q.ignoreOwner = self;
    WorldHit hit;
    if (!sim.world.segment(a, b, hit, q)) return true;
    return sim.world.collider(hit.collider).owner == target;
}

int spareRounds(const Character& c) {
    const Item* w = c.weapon.item();
    if (!w) return 0;
    const WeaponDef& d = c.weapon.def();
    int n = c.equip.roundsAvailable(d.caliber);
    for (const Grid* g : c.equip.grids())
        for (const Grid::Entry& e : g->items)
            if (e.item->d().cat == ItemCat::Magazine && magazineFits(e.item->d(), w->d().weapon)) n += int(e.item->rounds.size());
    return n;
}

// How much a bot wants a gun: value, plus a lot more if it can feed it.
float weaponScore(const Item& w, const Equipment& eq) {
    const ItemDef& d = w.d();
    const WeaponDef& wd = weaponDefs()[d.weapon];
    float s = float(d.value) + loadedRounds(w) * 15.0f;
    int ammo = eq.roundsAvailable(wd.caliber);
    for (const Grid* g : eq.grids())
        for (const Grid::Entry& e : g->items)
            if (e.item->d().cat == ItemCat::Magazine && magazineFits(e.item->d(), d.weapon)) ammo += int(e.item->rounds.size());
    s += std::min(ammo, 90) * 30.0f;
    if (loadedRounds(w) + ammo == 0) s *= 0.2f;
    return s;
}

} // namespace

// Sets where the bot wants to look; the turn itself happens smoothly in applyLook().
static void lookToward(Bot& b, float yaw, float rate) {
    b.wantYaw = yaw;
    b.turnRate = rate;
    b.lookStiffness = 8.0f;
}

// A neck and shoulders with inertia: accelerate toward the wanted direction, ease in, never snap.
static void applyLook(Bot& b, float dt) {
    float maxV = std::max(0.5f, b.turnRate);
    float accel = maxV * 7.0f;
    float e = wrap(b.wantYaw - b.yaw);
    float desired = std::clamp(e * b.lookStiffness, -maxV, maxV);
    b.yawVel += std::clamp(desired - b.yawVel, -accel * dt, accel * dt);
    b.yaw = wrap(b.yaw + b.yawVel * dt);
    float ep = b.wantPitch - b.pitch;
    float desiredP = std::clamp(ep * b.lookStiffness, -maxV, maxV);
    b.pitchVel += std::clamp(desiredP - b.pitchVel, -accel * dt, accel * dt);
    b.pitch += b.pitchVel * dt;
}

const char* goalName(Goal g) {
    switch (g) {
    case Goal::Roam: return "roaming";
    case Goal::Loot: return "looting";
    case Goal::Hunt: return "hunting";
    case Goal::Fight: return "fighting";
    case Goal::Heal: return "healing";
    case Goal::Extract: return "extracting";
    case Goal::Flee: return "fleeing";
    case Goal::SendBack: return "sending loot home";
    }
    return "?";
}

void BotManager::add(int charId, unsigned seed, double now) {
    remove(charId);
    Bot b;
    b.id = charId;
    b.rng.seed(seed);
    b.aggression = 0.2f + 0.8f * u01(b.rng);
    b.greed = u01(b.rng);
    b.caution = u01(b.rng);
    b.skill = 0.25f + 0.65f * u01(b.rng);
    b.spawnTime = now;
    b.nextPerceive = now + 0.1 * (charId % 10) / 10.0;
    b.nextThink = now + 0.05 * (charId % 5);
    bots.push_back(std::move(b));
}

void BotManager::remove(int charId) {
    if (m_updating) {
        removeLater(charId);
        return;
    }
    bots.erase(std::remove_if(bots.begin(), bots.end(), [&](const Bot& b) { return b.id == charId; }), bots.end());
}

Bot* BotManager::find(int charId) {
    for (Bot& b : bots)
        if (b.id == charId) return &b;
    return nullptr;
}

const CharacterInput* BotManager::input(int charId) const {
    for (const Bot& b : bots)
        if (b.id == charId) return &b.input;
    return nullptr;
}

Contact& BotManager::contact(Bot& b, int id) {
    for (Contact& c : b.contacts)
        if (c.id == id) return c;
    b.contacts.push_back(Contact{});
    b.contacts.back().id = id;
    return b.contacts.back();
}

std::string BotManager::describe(int charId) const {
    for (const Bot& b : bots)
        if (b.id == charId) return goalName(b.goal);
    return "";
}

// ------------------------------------------------------------------ senses

void BotManager::perceive(Bot& b, Simulation& sim) {
    Character& me = *sim.chars[b.id];
    const double now = sim.time;
    const float dt = float(std::max(0.02, now - b.lastPerceive));
    glm::vec3 eye = me.eye(), fwd = me.viewDir();

    for (auto& op : sim.chars) {
        Character& o = *op;
        if (o.id == me.id || !o.active) continue;
        glm::vec3 to = o.chest() - eye;
        float dist = glm::length(to);
        if (dist > 450.0f) continue;
        glm::vec3 dir = to / std::max(dist, 0.01f);
        float cosA = glm::dot(dir, fwd);
        float fov = cosA > 0.906f ? 1.0f : cosA > 0.26f ? 0.35f : 0.0f; // central 25 deg, peripheral to 75 deg
        if (o.dead) {
            // Bodies are remembered as loot.
            if (dist < 90.0f && fov > 0.0f && lineOfSight(sim, eye, o.chest(), me.id, o.id)) {
                Contact& c = contact(b, o.id);
                c.dead = true;
                c.position = o.chest();
                c.visible = false;
            }
            continue;
        }
        Contact& c = contact(b, o.id);
        c.dead = false;
        bool close = dist < 3.0f;
        if (fov <= 0.0f && !close) {
            c.visible = false;
            c.awareness = std::max(0.0f, c.awareness - 0.05f * dt);
            continue;
        }
        bool seen = lineOfSight(sim, eye, o.head(), me.id, o.id) || lineOfSight(sim, eye, o.chest(), me.id, o.id) ||
                    lineOfSight(sim, eye, o.joints[J_Pelvis], me.id, o.id);
        if (!seen) {
            c.visible = false;
            c.awareness = std::max(0.0f, c.awareness - 0.04f * dt);
            continue;
        }
        float distF = 1.0f / (1.0f + (dist / 55.0f) * (dist / 55.0f));
        float motion = 1.0f + 0.7f * o.horizontalSpeed();
        float stance = o.crouching ? 0.55f : 1.0f;
        float optics = me.weapon.isAiming() && cosA > 0.99f ? 1.0f + me.weapon.magnification() : 1.0f;
        float flash = now - o.lastShot < 0.4 ? 8.0f : 1.0f;
        // A lit weapon light (or a laser beam) gives its owner away; shone in the eyes, it dazzles.
        float lit = o.weapon.lightOn() ? 3.0f : o.weapon.laserOn() ? 1.8f : 1.0f;
        if (o.weapon.lightOn() && dist < 25.0f) {
            glm::vec3 beam = eye - o.weapon.lightEmitter();
            if (glm::dot(glm::normalize(beam), o.weapon.pointing()) > 0.995f) b.dazzledUntil = now + 0.5;
        }
        float dazzle = now < b.dazzledUntil ? 0.5f : 1.0f;
        float rate = 4.0f * fov * distF * motion * stance * optics * flash * lit * dazzle * (close ? 4.0f : 1.0f);
        c.awareness = std::min(1.5f, c.awareness + rate * dt);
        if (c.awareness >= 1.0f) {
            if (!c.visible) b.reaction = std::max(b.reaction, 0.25f + 0.6f * (1.0f - b.skill));
            c.visible = true;
            c.lastSeen = now;
            c.position = o.chest();
            c.velocity = o.velocity;
            c.uncertainty = 0.0f;
            c.power = kitPower(o, false); // what they carry is plain to see
        }
    }

    // Hearing: everything that reached these ears since the last look, with real propagation delay.
    for (const SoundEvent& e : sim.sound.events()) {
        if (e.owner == me.id) continue;
        Hearing h = SoundBus::hear(e, eye, nullptr);
        double arrival = e.time + h.delay;
        if (arrival <= b.lastPerceive || arrival > now) continue;
        float threshold = e.type == SoundType::Shot ? 38.0f : 42.0f;
        if (h.level < threshold) continue;
        h = SoundBus::hear(e, eye, &sim.world); // muffling through walls
        if (h.level < threshold) continue;

        float importance = 0.0f, uncertainty = 0.0f, aware = 0.0f;
        switch (e.type) {
        case SoundType::Shot: importance = 1.0f; uncertainty = 3.0f + 0.06f * h.distance; aware = 0.7f; break;
        case SoundType::Beacon: case SoundType::Flare: importance = 0.8f; uncertainty = 8.0f + 0.05f * h.distance; break;
        case SoundType::Footstep: importance = 0.3f; uncertainty = 1.0f + 0.15f * h.distance; aware = 0.45f; break;
        case SoundType::Reload: case SoundType::Med: case SoundType::Loot: case SoundType::DryFire:
            importance = 0.35f; uncertainty = 1.0f + 0.15f * h.distance; aware = 0.5f; break;
        case SoundType::Pain: case SoundType::BodyFall: importance = 0.6f; uncertainty = 2.0f + 0.1f * h.distance; aware = 0.5f; break;
        case SoundType::Impact: importance = 0.2f; uncertainty = 3.0f; break;
        case SoundType::Rustle: case SoundType::Breath: importance = 0.3f; uncertainty = 1.0f + 0.15f * h.distance; aware = 0.45f; break;
        default: break;
        }
        if (importance <= 0.0f) continue;
        glm::vec3 guess = e.position + glm::vec3(u11(b.rng), 0.0f, u11(b.rng)) * uncertainty;
        b.noises.push_back({guess, now, importance, e.owner});
        if (e.owner >= 0 && e.owner < int(sim.chars.size()) && aware > 0.0f) {
            Contact& c = contact(b, e.owner);
            if (!c.visible) {
                c.position = guess + glm::vec3(0.0f, 1.3f, 0.0f);
                c.uncertainty = uncertainty;
            }
            c.lastHeard = now;
            c.awareness = std::max(c.awareness, aware);
            if (e.type == SoundType::Pain) c.lastHurt = now;
            if (e.type == SoundType::Reload) c.lastReload = now;
        }
    }
    if (b.noises.size() > 24) b.noises.erase(b.noises.begin(), b.noises.begin() + (b.noises.size() - 24));
    b.noises.erase(std::remove_if(b.noises.begin(), b.noises.end(), [&](const Noise& n) { return now - n.time > 60.0; }), b.noises.end());
    b.lastPerceive = now;
}

void BotManager::onDamaged(int victim, int shooter, const glm::vec3& from, Simulation& sim) {
    Bot* b = find(victim);
    if (!b) return;
    b->lastDamaged = sim.time;
    b->damagedFrom = from;
    if (shooter >= 0 && shooter < int(sim.chars.size()) && shooter != victim) {
        Contact& c = contact(*b, shooter);
        float d = glm::length(from - sim.chars[victim]->eye());
        if (!c.visible) {
            c.position = from + glm::vec3(u11(b->rng), 0.0f, u11(b->rng)) * (2.0f + 0.04f * d);
            c.uncertainty = 2.0f + 0.04f * d;
        }
        c.lastHeard = sim.time;
        c.awareness = std::max(c.awareness, 0.85f);
    }
    b->hasCover = false; // that spot is not safe
    b->nextThink = sim.time;
}

void BotManager::onNearMiss(int victim, int shooter, const glm::vec3& from, Simulation& sim) {
    Bot* b = find(victim);
    if (!b) return;
    if (shooter >= 0 && shooter < int(sim.chars.size()) && shooter != victim) {
        Contact& c = contact(*b, shooter);
        float d = glm::length(from - sim.chars[victim]->eye());
        if (!c.visible) {
            c.position = from + glm::vec3(u11(b->rng), 0.0f, u11(b->rng)) * (4.0f + 0.06f * d);
            c.uncertainty = 4.0f + 0.06f * d;
        }
        c.lastHeard = sim.time;
        c.awareness = std::max(c.awareness, 0.75f);
    }
    b->nextThink = std::min(b->nextThink, sim.time + 0.1);
}

// ------------------------------------------------------------------ decisions

Contact* BotManager::bestThreat(Bot& b, Simulation& sim) {
    Character& me = *sim.chars[b.id];
    Contact* best = nullptr;
    float bestScore = -1.0f;
    for (Contact& c : b.contacts) {
        if (c.dead || c.id < 0 || c.id >= int(sim.chars.size()) || !sim.chars[c.id]->alive()) continue;
        double age = std::min(sim.time - c.lastSeen, sim.time - c.lastHeard);
        if (age > 20.0 || c.awareness < 0.35f) continue;
        float d = glm::length(c.position - me.eye());
        float score = (c.visible ? 3.0f : 1.0f) / (1.0f + d / 50.0f) / (1.0f + float(age) / 4.0f);
        if (score > bestScore) {
            bestScore = score;
            best = &c;
        }
    }
    return best;
}

bool BotManager::hasMeds(const Character& c) const {
    // Only meds that would treat something right now count (surgery needs a quiet moment).
    const Bot* b = nullptr;
    for (const Bot& x : bots)
        if (x.id == c.id) b = &x;
    for (const Grid* g : c.equip.grids())
        for (const Grid::Entry& e : g->items) {
            if (e.item->d().cat != ItemCat::Med) continue;
            MedKind k = e.item->d().med;
            if (k == MedKind::Surgical && b && m_now - b->lastDamaged < 20.0) continue;
            if (c.health.bestZoneFor(k) >= 0) return true;
        }
    return false;
}

bool BotManager::wantsExtract(const Bot& b, const Simulation& sim) const {
    const Character& me = *sim.chars[b.id];
    bool backFull = me.equip.slots[ES_Back0] && me.equip.slots[ES_Back1];
    float hp = me.health.totalHp() / me.health.totalMax();
    double alive = sim.time - b.spawnTime;
    int value = me.equip.totalValue();
    if (backFull && (b.sentBack || b.greed < 0.6f)) return true;
    if (value > 9000 + int(14000 * b.greed) && alive > 120.0) return true;
    if (b.spawnValue > 0 && value > b.spawnValue * 2.5f + 6000.0f * (1.0f - b.caution) && alive > 90.0) return true;
    if (hp < 0.4f && !hasMeds(me)) return true;
    if (me.weapon.ammo() + spareRounds(me) < 8 && alive > 60.0) return true;
    return alive > 900.0 + 900.0 * b.greed;
}

bool BotManager::chooseLootTarget(Bot& b, Simulation& sim) {
    Character& me = *sim.chars[b.id];
    glm::vec2 p = xz(me.position);
    float best = 0.0f;
    int type = 0, idx = -1;
    // Weapons lying around (dropped by the dead)
    for (int i = 0; i < int(sim.loot.items.size()); ++i) {
        const WorldItem& w = sim.loot.items[i];
        float d = glm::length(xz(w.position) - p);
        if (d > 60.0f || !w.resting) continue;
        bool skip = false;
        for (const glm::vec2& g : b.ignored) skip |= glm::length(g - xz(w.position)) < 3.0f;
        if (skip) continue;
        bool backFree = !me.equip.slots[ES_Back0] || !me.equip.slots[ES_Back1] || !me.equip.slots[ES_Hand0];
        if (w.item->d().cat == ItemCat::Weapon && !backFree) continue;
        float s = (w.item->d().cat == ItemCat::Weapon ? 3.0f : 1.0f) * (1.0f + w.item->d().value / 2000.0f) / (d + 10.0f);
        if (s > best) {
            best = s;
            type = 3;
            idx = i;
        }
    }
    // Known bodies
    for (const Contact& c : b.contacts) {
        if (!c.dead || std::find(b.doneBodies.begin(), b.doneBodies.end(), c.id) != b.doneBodies.end()) continue;
        if (c.id < 0 || c.id >= int(sim.chars.size()) || !sim.chars[c.id]->active || !sim.chars[c.id]->dead) continue;
        float d = glm::length(xz(sim.chars[c.id]->chest()) - p);
        if (d > 150.0f) continue;
        // Bodies draw people: only go when there's been no shooting there for a while and
        // whoever did it isn't still around.
        glm::vec3 bodyPos = sim.chars[c.id]->chest();
        bool deferred = false;
        for (auto& db : b.deferredBodies) deferred |= db.first == c.id && sim.time - db.second < 30.0;
        if (deferred) continue;
        bool mine = std::find(b.myKills.begin(), b.myKills.end(), c.id) != b.myKills.end();
        double quiet = mine ? 5.0 : 10.0 + 10.0 * b.caution;
        bool risky = false;
        for (const Noise& n : b.noises)
            if (n.importance >= 1.0f && sim.time - n.time < quiet && glm::length(n.position - bodyPos) < 40.0f && n.owner != b.id) risky = true;
        for (const Contact& k : b.contacts)
            if (!k.dead && k.id != c.id && k.id >= 0 && k.id < int(sim.chars.size()) && sim.chars[k.id]->alive() &&
                sim.time - std::max(k.lastSeen, k.lastHeard) < 20.0 && glm::length(k.position - bodyPos) < 35.0f)
                risky = true;
        if (risky) continue;
        float s = 4.0f / (d + 10.0f);
        if (s > best) {
            best = s;
            type = 2;
            idx = c.id;
        }
    }
    // Containers (they know the map, like the player)
    for (int i = 0; i < int(sim.loot.containers.size()); ++i) {
        if (std::find(b.doneContainers.begin(), b.doneContainers.end(), i) != b.doneContainers.end()) continue;
        const LootContainer& c = sim.loot.containers[i];
        float d = glm::length(xz(c.position) - p);
        if (d > 300.0f) continue;
        float s = (c.hotspot >= 0 ? 3.0f : 1.0f) * (1.0f + c.tier) * (c.type == ContainerType::WeaponCrate ? 2.0f : 1.0f) / (d + 25.0f);
        if (c.lootZone) s *= b.power > 0.55f ? 2.5f : b.power < 0.35f ? (b.zoneLoots >= 2 ? 0.1f : 1.3f) : 1.6f;
        s *= 0.6f + 0.8f * b.greed;
        if (s > best) {
            best = s;
            type = 1;
            idx = i;
        }
    }
    b.lootType = type;
    b.lootIndex = idx;
    return type != 0;
}

void BotManager::think(Bot& b, Simulation& sim) {
    Character& me = *sim.chars[b.id];
    const double now = sim.time;
    Contact* threat = bestThreat(b, sim);
    bool visible = threat && threat->visible;
    float hp = me.health.totalHp() / me.health.totalMax();
    b.power = kitPower(me);
    if (b.spawnValue < 0) {
        b.spawnValue = me.equip.totalValue();
        b.ambusher = b.power < 0.4f && sim.map.lootZone.radius > 0.0f && u01(b.rng) < 0.25f;
    }
    if (sim.nav.valid() && sim.nav.floorCount() > 0 && sim.nav.levelAt(me.position) > 0) ++sim.stats.botUpstairs;
    bool inZone = sim.map.inLootZone(me.position);
    if (inZone && !b.inZone) ++sim.stats.botLootZone;
    b.inZone = inZone;
    bool meds = hasMeds(me);
    int ammo = me.weapon.ammo() + spareRounds(me);
    Goal g;

    double sinceDamage = now - b.lastDamaged;
    bool treatable = me.health.bleeding() || me.health.anyFracture() || hp < 0.6f || me.health.pain() > 0.6f;
    if (me.health.heavyBleeding() && meds && (!visible || hp < 0.35f)) g = Goal::Heal;
    // Hurt in a fight: get out of sight and patch up rather than trade shots while bleeding.
    else if (threat && meds && !visible && now - threat->lastSeen > 1.5 && (me.health.bleeding() || hp < 0.5f) && sinceDamage > 1.0) g = Goal::Heal;
    else if (threat && (visible || now - threat->lastSeen < 8.0 || (now - threat->lastHeard < 5.0 && glm::length(threat->position - me.eye()) < 80.0f) ||
                        sinceDamage < 4.0)) {
        bool outgunned = (hp < 0.35f && b.aggression < 0.6f) || ammo == 0 || !me.weapon.hasWeapon();
        g = outgunned ? Goal::Flee : Goal::Fight;
    } else if (treatable && meds) g = Goal::Heal;
    else if (wantsExtract(b, sim)) {
        bool backFull = me.equip.slots[ES_Back0] && me.equip.slots[ES_Back1];
        g = (backFull && !b.sentBack && b.greed >= 0.6f) ? Goal::SendBack : Goal::Extract;
    } else {
        // Gunfire draws the aggressive (and the greedy: fights leave bodies).
        const Noise* lure = nullptr;
        for (const Noise& n : b.noises)
            if (n.importance >= 0.8f && now - n.time < 25.0 && glm::length(n.position - me.position) < 320.0f) lure = &n;
        // A fight between others: the well-armed and the bold go and take advantage; the poorly armed stay away.
        if (lure && b.aggression + 0.3f * b.greed + (b.power - 0.5f) > 0.6f && b.power > 0.35f) g = Goal::Hunt;
        else if (b.lootType != 0 || chooseLootTarget(b, sim)) g = Goal::Loot;
        else g = Goal::Roam;
    }
    if (g != b.goal) {
        b.goal = g;
        b.goalSince = now;
        b.wantPath = true;
        b.peeking = false;
        if (g != Goal::Fight && g != Goal::Heal) b.hasCover = false;
        if (g == Goal::Fight) {
            setTactic(b, Tactic::Engage, now);
            b.engageSince = now;
            b.hasCover = false;
            b.coverTime = -1e9;
            b.peeks = 0;
        }
        if (g == Goal::Heal) b.healSpotSet = false;
        if (g != Goal::Loot) me.cancelAction();
    }
    b.target = threat ? threat->id : -1;
}

// ------------------------------------------------------------------ motor control

void BotManager::steer(Bot& b, Simulation& sim, const glm::vec2& dir, bool sprint) {
    Character& me = *sim.chars[b.id];
    glm::vec2 d = dir;
    // Personal space: slide past other people.
    for (auto& op : sim.chars) {
        if (op->id == me.id || !op->alive()) continue;
        glm::vec2 off = xz(me.position - op->position);
        float l = glm::length(off);
        if (l < 1.2f && l > 1e-3f) d += off / l * (1.2f - l) * 1.5f;
    }
    float l = glm::length(d);
    if (l < 1e-4f) return;
    d /= l;
    glm::vec2 fwd(-std::sin(b.yaw), -std::cos(b.yaw)), right(std::cos(b.yaw), -std::sin(b.yaw));
    b.input.move = glm::vec2(glm::dot(d, right), glm::dot(d, fwd));
    b.input.sprint = sprint && b.input.move.y > 0.8f;
}

void BotManager::moveTo(Bot& b, const glm::vec2& dest, Simulation& sim, bool sprintOk, float stopDist) {
    moveTo3(b, glm::vec3(dest.x, -1.0e4f, dest.y), sim, sprintOk, stopDist); // ground level
}

void BotManager::moveTo3(Bot& b, const glm::vec3& dest3, Simulation& sim, bool sprintOk, float stopDist) {
    Character& me = *sim.chars[b.id];
    glm::vec2 p = xz(me.position);
    glm::vec2 dest = xz(dest3);
    // On another level (upstairs, roof): not there yet, however close it looks from below.
    bool otherLevel = sim.nav.valid() && sim.nav.floorCount() > 0 && sim.nav.levelAt(me.position) != sim.nav.levelAt(dest3);
    float toDest = glm::length(dest - p) + (otherLevel ? 8.0f : 0.0f);
    if (toDest < stopDist) {
        b.path.clear();
        b.debug = "arrived";
        return;
    }
    bool finished = b.pathIndex >= b.path.size();
    float moved = glm::length(dest - xz(b.destination)) + (b.destination.x > 1e8f ? 1e9f : 0.0f);
    if (moved > 4.0f || (moved > 1.0f && sim.time - b.pathTime > 1.5) || sim.time - b.pathTime > 20.0 || (finished && sim.time - b.pathTime > 1.5))
        b.wantPath = true;
    if (b.wantPath && pathBudget > 0) {
        --pathBudget;
        b.wantPath = false;
        b.destination = dest3;
        b.pathTime = sim.time;
        b.pathIndex = 0;
        glm::vec3 to = dest3;
        if (to.y < -1000.0f) to.y = sim.nav.valid() ? sim.nav.levelHeight(glm::vec3(dest.x, -1.0e4f, dest.y)) : 0.0f;
        // Away from enemy eyes: ground they can see costs a lot more (worked out lazily, 3 m cells).
        std::vector<glm::vec3> threats;
        if (b.hiddenRoute) threats = knownThreats(b, sim, 150.0f, 20.0);
        b.hiddenRoute = false;
        std::vector<int8_t> vis;
        glm::vec2 origin = glm::min(p, dest) - glm::vec2(30.0f);
        const float cell = 3.0f;
        int gw = 0, gh = 0;
        if (!threats.empty()) {
            glm::vec2 size = glm::abs(dest - p) + glm::vec2(60.0f);
            gw = std::min(80, int(size.x / cell) + 1);
            gh = std::min(80, int(size.y / cell) + 1);
            vis.assign(size_t(gw) * gh, -1);
            sim.nav.extraCost = [&](float x, float z) {
                int ix = int((x - origin.x) / cell), iz = int((z - origin.y) / cell);
                if (ix < 0 || iz < 0 || ix >= gw || iz >= gh) return 1.0f;
                int8_t& v = vis[size_t(iz) * gw + ix];
                if (v < 0) {
                    glm::vec2 c = origin + glm::vec2((ix + 0.5f) * cell, (iz + 0.5f) * cell);
                    float y = sim.map.hasTerrain ? sim.terrain.height(c.x, c.y) : me.position.y;
                    v = 0;
                    for (const glm::vec3& t : threats)
                        if (lineOfSight(sim, glm::vec3(c.x, y + 1.3f, c.y), t, me.id, -2)) {
                            v = 1;
                            break;
                        }
                }
                return v ? 5.0f : 1.0f;
            };
        }
        if (!sim.nav.valid() || !sim.nav.findPath3(me.position, to, b.path)) b.path = {to};
        sim.nav.extraCost = nullptr;
    }
    while (b.pathIndex < b.path.size()) {
        float d = glm::length(xz(b.path[b.pathIndex]) - p);
        bool last = b.pathIndex + 1 >= b.path.size();
        bool climb = std::abs(b.path[b.pathIndex].y - me.position.y) > 0.8f; // a stair head: get up there first
        // Corners (doorways) must actually be reached; skip only when the next leg is clear from here.
        if (climb) {
            if (d < 0.35f) ++b.pathIndex;
            else break;
        } else if (d < 0.3f || (d < 1.5f && !last && sim.nav.clearLine3(me.position, b.path[b.pathIndex + 1])) || (last && d < 0.7f)) {
            ++b.pathIndex;
        } else {
            break;
        }
    }
    glm::vec2 wp;
    if (b.pathIndex < b.path.size()) {
        wp = xz(b.path[b.pathIndex]);
        b.debug = "path";
    } else if (toDest < 6.0f && b.allowFinal) {
        // End of the walkable route: the last step to an object standing in a blocked cell.
        wp = dest;
        b.debug = "final approach";
    } else {
        b.debug = "waiting for path";
        return;
    }
    if (sim.time < b.unstickUntil) {
        // Backing off a corner it walked into, then trying again.
        steer(b, sim, b.unstickDir, false);
        return;
    }
    // Look ahead around the corner so the route is a curve, not a zig-zag.
    glm::vec2 dir = wp - p;
    if (b.pathIndex + 1 < b.path.size() && glm::length(dir) < 2.5f && std::abs(b.path[b.pathIndex + 1].y - me.position.y) < 0.8f &&
        sim.nav.clearLine3(me.position, b.path[b.pathIndex + 1])) {
        glm::vec2 next = xz(b.path[b.pathIndex + 1]) - p;
        float w = 1.0f - glm::length(dir) / 2.5f;
        dir = glm::normalize(dir + 1e-4f) * (1.0f - w) + glm::normalize(next + 1e-4f) * w;
    }
    steer(b, sim, dir, sprintOk && toDest > 12.0f);

    // No progress toward the destination for a long time (circling, unreachable): give up on it.
    if (glm::length(dest - b.progressDest) > 3.0f) {
        b.progressDest = dest;
        b.progressBest = toDest;
        b.progressTime = sim.time;
    } else if (toDest < b.progressBest - 1.0f) {
        b.progressBest = toDest;
        b.progressTime = sim.time;
    } else if (sim.time - b.progressTime > 20.0) {
        b.ignored.push_back(dest);
        if (b.lootType == 1) b.doneContainers.push_back(b.lootIndex);
        if (b.lootType == 2) b.doneBodies.push_back(b.lootIndex);
        b.lootType = 0;
        b.roamTarget = glm::vec2(1e9f);
        b.progressTime = sim.time;
        b.wantPath = true;
        b.debug = "gave up";
    }

    // Stuck: hop and pick a new route.
    if (sim.time - b.stuckSince > 3.0) {
        if (glm::length(p - b.stuckPos) < 0.6f) {
            ++b.stuckCount;
            b.input.jump = b.stuckCount % 2 == 0;
            // Step back and to the side, then re-plan.
            glm::vec2 fwd = glm::normalize(wp - p + glm::vec2(1e-4f));
            float side = u01(b.rng) < 0.5f ? -1.0f : 1.0f;
            b.unstickDir = -fwd * 0.6f + glm::vec2(-fwd.y, fwd.x) * side;
            b.unstickUntil = sim.time + 0.7 + 0.5 * u01(b.rng);
            b.wantPath = true;
            b.destination = glm::vec3(1e9f);
            b.debug = "stuck";
            if (b.stuckCount > 3) {
                // Give up on this objective.
                if (b.lootType == 1) b.doneContainers.push_back(b.lootIndex);
                if (b.lootType == 2) b.doneBodies.push_back(b.lootIndex);
                b.lootType = 0;
                b.roamTarget = glm::vec2(1e9f);
                b.stuckCount = 0;
                if (b.goal == Goal::Hunt) b.noises.clear(); // can't get there: stop chasing that sound
            }
        } else {
            b.stuckCount = 0;
        }
        b.stuckPos = p;
        b.stuckSince = sim.time;
    }
}

// ------------------------------------------------------------------ tactics

float BotManager::kitPower(const Character& c, bool withHealth) {
    // The gun: its class and quality. What others can see is the gun and the armour.
    const Item* gun = c.weapon.item();
    for (int s : {ES_Hand0, ES_Hand1})
        if (!gun && c.equip.slots[s] && c.equip.slots[s]->d().cat == ItemCat::Weapon) gun = c.equip.slots[s].get();
    float weapon = 0.05f;
    if (gun) {
        const WeaponDef& d = gun->weaponDef();
        switch (d.cls) {
        case WeaponClass::Pistol: weapon = 0.25f; break;
        case WeaponClass::Smg: case WeaponClass::Shotgun: weapon = 0.5f; break;
        case WeaponClass::Rifle: weapon = 0.72f; break;
        case WeaponClass::Lmg: weapon = 0.8f; break;
        case WeaponClass::Marksman: weapon = 0.75f; break;
        case WeaponClass::Sniper: weapon = 0.7f; break;
        }
        weapon += std::min(0.2f, float(d.value) / 40000.0f);
        if (withHealth && loadedRounds(*gun) + c.equip.roundsAvailable(d.caliber) < 15) weapon *= 0.6f;
    }
    float armor = 0.0f;
    if (const ItemPtr& a = c.equip.slots[ES_Armor])
        armor += a->d().armorClass / 4.0f * std::clamp(a->condition / std::max(1.0f, a->d().durability), 0.0f, 1.0f) * 0.6f;
    if (const ItemPtr& h = c.equip.slots[ES_Helmet])
        armor += h->d().armorClass / 3.0f * std::clamp(h->condition / std::max(1.0f, h->d().durability), 0.0f, 1.0f) * 0.4f;
    float hp = withHealth ? c.health.totalHp() / std::max(1.0f, c.health.totalMax()) : 1.0f;
    return std::clamp(0.6f * weapon + 0.25f * armor + 0.15f * hp, 0.0f, 1.0f);
}

void BotManager::onHitLanded(int shooter, int victim, Simulation& sim) {
    Bot* b = find(shooter);
    if (!b) return;
    contact(*b, victim).lastHurt = sim.time;
}

void BotManager::setTactic(Bot& b, Tactic t, double now) {
    if (b.tactic == t) return;
    b.tactic = t;
    b.tacticSince = now;
    b.tacticPoint = glm::vec3(1e9f);
    b.wantPath = true;
}

std::vector<glm::vec3> BotManager::knownThreats(Bot& b, Simulation& sim, float within, double recent) {
    std::vector<glm::vec3> out;
    Character& me = *sim.chars[b.id];
    for (const Contact& c : b.contacts) {
        if (c.dead || c.id < 0 || c.id >= int(sim.chars.size()) || !sim.chars[c.id]->alive()) continue;
        double age = sim.time - std::max(c.lastSeen, c.lastHeard);
        if (age > recent || c.awareness < 0.35f) continue;
        if (glm::length(c.position - me.eye()) > within) continue;
        out.push_back(c.position);
        if (out.size() >= 3) break;
    }
    return out;
}

// Spots near cover that hide a crouched body from the threat; better if a standing or leaning
// shooter can see out, at a sensible range, and not toward the enemy. Upstairs windows count.
bool BotManager::findCoverSpot(Bot& b, Simulation& sim, const glm::vec3& threat, bool wantCloser, bool avoidCurrent, glm::vec3& out,
                               bool& window) {
    Character& me = *sim.chars[b.id];
    float dist = glm::length(threat - me.eye());
    float ideal = std::clamp(effectiveRange(me.weapon.def()) * 0.45f, 12.0f, 70.0f);
    struct Cand {
        glm::vec3 p;
        bool win;
        float pre;
    };
    std::vector<Cand> cands;
    if (sim.nav.valid()) {
        std::vector<NavGrid::Cover> covers;
        sim.nav.findCover(xz(me.position), xz(threat), 28.0f, covers, 24);
        for (const NavGrid::Cover& cv : covers) {
            float y = sim.map.hasTerrain ? sim.terrain.height(cv.position.x, cv.position.y) : me.position.y;
            cands.push_back({glm::vec3(cv.position.x, y, cv.position.y), false, 0.0f});
        }
    }
    for (const FiringSpot& fs : sim.map.firingSpots) {
        if (glm::length(xz(fs.position - me.position)) > 32.0f) continue;
        glm::vec2 toT = xz(threat - fs.position);
        if (glm::length(toT) < 1.0f || glm::dot(fs.facing, glm::normalize(toT)) < 0.35f) continue;
        cands.push_back({fs.position, true, 0.0f});
    }
    glm::vec3 toThreat = threat - me.position;
    for (Cand& c : cands) {
        float dMe = glm::length(c.p - me.position), dT = glm::length(threat - c.p);
        c.pre = -dMe * 0.6f - std::abs(dT - ideal) * 0.08f;
        if (c.win) c.pre += 3.0f + 0.3f * std::clamp(c.p.y - threat.y + 1.3f, 0.0f, 8.0f);
        if (wantCloser && dT > dist * 0.75f) c.pre -= 20.0f;
        if (avoidCurrent && glm::length(xz(c.p) - b.lastCover) < 2.5f) c.pre -= 100.0f;
        if (dT < 6.0f) c.pre -= 30.0f;
        if (!wantCloser && glm::dot(c.p - me.position, toThreat) > 0.0f && dT < dist * 0.5f) c.pre -= 8.0f;
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& c) { return a.pre > c.pre; });
    float best = -1e9f;
    bool found = false;
    int tried = 0;
    for (const Cand& c : cands) {
        if (++tried > 10) break;
        if (c.pre < -60.0f) break;
        glm::vec3 d = glm::normalize(glm::vec3(threat.x - c.p.x, 0.0f, threat.z - c.p.z) + glm::vec3(1e-4f));
        glm::vec3 side(-d.z, 0.0f, d.x);
        bool canShoot = lineOfSight(sim, c.p + glm::vec3(0, 1.62f, 0), threat, me.id, -2) ||
                        lineOfSight(sim, c.p + glm::vec3(0, 1.5f, 0) + side * 0.45f, threat, me.id, -2) ||
                        lineOfSight(sim, c.p + glm::vec3(0, 1.5f, 0) - side * 0.45f, threat, me.id, -2);
        glm::vec3 hideAt = c.p;
        if (c.win) {
            // At a window you hide beside it, behind the wall.
            if (!canShoot) continue;
            bool ok = false;
            for (float s : {0.9f, -0.9f}) {
                glm::vec3 q = c.p + glm::vec3(-d.z, 0.0f, d.x) * s - d * 0.3f;
                if (!sim.nav.walkable3(q)) continue;
                if (!lineOfSight(sim, q + glm::vec3(0, 1.15f, 0), threat, me.id, -2) && !lineOfSight(sim, q + glm::vec3(0, 1.6f, 0), threat, me.id, -2)) {
                    hideAt = q;
                    ok = true;
                    break;
                }
            }
            if (!ok) continue;
        } else if (lineOfSight(sim, c.p + glm::vec3(0, 1.15f, 0), threat, me.id, -2) || lineOfSight(sim, c.p + glm::vec3(0, 0.6f, 0), threat, me.id, -2)) {
            continue; // not actually covered from there
        }
        float score = c.pre + (canShoot ? 5.0f : 0.0f);
        if (score > best) {
            best = score;
            out = c.win ? c.p : hideAt;
            window = c.win;
            b.tacticPoint = hideAt; // where to duck back to
            found = true;
        }
    }
    return found;
}

// Somewhere none of the threats can see, preferably further from them.
bool BotManager::findHiddenSpot(Bot& b, Simulation& sim, const std::vector<glm::vec3>& threats, float minDist, float maxDist, glm::vec3& out) {
    Character& me = *sim.chars[b.id];
    bool upstairs = sim.nav.valid() && sim.nav.levelAt(me.position) > 0;
    float best = -1e9f;
    bool found = false;
    glm::vec2 away(0.0f);
    for (const glm::vec3& t : threats) away += glm::normalize(xz(me.position - t) + glm::vec2(1e-4f));
    if (glm::length(away) > 1e-3f) away = glm::normalize(away);
    for (int r = 0; r < 3; ++r) {
        float rad = minDist + (maxDist - minDist) * (r + 0.5f) / 3.0f;
        for (int k = 0; k < 12; ++k) {
            float a = k / 12.0f * 6.2832f + u11(b.rng) * 0.2f;
            glm::vec2 q = xz(me.position) + glm::vec2(std::cos(a), std::sin(a)) * rad;
            glm::vec3 p;
            if (upstairs) {
                p = glm::vec3(q.x, me.position.y, q.y);
                if (!sim.nav.walkable3(p)) continue;
            } else {
                glm::vec2 w;
                if (!sim.nav.valid() || !sim.nav.nearestWalkable(q, w, 3)) continue;
                p = glm::vec3(w.x, sim.map.hasTerrain ? sim.terrain.height(w.x, w.y) : me.position.y, w.y);
            }
            float score = glm::dot(glm::normalize(q - xz(me.position)), away) * 6.0f - rad * 0.15f;
            if (score <= best) continue;
            bool hidden = true;
            for (const glm::vec3& t : threats)
                if (lineOfSight(sim, p + glm::vec3(0, 1.2f, 0), t, me.id, -2) || lineOfSight(sim, p + glm::vec3(0, 1.65f, 0), t, me.id, -2)) {
                    hidden = false;
                    break;
                }
            if (!hidden) continue;
            best = score;
            out = p;
            found = true;
        }
    }
    return found;
}

void BotManager::manageLight(Bot& b, Simulation& sim, bool fighting, float dist) {
    Character& me = *sim.chars[b.id];
    Item* it = me.weapon.item();
    if (!it) return;
    int light = me.weapon.def().light;
    if (!light) return;
    bool indoors = false;
    {
        WorldHit hit;
        SegmentQuery q;
        q.ignoreOwner = me.id;
        indoors = sim.world.segment(me.eye(), me.eye() + glm::vec3(0, 4.0f, 0), hit, q) && sim.world.collider(hit.collider).owner == kOwnerWorld;
    }
    bool quiet = knownThreats(b, sim, 80.0f, 30.0).empty();
    bool want = false;
    if ((light & 2) && fighting && dist < 25.0f) want = true;                       // laser up close
    if ((light & 1) && indoors && (quiet || (fighting && dist < 12.0f))) want = true; // light to clear rooms
    if (fighting && (light & 1) && !(light & 2) && dist > 12.0f) want = false;      // a lit light gives you away
    if (want != it->lightOn) b.input.weapon.toggleLight = true;
}

void BotManager::fight(Bot& b, float dt, Simulation& sim, Contact& c) {
    Character& me = *sim.chars[b.id];
    Character& t = *sim.chars[c.id];
    const double now = sim.time;
    glm::vec3 eye = me.eye();
    bool visible = c.visible && now - c.lastSeen < 0.35;
    float dist = glm::length(c.position - eye);
    const WeaponDef& wd = me.weapon.def();
    float range = effectiveRange(wd);
    bool underFire = now - b.lastDamaged < 2.5 || me.suppression > 0.5f;

    // ----- the gun: reload, or go to the sidearm when it's empty and they're close
    int other = 1 - me.weapon.hand();
    bool otherLoaded = me.equip.slots[other] && me.equip.slots[other]->d().cat == ItemCat::Weapon && loadedRounds(*me.equip.slots[other]) > 0;
    bool empty = me.weapon.hasWeapon() && me.weapon.ammo() == 0;
    if (empty && otherLoaded && (dist < 20.0f || spareRounds(me) == 0) && !me.weapon.isSwitching()) {
        b.input.weapon.selectWeapon = other;
    } else {
        bool reload = me.weapon.hasWeapon() && !me.weapon.isReloading() && !me.weapon.isHandLoading() &&
                      (me.weapon.ammo() == 0 || (!visible && me.weapon.ammo() < me.weapon.capacity() * 0.4f));
        if (reload) {
            b.input.weapon.reloadPressed = true;
            if (visible && !b.hasCover) b.coverTime = -1e9; // get behind something to reload
        }
    }
    if (!me.weapon.hasWeapon() && me.equip.slots[other]) b.input.weapon.selectWeapon = other;

    // ----- what kind of fight this is
    bool wounded = now - c.lastHurt < 8.0, reloading = now - c.lastReload < 2.5;
    bool outmatched = (b.power < c.power - 0.25f || (b.power < 0.35f && dist > 35.0f)) && !wounded && !reloading;
    if (glm::length(c.position - c.seenAt) > 2.5f || c.seenAt.x > 1e8f) {
        c.seenAt = c.position;
        c.stillSince = now;
    }
    switch (b.tactic) {
    case Tactic::Engage:
        if (outmatched && dist > 15.0f && now - b.tacticSince > 1.0) {
            setTactic(b, Tactic::Withdraw, now);
            ++sim.stats.botWithdrawals;
        } else if ((wounded || reloading || b.power > c.power + 0.3f) && b.aggression + (wounded ? 0.3f : 0.0f) > 0.55f && dist > 10.0f &&
                   dist < range && now - b.tacticSince > 2.0) {
            setTactic(b, Tactic::Push, now);
            ++sim.stats.botPushes;
        } else if (now - c.stillSince > 8.0 + 7.0 * b.caution && now - b.lastFlank > 30.0 && now - b.lastDamaged > 4.0 &&
                   (b.aggression > 0.5f || b.power > c.power + 0.15f) && dist < range * 0.9f && dist > 15.0f && now - b.tacticSince > 3.0) {
            setTactic(b, Tactic::Flank, now);
            b.lastFlank = now;
            ++sim.stats.botFlanks;
        } else if (!visible && now - c.lastSeen > 5.0 && now - b.lastDamaged > 3.0 && now - b.tacticSince > 2.0) {
            setTactic(b, Tactic::Search, now);
        }
        break;
    case Tactic::Push:
        if (now - b.tacticSince > 12.0 || (!wounded && !reloading && b.power <= c.power + 0.3f && now - b.tacticSince > 4.0) || underFire)
            setTactic(b, Tactic::Engage, now);
        break;
    case Tactic::Flank:
        if (now - b.tacticSince > 30.0 || (visible && dist < 25.0f) || underFire) setTactic(b, Tactic::Engage, now);
        break;
    case Tactic::Withdraw:
        if (now - b.tacticSince > 20.0 || (dist < 10.0f && visible)) setTactic(b, Tactic::Engage, now);
        break;
    case Tactic::Search:
        if (visible || underFire) setTactic(b, Tactic::Engage, now);
        break;
    }

    bool crouch = false;
    float lean = 0.0f;
    float toCover = 1e9f;
    auto coverPos = [&]() { return glm::vec3(b.cover.x, b.coverY, b.cover.y); };
    auto distTo = [&](const glm::vec3& p) {
        return glm::length(xz(p) - xz(me.position)) + (std::abs(p.y - me.position.y) > 1.6f ? 6.0f : 0.0f);
    };

    if (b.tactic == Tactic::Withdraw || b.tactic == Tactic::Flank || b.tactic == Tactic::Search) {
        // Moves along routes the enemy can't see.
        if (b.tacticPoint.x > 1e8f) {
            glm::vec3 p;
            bool ok = false;
            if (b.tactic == Tactic::Withdraw) {
                ok = findHiddenSpot(b, sim, knownThreats(b, sim, 150.0f, 15.0), 18.0f, 40.0f, p);
            } else if (b.tactic == Tactic::Flank) {
                // Round the side, 70-110 degrees off the line between us, a bit closer than now.
                glm::vec2 from = xz(me.position - c.position);
                float r0 = std::clamp(glm::length(from) * 0.7f, 12.0f, range * 0.6f);
                float base = std::atan2(from.y, from.x);
                for (int k = 0; k < 6 && !ok; ++k) {
                    float off = (1.22f + 0.35f * u01(b.rng)) * (k % 2 == 0 ? 1.0f : -1.0f) * (b.strafeSide >= 0.0f ? 1.0f : -1.0f);
                    glm::vec2 q = xz(c.position) + glm::vec2(std::cos(base + off), std::sin(base + off)) * r0;
                    glm::vec2 w;
                    if (sim.nav.valid() && sim.nav.nearestWalkable(q, w, 6)) {
                        p = glm::vec3(w.x, sim.map.hasTerrain ? sim.terrain.height(w.x, w.y) : me.position.y, w.y);
                        ok = true;
                    }
                }
            } else {
                // Search: a vantage point short of where they were.
                glm::vec3 to = c.position - me.position;
                float l = glm::length(xz(to));
                glm::vec3 q = c.position - glm::vec3(to.x, 0, to.z) / std::max(l, 0.1f) * std::min(8.0f, l);
                p = glm::vec3(q.x, c.position.y - 1.3f, q.z);
                ok = true;
            }
            if (ok) {
                b.tacticPoint = p;
                b.hiddenRoute = true;
                b.wantPath = true;
            } else {
                setTactic(b, Tactic::Engage, now);
            }
        }
        if (b.tacticPoint.x < 1e8f) {
            float d = distTo(b.tacticPoint);
            bool sprint = b.tactic == Tactic::Withdraw || (b.tactic == Tactic::Flank && d > 10.0f);
            if (d > 1.5f) {
                b.allowFinal = false;
                moveTo3(b, b.tacticPoint, sim, sprint, 1.2f);
                if (b.tactic == Tactic::Search && glm::length(xz(c.position - me.position)) < 18.0f) crouch = true;
            } else if (b.tactic == Tactic::Search) {
                c.awareness = std::min(c.awareness, 0.3f); // nobody here any more
                setTactic(b, Tactic::Engage, now);
            } else {
                b.hasCover = false;
                setTactic(b, Tactic::Engage, now);
            }
        }
    } else {
        // Engage / push: fight from cover, never stand about in the open.
        bool wantCloser = b.tactic == Tactic::Push;
        if (b.hasCover) {
            toCover = distTo(b.peeking && b.coverIsWindow ? coverPos() : b.tacticPoint.x < 1e8f ? b.tacticPoint : coverPos());
            if (wantCloser && toCover < 1.2f && now - b.coverTime > 2.0) b.hasCover = false; // next cover forward
        }
        if (!b.hasCover && now - b.coverTime > 0.6) {
            glm::vec3 spot;
            bool window = false;
            if (findCoverSpot(b, sim, c.position, wantCloser, b.peeks >= b.peekLimit || wantCloser, spot, window)) {
                b.hasCover = true;
                b.cover = xz(spot);
                b.coverY = spot.y;
                b.coverIsWindow = window;
                b.coverTime = now;
                b.peeking = false;
                b.peeks = 0;
                b.peekLimit = 2 + int(u01(b.rng) * 3.0f);
                b.phaseUntil = now + 0.5 + u01(b.rng);
                b.wantPath = true;
            } else {
                b.coverTime = now;
                if (wantCloser && dist < 18.0f) {
                    moveTo(b, xz(c.position), sim, false, 4.0f); // close enough: just go
                } else if (visible && b.tacticPoint.x > 1e8f) {
                    // Nothing to hide behind here: break line of sight.
                    glm::vec3 p;
                    if (findHiddenSpot(b, sim, {c.position}, 5.0f, 22.0f, p)) {
                        b.tacticPoint = p;
                        b.hiddenRoute = true;
                        b.wantPath = true;
                    }
                }
            }
        }
        if (b.hasCover) {
            glm::vec3 hide = b.tacticPoint.x < 1e8f ? b.tacticPoint : coverPos();
            glm::vec3 at = b.peeking && b.coverIsWindow ? coverPos() : (b.coverIsWindow ? hide : coverPos());
            toCover = distTo(at);
            if (toCover > 0.8f) {
                moveTo3(b, at, sim, toCover > 4.0f, 0.4f);
                if (toCover > 40.0f) b.hasCover = false;
            } else {
                b.input.move = glm::vec2(0.0f);
                if (!b.peeking) {
                    crouch = true;
                    // Flanked? This spot no longer hides us from them.
                    if (now > b.flankCheck) {
                        b.flankCheck = now + 0.5;
                        glm::vec3 base = me.position;
                        if (lineOfSight(sim, base + glm::vec3(0, 1.15f, 0), c.position, me.id, c.id) && now - b.coverTime > 1.0) {
                            b.lastCover = b.cover;
                            b.hasCover = false;
                            b.peeks = b.peekLimit;
                        }
                    }
                    if (b.hasCover && now > b.phaseUntil && !me.weapon.isReloading() && now - b.lastDamaged > 1.0) {
                        // Find a way to see out: stand up, or lean round the edge (at a window: step into it).
                        glm::vec3 base = me.position;
                        glm::vec3 right = me.right();
                        b.peekStand = b.coverIsWindow || lineOfSight(sim, base + glm::vec3(0, 1.66f, 0), c.position, me.id, c.id);
                        b.peekLean = 0.0f;
                        if (!b.peekStand) {
                            for (float side : {1.0f, -1.0f})
                                if (lineOfSight(sim, base + glm::vec3(0, 1.55f, 0) + right * (0.45f * side), c.position, me.id, c.id)) {
                                    b.peekLean = side;
                                    break;
                                }
                            if (b.peekLean == 0.0f) b.peekStand = true;
                        }
                        b.peeking = true;
                        ++b.peeks;
                        b.phaseUntil = now + 1.5 + u01(b.rng) * (1.0 + 2.0 * b.aggression);
                    }
                } else {
                    crouch = !b.peekStand;
                    lean = b.peekLean;
                    if (now > b.phaseUntil || now - b.lastDamaged < 0.3) {
                        b.peeking = false;
                        b.phaseUntil = now + 0.8 + u01(b.rng) * 2.0 * b.caution;
                        if (b.peeks >= b.peekLimit) {
                            b.lastCover = b.cover; // reposition: somewhere else next
                            b.hasCover = false;
                        }
                    }
                }
            }
        } else if (b.tacticPoint.x < 1e8f) {
            float d = distTo(b.tacticPoint);
            if (d > 1.0f) moveTo3(b, b.tacticPoint, sim, true, 0.8f);
            else b.tacticPoint = glm::vec3(1e9f);
            crouch = d < 1.0f;
        } else if (visible) {
            // Last resort: low and moving, never standing still in the open.
            if (now > b.strafeUntil) {
                b.strafeSide = u01(b.rng) < 0.5f ? -1.0f : 1.0f;
                b.strafeUntil = now + 0.8 + 1.5 * u01(b.rng);
            }
            glm::vec2 toT = glm::normalize(xz(c.position - me.position) + glm::vec2(1e-4f));
            steer(b, sim, glm::vec2(-toT.y, toT.x) * b.strafeSide - (outmatched ? toT * 0.6f : glm::vec2(0.0f)), false);
            crouch = dist > 25.0f;
        } else {
            float d = glm::length(xz(c.position - me.position));
            if (d > 3.0f) moveTo(b, xz(c.position), sim, false, 2.0f);
        }
    }
    b.input.crouch = crouch;
    b.input.lean = lean;

    // Stats: standing in the open with them in view.
    bool inCover = b.hasCover && toCover < 1.2f;
    bool exposed = visible && !inCover && lineOfSight(sim, t.eye(), me.chest(), c.id, me.id);
    if (exposed) {
        if (b.exposedSince < 0.0) b.exposedSince = now;
        b.maxExposed = std::max(b.maxExposed, float(now - b.exposedSince));
    } else {
        b.exposedSince = -1.0;
    }
    manageLight(b, sim, true, dist);

    // Getting to cover (or out of sight): run there looking where you're going; only shoot on the
    // move if they're close.
    bool relocating = (b.hasCover && toCover > 2.5f) || (b.tactic != Tactic::Engage && b.tactic != Tactic::Push && b.tacticPoint.x < 1e8f) ||
                      (!b.hasCover && b.tacticPoint.x < 1e8f);
    if (relocating && dist > 15.0f && !(b.tactic == Tactic::Search && dist < 25.0f)) {
        glm::vec2 dir = b.pathIndex < b.path.size() ? xz(b.path[b.pathIndex]) - xz(me.position) : glm::vec2(0.0f);
        if (glm::length(dir) > 0.1f) {
            float want = std::atan2(-dir.x, -dir.y);
            lookToward(b, want, 6.0f);
            b.wantPitch = 0.0f;
            // move input is relative to the look: re-steer now that the look follows the path
            steer(b, sim, dir, b.tactic != Tactic::Search && !crouch);
            b.input.crouch = crouch && b.tactic == Tactic::Search;
        }
        b.input.weapon.adsHeld = false;
        b.burst = 0;
        b.reaction = std::max(b.reaction, 0.15f); // turning back takes a moment
        return;
    }

    // ----- aiming
    b.reaction -= dt;
    glm::vec3 aimAt = visible ? t.chest() : c.position;
    if (visible && dist < 25.0f && u01(b.rng) < b.skill * 0.02f) aimAt = t.head();
    if (visible) {
        float v = wd.projectile.muzzleVelocity;
        float tof = dist / std::max(v * 0.85f, 100.0f);
        aimAt += t.velocity * tof * b.skill;
        float tz = me.weapon.zeroRange() / v;
        float drop = 0.5f * 9.81f * (tof * tof - tz * tz);
        aimAt.y += drop * b.skill;
    }
    float sigma = 0.004f + 0.02f * (1.0f - b.skill) + me.suppression * 0.02f + (me.horizontalSpeed() > 0.5f ? 0.01f : 0.0f) +
                  (b.reaction > 0.0f ? 0.03f : 0.0f) + (now < b.dazzledUntil ? 0.03f : 0.0f);
    if (me.weapon.laserOn() && !me.weapon.isAiming()) sigma *= 0.6f; // the dot shows where the gun points
    std::normal_distribution<float> n(0.0f, 1.0f);
    const float tau = 0.5f;
    b.aimError += -b.aimError * (dt / tau) + glm::vec2(n(b.rng), n(b.rng)) * sigma * std::sqrt(2.0f * dt / tau);
    glm::vec3 to = aimAt - eye;
    float flat = std::sqrt(to.x * to.x + to.z * to.z);
    b.wantYaw = yawOf(to) + b.aimError.x;
    b.wantPitch = std::atan2(to.y, std::max(flat, 0.01f)) + b.aimError.y;
    b.turnRate = 2.5f + 5.0f * b.skill;
    b.lookStiffness = 14.0f + 10.0f * b.skill;

    bool optic = me.weapon.optic().type != OpticType::Iron;
    bool wantAds = (visible || b.peeking) && (dist > 12.0f || optic) && !me.weapon.isReloading() && !b.input.sprint;
    if (wantAds) b.adsUntil = now + 0.8;
    b.input.weapon.adsHeld = !me.weapon.isReloading() && (wantAds || now < b.adsUntil) && !b.input.sprint;
    if (wd.canAuto && me.weapon.item()) {
        bool wantAuto = dist < 35.0f;
        if (wantAuto != me.weapon.isFullAuto()) b.input.weapon.toggleFireMode = true;
    }

    // ----- trigger
    if (!visible || b.reaction > 0.0f || !me.weapon.hasWeapon()) {
        if (!visible && b.aggression > 0.75f && now - c.lastSeen < 2.5 && now >= b.nextShot && me.weapon.ammo() > 5 && inCover) {
            b.input.weapon.firePressed = true; // suppressing where they were
            b.nextShot = now + 0.5 + u01(b.rng) * 0.5;
            ++sim.stats.botShots;
            ++sim.stats.botCoveredShots;
        }
        return;
    }
    glm::vec3 bore = me.weapon.boreDirection();
    glm::vec3 want = glm::normalize(aimAt - me.weapon.muzzle());
    float err = std::acos(std::clamp(glm::dot(bore, want), -1.0f, 1.0f));
    float tolerance = std::max(0.003f, 0.22f / std::max(dist, 1.0f)) * (1.6f - b.skill);
    if (dist > range * 1.3f) return;
    if (err < tolerance && now >= b.nextShot) {
        bool fired = false;
        if (me.weapon.isFullAuto()) {
            b.input.weapon.fireHeld = true;
            b.input.weapon.firePressed = true;
            fired = true;
            if (++b.burst > int(40 + 60 * u01(b.rng))) {
                b.burst = 0;
                b.nextShot = now + 0.25 + 0.3 * u01(b.rng);
            }
        } else {
            b.input.weapon.firePressed = true;
            fired = true;
            float base = dist < 20.0f ? 0.18f : dist < 80.0f ? 0.32f : 0.7f;
            b.nextShot = now + base * (1.4f - 0.6f * b.skill) * (0.8f + 0.4f * u01(b.rng));
        }
        if (fired && b.burst <= 1) {
            ++sim.stats.botShots;
            if (inCover) ++sim.stats.botCoveredShots;
        }
    } else {
        b.burst = 0;
    }
}

void BotManager::heal(Bot& b, Simulation& sim) {
    Character& me = *sim.chars[b.id];
    const double now = sim.time;
    b.input.crouch = true;
    b.input.move = glm::vec2(0.0f);
    if (me.action() == Character::Action::Med) return;
    // With enemies about, get somewhere none of them can see before stopping to treat wounds.
    std::vector<glm::vec3> threats = knownThreats(b, sim, 60.0f, 20.0);
    bool hidden = true;
    for (const glm::vec3& t : threats)
        if (lineOfSight(sim, me.position + glm::vec3(0, 1.15f, 0), t, me.id, -2)) hidden = false;
    if (!hidden && !(me.health.heavyBleeding() && me.health.totalHp() < me.health.totalMax() * 0.3f)) {
        if (!b.healSpotSet && findHiddenSpot(b, sim, threats, 4.0f, 30.0f, b.healSpot)) {
            b.healSpotSet = true;
            b.healSince = now;
            b.hiddenRoute = true;
            b.wantPath = true;
        }
        if (b.healSpotSet && now - b.healSince < 15.0 &&
            glm::length(xz(b.healSpot - me.position)) + (std::abs(b.healSpot.y - me.position.y) > 1.6f ? 6.0f : 0.0f) > 1.0f) {
            b.input.crouch = false;
            moveTo3(b, b.healSpot, sim, true, 0.8f);
            glm::vec2 dir = b.pathIndex < b.path.size() ? xz(b.path[b.pathIndex]) - xz(me.position) : xz(b.healSpot - me.position);
            if (glm::length(dir) > 0.1f) lookToward(b, std::atan2(-dir.x, -dir.y), 5.0f);
            return;
        }
    }
    bool counted = hidden && !threats.empty();
    // Worst problem first.
    MedKind order[] = {MedKind::Tourniquet, MedKind::Hemostatic, MedKind::Bandage, MedKind::Splint, MedKind::FieldKit,
                       MedKind::Painkillers, MedKind::Surgical};
    for (MedKind k : order) {
        if (k == MedKind::Surgical && sim.time - b.lastDamaged < 20.0) continue;
        int zone = me.health.bestZoneFor(k);
        if (zone < 0) continue;
        for (const Grid* g : me.equip.grids())
            for (const Grid::Entry& e : g->items)
                if (e.item->d().cat == ItemCat::Med && e.item->d().med == k) {
                    b.input.useMed = e.item->uid;
                    b.input.useMedZone = zone;
                    if (counted) ++sim.stats.botHealsHidden;
                    return;
                }
    }
    b.nextThink = sim.time; // nothing usable: decide again
    b.goal = Goal::Roam;
}

void BotManager::takeLoot(Bot& b, Simulation& sim) {
    Character& me = *sim.chars[b.id];
    Grid* grid = b.lootType == 1 ? sim.containerGrid(b.lootIndex) : nullptr;
    Equipment* body = b.lootType == 2 ? sim.bodyEquipment(b.lootIndex) : nullptr;
    if (!grid && !body) return;
    ++sim.stats.botLoots;

    // Everything on offer, best first.
    struct Offer {
        uint32_t uid;
        int value;
        ItemCat cat;
    };
    std::vector<Offer> offers;
    auto list = [&](const Item& it) { offers.push_back({it.uid, it.totalValue(), it.d().cat}); };
    if (grid)
        for (const Grid::Entry& e : grid->items) list(*e.item);
    if (body) {
        for (const ItemPtr& s : body->slots)
            if (s) list(*s);
        for (Grid* g : body->grids())
            for (const Grid::Entry& e : g->items) list(*e.item);
    }
    std::sort(offers.begin(), offers.end(), [](const Offer& a, const Offer& c) { return a.value > c.value; });

    auto take = [&](uint32_t uid) -> ItemPtr {
        if (grid) {
            for (int i = 0; i < int(grid->items.size()); ++i)
                if (grid->items[i].item->uid == uid) return grid->take(i);
            return nullptr;
        }
        return body->remove(uid);
    };
    auto giveBack = [&](ItemPtr it) {
        if (!it) return;
        if (grid && grid->autoPlace(it)) return;
        if (body && body->stow(it)) return;
        sim.dropFromCharacter(me, std::move(it));
    };
    auto peekItem = [&](uint32_t uid) -> const Item* {
        if (grid) {
            for (const Grid::Entry& e : grid->items)
                if (e.item->uid == uid) return e.item.get();
            return nullptr;
        }
        return body->find(uid);
    };
    auto usable = [&](const ItemDef& d) {
        for (int s : {ES_Hand0, ES_Hand1}) {
            const Item* w = me.equip.slots[s].get();
            if (!w) continue;
            const WeaponDef& wd = weaponDefs()[w->d().weapon];
            if (d.cat == ItemCat::Magazine && magazineFits(d, w->d().weapon)) return true;
            if (d.cat == ItemCat::Ammo && d.caliber == wd.caliber) return true;
        }
        return false;
    };

    for (const Offer& o : offers) {
        switch (o.cat) {
        case ItemCat::Weapon: {
            ItemPtr it = take(o.uid);
            if (!it) break;
            Item* hand = me.equip.slots[ES_Hand0].get();
            if (!hand) {
                me.equip.slots[ES_Hand0] = std::move(it);
                ++sim.stats.botWeaponsTaken;
            } else if (weaponScore(*it, me.equip) > weaponScore(*hand, me.equip) * 1.25f) {
                // Better gun: it goes in the hands, the old one on the back (or stays behind).
                ItemPtr old = std::move(me.equip.slots[ES_Hand0]);
                me.equip.slots[ES_Hand0] = std::move(it);
                ++sim.stats.botWeaponsTaken;
                if (!me.equip.slots[ES_Back0]) me.equip.slots[ES_Back0] = std::move(old);
                else if (!me.equip.slots[ES_Back1]) me.equip.slots[ES_Back1] = std::move(old);
                else giveBack(std::move(old));
            } else if (o.value >= 600 && (!me.equip.slots[ES_Back0] || !me.equip.slots[ES_Back1])) {
                me.equip.slots[!me.equip.slots[ES_Back0] ? ES_Back0 : ES_Back1] = std::move(it);
                ++sim.stats.botWeaponsTaken;
            } else {
                giveBack(std::move(it));
            }
            break;
        }
        case ItemCat::Rig:
        case ItemCat::Backpack: {
            int slot = o.cat == ItemCat::Rig ? ES_Rig : ES_Backpack;
            const Item* offer = peekItem(o.uid);
            if (!offer) break;
            if (me.equip.slots[slot]) {
                // Bigger: swap, moving everything across.
                const ItemDef& cur = me.equip.slots[slot]->d();
                if (offer->d().gridW * offer->d().gridH <= cur.gridW * cur.gridH) break;
                ItemPtr it = take(o.uid);
                if (!it || !it->grid) break;
                ItemPtr old = std::move(me.equip.slots[slot]);
                for (Grid::Entry& e : it->grid->items) giveBack(std::move(e.item)); // whatever was in the new one stays here
                it->grid->items.clear();
                for (Grid::Entry& e : old->grid->items)
                    if (!it->grid->autoPlace(e.item)) giveBack(std::move(e.item));
                old->grid->items.clear();
                me.equip.slots[slot] = std::move(it);
                giveBack(std::move(old));
                ++sim.stats.botGearSwaps;
                break;
            }
            ItemPtr it = take(o.uid);
            if (it) me.equip.slots[slot] = std::move(it);
            break;
        }
        case ItemCat::Armor:
        case ItemCat::Helmet: {
            int slot = o.cat == ItemCat::Armor ? ES_Armor : ES_Helmet;
            const Item* offer = peekItem(o.uid);
            if (!offer) break;
            auto rating = [](const Item& a) { return a.d().armorClass * std::clamp(a.condition / std::max(1.0f, a.d().durability), 0.0f, 1.0f); };
            float cur = me.equip.slots[slot] ? rating(*me.equip.slots[slot]) : 0.0f;
            if (rating(*offer) <= cur + 0.3f) break;
            ItemPtr it = take(o.uid);
            if (!it) break;
            ItemPtr old = std::move(me.equip.slots[slot]);
            me.equip.slots[slot] = std::move(it);
            giveBack(std::move(old));
            ++sim.stats.botGearSwaps;
            break;
        }
        case ItemCat::Attachment: {
            const Item* offer = peekItem(o.uid);
            if (!offer) break;
            int a = offer->d().attach;
            const AttachmentDef& ad = attachmentDefs()[a];
            for (int s : {ES_Hand0, ES_Hand1}) {
                Item* gun = me.equip.slots[s].get();
                if (!gun || gun->d().cat != ItemCat::Weapon || !attachmentFits(weaponDefs()[gun->d().weapon], a, gun)) continue;
                int cur = gun->att[ad.slot];
                if (cur >= 0 && attachmentDefs()[cur].value * 1.2f >= ad.value) continue;
                ItemPtr it = take(o.uid);
                if (!it) break;
                gun->att[ad.slot] = a;
                gun->attachmentsChanged();
                if (cur >= 0) giveBack(makeItem(attachmentItemDef(cur)));
                ++sim.stats.botAttachmentsFitted;
                break;
            }
            break;
        }
        case ItemCat::Optic: {
            const Item* offer = peekItem(o.uid);
            Item* gun = me.equip.slots[ES_Hand0].get();
            if (!offer || !gun || gun->d().cat != ItemCat::Weapon) break;
            int op = offer->d().optic;
            if (!opticFits(weaponDefs()[gun->d().weapon], op)) break;
            int curItem = gun->optic > 0 ? opticItemDef(gun->optic) : -1;
            if (curItem >= 0 && itemDefs()[curItem].value * 1.2f >= offer->d().value) break;
            ItemPtr it = take(o.uid);
            if (!it) break;
            int old = gun->optic;
            gun->optic = op;
            gun->magnification = opticDefs()[op].minMag;
            gun->altSight = false;
            if (old > 0) giveBack(makeItem(opticItemDef(old)));
            ++sim.stats.botAttachmentsFitted;
            break;
        }
        case ItemCat::Med:
        case ItemCat::Magazine:
        case ItemCat::Ammo: {
            Item* peek = grid ? nullptr : body->find(o.uid);
            const ItemDef* d = nullptr;
            if (peek) d = &peek->d();
            else if (grid)
                for (const Grid::Entry& e : grid->items)
                    if (e.item->uid == o.uid) d = &e.item->d();
            if (!d) break;
            if (o.cat != ItemCat::Med && !usable(*d)) break;
            ItemPtr it = take(o.uid);
            if (!me.equip.stow(it)) giveBack(std::move(it));
            break;
        }
        default: break;
        }
    }
    // Keep the guns fed: a usable weapon in the hands.
    if (!me.equip.slots[ES_Hand0] && me.equip.slots[ES_Back0]) me.equip.slots[ES_Hand0] = std::move(me.equip.slots[ES_Back0]);
}

void BotManager::loot(Bot& b, float dt, Simulation& sim) {
    Character& me = *sim.chars[b.id];
    if (b.lootType == 0 && !chooseLootTarget(b, sim)) {
        b.goal = Goal::Roam;
        return;
    }
    glm::vec3 target;
    Interaction it;
    if (b.lootType == 1) {
        if (b.lootIndex >= int(sim.loot.containers.size())) {
            b.lootType = 0;
            return;
        }
        target = sim.loot.containers[b.lootIndex].position;
        it = {Interaction::Container, b.lootIndex};
    } else if (b.lootType == 2) {
        if (b.lootIndex >= int(sim.chars.size()) || !sim.chars[b.lootIndex]->active || !sim.chars[b.lootIndex]->dead) {
            b.lootType = 0;
            return;
        }
        target = sim.chars[b.lootIndex]->chest();
        it = {Interaction::Body, b.lootIndex};
    } else {
        if (b.lootIndex >= int(sim.loot.items.size())) {
            b.lootType = 0;
            return;
        }
        target = sim.loot.items[b.lootIndex].position;
        it = {Interaction::Item, b.lootIndex};
    }
    float d = glm::length(xz(target - me.position)) + (std::abs(target.y - me.position.y) > 1.6f ? 6.0f : 0.0f);
    if (sim.time - b.goalSince > 90.0) {
        // Taking too long: forget it.
        if (b.lootType == 1) b.doneContainers.push_back(b.lootIndex);
        if (b.lootType == 2) b.doneBodies.push_back(b.lootIndex);
        b.lootType = 0;
        b.goalSince = sim.time;
        return;
    }
    if (b.lootType == 2 && b.watchedBody != b.lootIndex) {
        bool mine = std::find(b.myKills.begin(), b.myKills.end(), b.lootIndex) != b.myKills.end();
        if (b.watchingBody != b.lootIndex && d < 30.0f) {
            b.watchingBody = b.lootIndex;
            b.watchStart = sim.time;
            b.watchUntil = sim.time + (mine ? 1.5 : 4.0 + 6.0 * b.caution);
            ++sim.stats.botBodyWatches;
        }
        if (b.watchingBody == b.lootIndex) {
            // Hold back, low, gun up, and watch the body and what's round it.
            b.input.crouch = true;
            b.input.move = glm::vec2(0.0f);
            b.input.weapon.adsHeld = true;
            glm::vec3 to = target - me.eye();
            if (sim.time > b.scanTime) {
                b.scanYaw = u11(b.rng) * 0.6f;
                b.scanTime = sim.time + 1.0 + u01(b.rng);
            }
            lookToward(b, yawOf(to) + b.scanYaw, 2.0f);
            b.wantPitch = std::atan2(to.y, std::max(0.1f, glm::length(xz(to))));
            for (const Noise& n : b.noises)
                if (n.time > b.watchStart && n.importance >= 0.3f && n.owner != b.id && glm::length(n.position - target) < 35.0f) {
                    b.deferredBodies.push_back({b.lootIndex, sim.time}); // someone's there: not now
                    b.watchingBody = -1;
                    b.lootType = 0;
                    return;
                }
            if (sim.time >= b.watchUntil) {
                b.watchedBody = b.lootIndex;
                b.watchingBody = -1;
            }
            return;
        }
    }
    if (d > 1.9f) {
        b.allowFinal = true;
        bool careful = b.lootType == 2 && d < 14.0f;
        moveTo3(b, target, sim, d > 25.0f && !careful, 1.2f);
        if (careful) {
            b.input.crouch = true;
            b.input.weapon.adsHeld = true;
        }
        b.allowFinal = false;
        glm::vec2 dir = b.pathIndex < b.path.size() ? xz(b.path[b.pathIndex]) - xz(me.position) : xz(target - me.position);
        if (glm::length(dir) > 0.1f) {
            float want = std::atan2(-dir.x, -dir.y);
            lookToward(b, want, 4.0f);
        }
        b.wantPitch = -0.1f;
        return;
    }
    // Look down at it and search.
    glm::vec3 to = target - me.eye();
    lookToward(b, yawOf(to), 4.0f);
    b.wantPitch = std::atan2(to.y, std::max(0.1f, glm::length(xz(to))));
    b.input.crouch = true;
    if (b.lootType == 3) {
        int idx = b.lootIndex;
        b.lootType = 0;
        if (sim.pickUp(me, idx)) ++sim.stats.botWeaponsTaken;
        return;
    }
    if (sim.interact(me, it, true, dt)) {
        if (b.lootType == 1 && sim.loot.containers[b.lootIndex].lootZone) ++b.zoneLoots;
        takeLoot(b, sim);
        b.pauseUntil = sim.time + 0.8 + u01(b.rng);
        if (b.lootType == 1) b.doneContainers.push_back(b.lootIndex);
        if (b.lootType == 2) b.doneBodies.push_back(b.lootIndex);
        b.lootType = 0;
        b.goalSince = sim.time;
    }
}

void BotManager::extractGoal(Bot& b, float dt, Simulation& sim) {
    Character& me = *sim.chars[b.id];
    if (sim.map.extracts.empty()) {
        b.goal = Goal::Roam;
        return;
    }
    if (b.extract < 0 || b.extract >= int(sim.map.extracts.size())) {
        float best = 1e9f;
        for (int i = 0; i < int(sim.map.extracts.size()); ++i) {
            float d = glm::length(xz(sim.map.extracts[i].position - me.position));
            // Avoid extracts where shooting was heard recently.
            for (const Noise& n : b.noises)
                if (n.importance >= 1.0f && glm::length(n.position - sim.map.extracts[i].position) < 60.0f) d += 250.0f;
            if (d < best) {
                best = d;
                b.extract = i;
            }
        }
    }
    const ExtractPoint& e = sim.map.extracts[b.extract];
    bool sendBack = b.goal == Goal::SendBack;
    glm::vec3 target = sendBack ? e.dropBox : e.position;
    float d = glm::length(xz(target - me.position));
    float reach = sendBack ? 1.6f : e.radius - 2.0f;
    if (d > reach) {
        moveTo(b, xz(target), sim, d > 20.0f, reach * 0.6f);
        glm::vec2 dir = b.pathIndex < b.path.size() ? xz(b.path[b.pathIndex]) - xz(me.position) : xz(target - me.position);
        if (glm::length(dir) > 0.1f) lookToward(b, std::atan2(-dir.x, -dir.y), 4.0f);
        b.wantPitch = 0.0f;
        return;
    }
    // Waiting: keep watch.
    b.input.crouch = true;
    if (sim.time > b.scanTime) {
        b.scanYaw = b.yaw + u11(b.rng) * 2.0f;
        b.scanTime = sim.time + 1.5 + u01(b.rng) * 2.0;
    }
    lookToward(b, b.scanYaw, 1.5f);
    Interaction i{sendBack ? Interaction::SendBack : Interaction::Extract, b.extract};
    if (sim.interact(me, i, true, dt) && sendBack) {
        b.sentBack = true;
        b.goal = Goal::Roam;
        b.nextThink = sim.time;
    }
}

void BotManager::act(Bot& b, float dt, Simulation& sim) {
    Character& me = *sim.chars[b.id];
    const double now = sim.time;
    CharacterInput prev = b.input;
    b.input = CharacterInput{};
    b.debug.clear();

    switch (b.goal) {
    case Goal::Fight: {
        Contact* c = nullptr;
        for (Contact& k : b.contacts)
            if (k.id == b.target) c = &k;
        if (c && c->id < int(sim.chars.size()) && sim.chars[c->id]->alive()) fight(b, dt, sim, *c);
        else b.nextThink = now;
        break;
    }
    case Goal::Flee: {
        Contact* c = bestThreat(b, sim);
        glm::vec2 away = c ? xz(me.position - c->position) : glm::vec2(1, 0);
        float l = glm::length(away);
        away = l > 1e-3f ? away / l : glm::vec2(1, 0);
        if (now > b.phaseUntil || glm::length(b.fleeTo - xz(me.position)) < 4.0f) {
            glm::vec2 p = xz(me.position);
            float lim = sim.map.halfSize > 0.0f ? sim.map.halfSize - 40.0f : 1e9f;
            glm::vec2 dest = p + away * 40.0f;
            for (float turn : {0.0f, 1.2f, -1.2f, 2.0f, -2.0f}) {
                glm::vec2 a(away.x * std::cos(turn) - away.y * std::sin(turn), away.x * std::sin(turn) + away.y * std::cos(turn));
                glm::vec2 q = p + a * 40.0f;
                if (std::abs(q.x) < lim && std::abs(q.y) < lim) {
                    dest = q;
                    break;
                }
            }
            b.fleeTo = glm::clamp(dest, glm::vec2(-lim), glm::vec2(lim));
            b.phaseUntil = now + 6.0;
        }
        moveTo(b, b.fleeTo, sim, true, 3.0f);
        glm::vec2 dir = b.pathIndex < b.path.size() ? xz(b.path[b.pathIndex]) - xz(me.position) : away;
        lookToward(b, std::atan2(-dir.x, -dir.y), 5.0f);
        if (me.health.bleeding() && hasMeds(me) && (!c || !c->visible)) heal(b, sim);
        break;
    }
    case Goal::Heal: heal(b, sim); break;
    case Goal::Loot: loot(b, dt, sim); break;
    case Goal::Extract:
    case Goal::SendBack: extractGoal(b, dt, sim); break;
    case Goal::Hunt:
    case Goal::Roam: {
        glm::vec2 dest;
        bool sprint = true;
        if (b.goal == Goal::Hunt) {
            const Noise* lure = nullptr;
            for (const Noise& n : b.noises)
                if (n.importance >= 0.8f && now - n.time < 40.0) lure = &n;
            if (!lure) b.goal = Goal::Roam;
        }
        if (b.goal == Goal::Hunt) {
            const Noise* lure = nullptr;
            for (const Noise& n : b.noises)
                if (n.importance >= 0.8f && now - n.time < 40.0) lure = &n;
            dest = xz(lure->position);
            float d = glm::length(dest - xz(me.position));
            // Into someone else's fight: fast until near, then quiet, then wait for an opening.
            sprint = d > 90.0f;
            bool quiet = d < 70.0f;
            if (lure->time != b.huntNoise) {
                b.huntNoise = lure->time;
                if (d < 45.0f && b.huntHoldUntil < now - 20.0) b.huntHoldUntil = now + 4.0 + 6.0 * b.caution;
            }
            b.input.weapon.adsHeld = d < 45.0f;
            if (quiet) b.input.crouch = d < 35.0f;
            if (now < b.huntHoldUntil) {
                b.input.move = glm::vec2(0.0f);
                b.input.crouch = true;
                lookToward(b, std::atan2(-(dest.x - me.position.x), -(dest.y - me.position.z)), 2.0f);
                b.wantPitch = 0.0f;
                break;
            }
            if (d < 6.0f) {
                b.noises.clear();
                b.goal = Goal::Roam;
            }
        } else {
            if (b.roamTarget.x > 1e8f || glm::length(b.roamTarget - xz(me.position)) < 8.0f) {
                if (b.roamTarget.x < 1e8f) {
                    b.pauseUntil = now + 2.0 + 2.0 * u01(b.rng); // arrived: stop and look around
                    for (int i = 0; i < int(sim.map.hotspots.size()); ++i)
                        if (glm::length(xz(sim.map.hotspots[i].position) - b.roamTarget) < 10.0f) b.doneHotspots.push_back(i);
                }
                // Where next: hotspots, weighted by kit (the loot zone draws the well armed; the poorly
                // armed either slip in and out, or wait by the way out for people leaving it).
                const LootZone& zone = sim.map.lootZone;
                if (b.ambusher && b.ambushUntil < -1e8 && zone.radius > 0.0f && !sim.map.extracts.empty()) {
                    const ExtractPoint* ex = &sim.map.extracts[0];
                    for (const ExtractPoint& e : sim.map.extracts)
                        if (glm::length(xz(e.position) - zone.center) < glm::length(xz(ex->position) - zone.center)) ex = &e;
                    glm::vec2 dir = glm::normalize(xz(ex->position) - zone.center + glm::vec2(1e-3f));
                    b.roamTarget = zone.center + dir * (zone.radius + 25.0f);
                    b.ambushUntil = -1.0; // armed: set on arrival
                } else {
                    if (b.ambushUntil == -1.0 && b.roamTarget.x < 1e8f) {
                        b.ambushUntil = now + 60.0 + 60.0 * u01(b.rng);
                        b.pauseUntil = b.ambushUntil;
                    }
                    std::vector<int> options;
                    std::vector<float> weights;
                    float total = 0.0f;
                    for (int i = 0; i < int(sim.map.hotspots.size()); ++i) {
                        if (std::find(b.doneHotspots.begin(), b.doneHotspots.end(), i) != b.doneHotspots.end()) continue;
                        float w = 1.0f;
                        if (sim.map.hotspots[i].lootZone) w = b.power > 0.55f ? 3.5f : b.power < 0.35f ? 0.6f : 1.5f;
                        options.push_back(i);
                        weights.push_back(w);
                        total += w;
                    }
                    if (!options.empty() && u01(b.rng) < 0.7f) {
                        float r = u01(b.rng) * total;
                        size_t k = 0;
                        while (k + 1 < options.size() && (r -= weights[k]) > 0.0f) ++k;
                        b.roamTarget = xz(sim.map.hotspots[options[k]].position);
                    } else {
                        b.roamTarget = glm::vec2(u11(b.rng), u11(b.rng)) * (sim.map.halfSize > 0.0f ? sim.map.halfSize * 0.8f : 30.0f);
                    }
                }
                b.lootType = 0;
            }
            dest = b.roamTarget;
            // Poorly armed: no running about near the places people gather.
            if (b.power < 0.35f)
                for (const Hotspot& h : sim.map.hotspots)
                    if (glm::length(xz(h.position - me.position)) < 50.0f) sprint = false;
            // While roaming, opportunistically loot.
            if (sim.time > b.nextThink - 0.01 && chooseLootTarget(b, sim)) {
                b.goal = Goal::Loot;
                b.goalSince = now;
            }
        }
        moveTo(b, dest, sim, sprint, 2.0f);
        // Look where we're going, glancing around now and then.
        glm::vec2 dir = b.pathIndex < b.path.size() ? xz(b.path[b.pathIndex]) - xz(me.position) : dest - xz(me.position);
        float want = glm::length(dir) > 0.1f ? std::atan2(-dir.x, -dir.y) : b.yaw;
        if (now > b.scanTime) {
            b.scanYaw = u11(b.rng) * (b.input.sprint ? 0.2f : 0.9f);
            b.scanTime = now + 1.5 + 3.0 * u01(b.rng);
        }
        lookToward(b, want + b.scanYaw, 3.0f);
        b.wantPitch = -0.05f;
        if (!b.input.sprint) {
            // Moving and looking aside: the move input is relative to the look.
            if (std::abs(wrap(want - b.yaw)) > 0.3f) steer(b, sim, dir, false);
        }
        break;
    }
    }

    // Reload when it's quiet.
    if (b.goal != Goal::Fight && me.weapon.hasWeapon() && me.weapon.ammo() < me.weapon.capacity() * 0.6f && !me.weapon.isReloading() &&
        !me.weapon.isHandLoading() && now > b.goalSince + 1.0 && u01(b.rng) < 0.01f)
        b.input.weapon.reloadPressed = true;
    // Pick a weapon if empty-handed.
    if (!me.weapon.hasWeapon() && !me.weapon.isSwitching()) {
        int other = 1 - me.weapon.hand();
        if (me.equip.slots[other]) b.input.weapon.selectWeapon = other;
    }
    if (sim.time < b.pauseUntil && b.goal != Goal::Fight && b.goal != Goal::Flee) {
        // A pause to look and listen (an ambusher waits low, watching the way from the loot zone).
        b.input.move = glm::vec2(0.0f);
        b.input.sprint = false;
        bool ambush = now < b.ambushUntil && sim.map.lootZone.radius > 0.0f;
        if (ambush) b.input.crouch = true;
        if (sim.time > b.scanTime) {
            float toward = ambush ? std::atan2(-(sim.map.lootZone.center.x - me.position.x), -(sim.map.lootZone.center.y - me.position.z)) : b.yaw;
            b.scanYaw = toward + u11(b.rng) * (ambush ? 0.6f : 1.6f);
            b.scanTime = sim.time + 0.8 + 1.2 * u01(b.rng);
        }
        lookToward(b, b.scanYaw, 1.6f);
        b.wantPitch = u11(b.rng) * 0.02f;
    }
    if (b.ambusher && b.ambushUntil > 0.0 && now > b.ambushUntil) b.ambusher = false; // done waiting: go have a look
    if (b.goal != Goal::Fight) manageLight(b, sim, false, 1e9f);
    applyLook(b, dt);
    // Feet don't change direction instantly either.
    float k = std::min(1.0f, dt * (b.goal == Goal::Fight ? 10.0f : 6.0f));
    b.moveSmooth += (b.input.move - b.moveSmooth) * k;
    b.input.move = b.moveSmooth;
    b.pitch = std::clamp(b.pitch, -1.2f, 1.2f);
    {
        char buf[96];
        std::snprintf(buf, sizeof(buf), " goal %d mv %.2f %.2f", int(b.goal), b.input.move.x, b.input.move.y);
        b.debug += buf;
    }
    b.input.yaw = b.yaw;
    b.input.pitch = b.pitch;
}

void BotManager::update(float dt, Simulation& sim) {
    pathBudget = 4;
    m_now = sim.time;
    m_updating = true;
    for (size_t bi = 0; bi < bots.size(); ++bi) {
        Bot& b = bots[bi];
        if (b.id < 0 || b.id >= int(sim.chars.size())) continue;
        Character& me = *sim.chars[b.id];
        if (!me.alive()) continue;
        if (b.yaw == 0.0f && b.pitch == 0.0f && sim.time - b.spawnTime < 0.05) b.yaw = b.wantYaw = me.yaw;
        if (sim.time >= b.nextPerceive) {
            perceive(b, sim);
            b.nextPerceive = sim.time + 0.1;
        }
        if (sim.time >= b.nextThink) {
            think(b, sim);
            b.nextThink = sim.time + 0.25;
        }
        act(b, dt, sim);
    }
    m_updating = false;
    for (int id : m_pendingRemove) remove(id);
    m_pendingRemove.clear();
}
