#include "ai/Bots.h"

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
        float rate = 4.0f * fov * distF * motion * stance * optics * flash * (close ? 4.0f : 1.0f);
        c.awareness = std::min(1.5f, c.awareness + rate * dt);
        if (c.awareness >= 1.0f) {
            if (!c.visible) b.reaction = std::max(b.reaction, 0.25f + 0.6f * (1.0f - b.skill));
            c.visible = true;
            c.lastSeen = now;
            c.position = o.chest();
            c.velocity = o.velocity;
            c.uncertainty = 0.0f;
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
    bool meds = hasMeds(me);
    int ammo = me.weapon.ammo() + spareRounds(me);
    Goal g;

    double sinceDamage = now - b.lastDamaged;
    bool treatable = me.health.bleeding() || me.health.anyFracture() || hp < 0.6f || me.health.pain() > 0.6f;
    if (me.health.heavyBleeding() && meds && (!visible || hp < 0.35f)) g = Goal::Heal;
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
        if (lure && b.aggression + 0.3f * b.greed > 0.6f) g = Goal::Hunt;
        else if (b.lootType != 0 || chooseLootTarget(b, sim)) g = Goal::Loot;
        else g = Goal::Roam;
    }
    if (g != b.goal) {
        b.goal = g;
        b.goalSince = now;
        b.wantPath = true;
        b.peeking = false;
        if (g != Goal::Fight && g != Goal::Heal) b.hasCover = false;
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
    Character& me = *sim.chars[b.id];
    glm::vec2 p = xz(me.position);
    float toDest = glm::length(dest - p);
    if (toDest < stopDist) {
        b.path.clear();
        b.debug = "arrived";
        return;
    }
    bool finished = b.pathIndex >= b.path.size();
    float moved = glm::length(dest - b.destination);
    if (moved > 4.0f || (moved > 1.0f && sim.time - b.pathTime > 1.5) || sim.time - b.pathTime > 20.0 || (finished && sim.time - b.pathTime > 1.5))
        b.wantPath = true;
    if (b.wantPath && pathBudget > 0) {
        --pathBudget;
        b.wantPath = false;
        b.destination = dest;
        b.pathTime = sim.time;
        b.pathIndex = 0;
        if (!sim.nav.valid() || !sim.nav.findPath(p, dest, b.path)) b.path = {dest};
    }
    while (b.pathIndex < b.path.size()) {
        float d = glm::length(b.path[b.pathIndex] - p);
        bool last = b.pathIndex + 1 >= b.path.size();
        // Corners (doorways) must actually be reached; skip only when the next leg is clear from here.
        if (d < 0.3f || (d < 1.5f && !last && sim.nav.clearLine(p, b.path[b.pathIndex + 1])) || (last && d < 0.7f)) ++b.pathIndex;
        else break;
    }
    glm::vec2 wp;
    if (b.pathIndex < b.path.size()) {
        wp = b.path[b.pathIndex];
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
    if (b.pathIndex + 1 < b.path.size() && glm::length(dir) < 2.5f && sim.nav.clearLine(p, b.path[b.pathIndex + 1])) {
        glm::vec2 next = b.path[b.pathIndex + 1] - p;
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
            b.destination = glm::vec2(1e9f);
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

    // Reloading: empty, or low and nobody in sight.
    bool reload = me.weapon.hasWeapon() && !me.weapon.isReloading() && !me.weapon.isHandLoading() &&
                  (me.weapon.ammo() == 0 || (!visible && me.weapon.ammo() < me.weapon.capacity() * 0.4f));
    if (reload) b.input.weapon.reloadPressed = true;
    // Out of ammunition for this gun: the other one.
    if (me.weapon.hasWeapon() && me.weapon.ammo() == 0 && spareRounds(me) == 0) {
        int other = 1 - me.weapon.hand();
        if (me.equip.slots[other]) b.input.weapon.selectWeapon = other;
    }
    if (!me.weapon.hasWeapon()) {
        int other = 1 - me.weapon.hand();
        if (me.equip.slots[other]) b.input.weapon.selectWeapon = other;
    }

    // ----- positioning
    bool wantCover = (underFire || me.weapon.isReloading() || reload || dist < range * 0.9f) && b.caution + (underFire ? 0.5f : 0.0f) > 0.35f;
    if (wantCover && !b.hasCover && now - b.coverTime > 2.0) {
        std::vector<NavGrid::Cover> covers;
        sim.nav.findCover(xz(me.position), xz(c.position), 22.0f, covers, 12);
        for (const NavGrid::Cover& cv : covers) {
            float gy = sim.terrain.height(cv.position.x, cv.position.y);
            if (sim.map.hasTerrain == false) gy = me.position.y;
            glm::vec3 low(cv.position.x, gy + 1.0f, cv.position.y);
            if (lineOfSight(sim, low, c.position, me.id, c.id)) continue; // not actually covered
            b.hasCover = true;
            b.cover = cv.position;
            b.coverTime = now;
            b.peeking = false;
            b.phaseUntil = now + 1.0 + u01(b.rng) * 1.5;
            break;
        }
        if (!b.hasCover) b.coverTime = now; // try again later
    }
    bool crouch = false;
    float lean = 0.0f;
    if (b.hasCover) {
        float toCover = glm::length(b.cover - xz(me.position));
        if (toCover > 0.9f) {
            moveTo(b, b.cover, sim, toCover > 5.0f, 0.5f);
            if (toCover > 30.0f) b.hasCover = false;
        } else {
            b.input.move = glm::vec2(0.0f);
            if (!b.peeking) {
                crouch = true;
                if (now > b.phaseUntil && !me.weapon.isReloading() && now - b.lastDamaged > 1.0) {
                    // Find a way to see out: stand up, or lean around the edge.
                    glm::vec3 base(me.position.x, me.position.y, me.position.z);
                    glm::vec3 right = me.right();
                    b.peekStand = lineOfSight(sim, base + glm::vec3(0, 1.66f, 0), c.position, me.id, c.id);
                    b.peekLean = 0.0f;
                    if (!b.peekStand) {
                        for (float side : {1.0f, -1.0f})
                            if (lineOfSight(sim, base + glm::vec3(0, 1.55f, 0) + right * (0.45f * side), c.position, me.id, c.id)) {
                                b.peekLean = side;
                                break;
                            }
                        if (b.peekLean == 0.0f) b.peekStand = true; // just stand and look
                    }
                    b.peeking = true;
                    b.phaseUntil = now + 1.5 + u01(b.rng) * (1.0 + 2.0 * b.aggression);
                }
            } else {
                crouch = !b.peekStand;
                lean = b.peekLean;
                if (now > b.phaseUntil || now - b.lastDamaged < 0.3) {
                    b.peeking = false;
                    b.phaseUntil = now + 0.8 + u01(b.rng) * 2.0 * b.caution;
                    if (u01(b.rng) < 0.25f + 0.3f * b.aggression) b.hasCover = false; // move to another position
                }
            }
        }
    } else if (visible) {
        if (dist > range * 0.85f) moveTo(b, xz(c.position), sim, false, 5.0f); // close in
        else {
            // Hold and shoot; strafe a little to be harder to hit.
            if (now > b.strafeUntil) {
                b.strafeSide = u01(b.rng) < 0.5f ? -1.0f : 1.0f;
                if (u01(b.rng) < 0.3f) b.strafeSide = 0.0f; // sometimes just hold still and aim
                b.strafeUntil = now + 1.0 + 2.0 * u01(b.rng);
            }
            float side = b.strafeSide;
            glm::vec2 toT = glm::normalize(xz(c.position - me.position) + glm::vec2(1e-4f));
            steer(b, sim, glm::vec2(-toT.y, toT.x) * side * (dist > 25.0f ? 0.5f : 1.0f), false);
            bool wantCrouch = dist > 50.0f && b.caution > 0.4f;
            if (now > b.crouchUntil && wantCrouch != b.crouchHeld) {
                b.crouchHeld = wantCrouch;
                b.crouchUntil = now + 1.0 + u01(b.rng);
            }
            crouch = b.crouchHeld;
        }
    } else {
        // Lost them: go to where they were, carefully.
        float d = glm::length(xz(c.position - me.position));
        if (d > 3.0f) moveTo(b, xz(c.position), sim, d > 60.0f && b.aggression > 0.6f, 2.0f);
        else c.awareness = std::min(c.awareness, 0.4f); // nobody here any more
    }
    b.input.crouch = crouch;
    b.input.lean = lean;

    // ----- aiming
    b.reaction -= dt;
    glm::vec3 aimAt = visible ? t.chest() : c.position;
    if (visible && dist < 25.0f && u01(b.rng) < b.skill * 0.02f) aimAt = t.head();
    if (visible) {
        float v = wd.projectile.muzzleVelocity;
        float tof = dist / std::max(v * 0.85f, 100.0f);
        aimAt += t.velocity * tof * b.skill;
        // Hold over for drop beyond the zero.
        float tz = me.weapon.zeroRange() / v;
        float drop = 0.5f * 9.81f * (tof * tof - tz * tz);
        aimAt.y += drop * b.skill;
    }
    // Shaky, imperfect tracking: worse when hurried, hurt or shot at.
    float sigma = 0.004f + 0.02f * (1.0f - b.skill) + me.suppression * 0.02f + (me.horizontalSpeed() > 0.5f ? 0.01f : 0.0f) +
                  (b.reaction > 0.0f ? 0.03f : 0.0f);
    std::normal_distribution<float> n(0.0f, 1.0f);
    const float tau = 0.5f;
    b.aimError += -b.aimError * (dt / tau) + glm::vec2(n(b.rng), n(b.rng)) * sigma * std::sqrt(2.0f * dt / tau);
    glm::vec3 to = aimAt - eye;
    float flat = std::sqrt(to.x * to.x + to.z * to.z);
    float wantYaw = yawOf(to) + b.aimError.x;
    float wantPitch = std::atan2(to.y, std::max(flat, 0.01f)) + b.aimError.y;
    // Tracking a target: a quicker, stiffer neck than when just looking around.
    b.wantYaw = wantYaw;
    b.wantPitch = wantPitch;
    b.turnRate = 2.5f + 5.0f * b.skill;
    b.lookStiffness = 14.0f + 10.0f * b.skill;

    bool optic = me.weapon.optic().type != OpticType::Iron;
    bool wantAds = (visible || b.peeking) && (dist > 12.0f || optic) && !me.weapon.isReloading();
    if (wantAds) b.adsUntil = now + 0.8;
    b.input.weapon.adsHeld = !me.weapon.isReloading() && (wantAds || now < b.adsUntil);
    // Full auto up close, single shots further out.
    if (wd.canAuto && me.weapon.item()) {
        bool wantAuto = dist < 35.0f;
        if (wantAuto != me.weapon.isFullAuto()) b.input.weapon.toggleFireMode = true;
    }

    // ----- trigger
    if (!visible || b.reaction > 0.0f || !me.weapon.hasWeapon()) {
        // Suppressing fire at where they were last seen.
        if (!visible && b.aggression > 0.75f && now - c.lastSeen < 2.5 && now >= b.nextShot && me.weapon.ammo() > 5) {
            b.input.weapon.firePressed = true;
            b.nextShot = now + 0.5 + u01(b.rng) * 0.5;
        }
        return;
    }
    glm::vec3 bore = me.weapon.boreDirection();
    glm::vec3 want = glm::normalize(aimAt - me.weapon.muzzle());
    float err = std::acos(std::clamp(glm::dot(bore, want), -1.0f, 1.0f));
    float tolerance = std::max(0.003f, 0.22f / std::max(dist, 1.0f)) * (1.6f - b.skill);
    if (dist > range * 1.3f) return; // too far for this gun
    if (err < tolerance && now >= b.nextShot) {
        if (me.weapon.isFullAuto()) {
            b.input.weapon.fireHeld = true;
            b.input.weapon.firePressed = true;
            if (++b.burst > int(40 + 60 * u01(b.rng))) {
                b.burst = 0;
                b.nextShot = now + 0.25 + 0.3 * u01(b.rng);
            }
        } else {
            b.input.weapon.firePressed = true;
            float base = dist < 20.0f ? 0.18f : dist < 80.0f ? 0.32f : 0.7f;
            b.nextShot = now + base * (1.4f - 0.6f * b.skill) * (0.8f + 0.4f * u01(b.rng));
        }
    } else {
        b.burst = 0;
    }
}

void BotManager::heal(Bot& b, Simulation& sim) {
    Character& me = *sim.chars[b.id];
    b.input.crouch = true;
    b.input.move = glm::vec2(0.0f);
    if (me.action() == Character::Action::Med) return;
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
            if (me.equip.slots[slot]) break;
            ItemPtr it = take(o.uid);
            if (it) me.equip.slots[slot] = std::move(it);
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
    float d = glm::length(xz(target - me.position));
    if (sim.time - b.goalSince > 90.0) {
        // Taking too long: forget it.
        if (b.lootType == 1) b.doneContainers.push_back(b.lootIndex);
        if (b.lootType == 2) b.doneBodies.push_back(b.lootIndex);
        b.lootType = 0;
        b.goalSince = sim.time;
        return;
    }
    if (d > 1.9f) {
        b.allowFinal = true;
        moveTo(b, xz(target), sim, d > 25.0f, 1.2f);
        b.allowFinal = false;
        glm::vec2 dir = b.pathIndex < b.path.size() ? b.path[b.pathIndex] - xz(me.position) : xz(target - me.position);
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
        glm::vec2 dir = b.pathIndex < b.path.size() ? b.path[b.pathIndex] - xz(me.position) : xz(target - me.position);
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
        glm::vec2 dir = b.pathIndex < b.path.size() ? b.path[b.pathIndex] - xz(me.position) : away;
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
            sprint = d > 90.0f;
            b.input.weapon.adsHeld = d < 45.0f && u01(b.rng) < 0.02f ? !prev.weapon.adsHeld : prev.weapon.adsHeld && d < 45.0f;
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
                std::vector<int> options;
                for (int i = 0; i < int(sim.map.hotspots.size()); ++i)
                    if (std::find(b.doneHotspots.begin(), b.doneHotspots.end(), i) == b.doneHotspots.end()) options.push_back(i);
                if (!options.empty() && u01(b.rng) < 0.7f) b.roamTarget = xz(sim.map.hotspots[options[size_t(u01(b.rng) * options.size()) % options.size()]].position);
                else b.roamTarget = glm::vec2(u11(b.rng), u11(b.rng)) * (sim.map.halfSize > 0.0f ? sim.map.halfSize * 0.8f : 30.0f);
                b.lootType = 0;
            }
            dest = b.roamTarget;
            // While roaming, opportunistically loot.
            if (sim.time > b.nextThink - 0.01 && chooseLootTarget(b, sim)) {
                b.goal = Goal::Loot;
                b.goalSince = now;
            }
        }
        moveTo(b, dest, sim, sprint, 2.0f);
        // Look where we're going, glancing around now and then.
        glm::vec2 dir = b.pathIndex < b.path.size() ? b.path[b.pathIndex] - xz(me.position) : dest - xz(me.position);
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
        // A pause to look and listen.
        b.input.move = glm::vec2(0.0f);
        b.input.sprint = false;
        if (sim.time > b.scanTime) {
            b.scanYaw = b.yaw + u11(b.rng) * 1.6f;
            b.scanTime = sim.time + 0.8 + 1.2 * u01(b.rng);
        }
        lookToward(b, b.scanYaw, 1.6f);
        b.wantPitch = u11(b.rng) * 0.02f;
    }
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
