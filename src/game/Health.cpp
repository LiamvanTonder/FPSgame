#include "game/Health.h"

#include <algorithm>
#include <cmath>

namespace {
const float kMaxHp[kZoneCount] = {35.0f, 85.0f, 70.0f, 60.0f, 60.0f, 65.0f, 65.0f};
const float kZoneMul[kZoneCount] = {2.0f, 1.0f, 1.0f, 0.8f, 0.8f, 0.8f, 0.8f};
const float kLightBleedRate = 0.4f, kHeavyBleedRate = 1.6f;

float u01(std::mt19937& rng) { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
bool isLimb(int z) { return z >= Z_LArm; }
bool overflows(int z) { return z != Z_Head && z != Z_Thorax; }
} // namespace

const char* zoneName(int z) {
    static const char* names[kZoneCount] = {"head", "thorax", "stomach", "left arm", "right arm", "left leg", "right leg"};
    return z >= 0 && z < kZoneCount ? names[z] : "?";
}

int zoneOfPart(int part) {
    // Matches BodyPart in Character.h
    static const int map[11] = {Z_Head, Z_Thorax, Z_Stomach, Z_LArm, Z_RArm, Z_LArm, Z_RArm, Z_LLeg, Z_RLeg, Z_LLeg, Z_RLeg};
    return part >= 0 && part < 11 ? map[part] : Z_Thorax;
}

std::string HitOutcome::describe(int) const {
    std::string s;
    auto add = [&](const char* t) {
        if (!s.empty()) s += ", ";
        s += t;
    };
    if (killed) add("killed");
    if (destroyed && !killed) add("limb destroyed");
    if (newFracture) add("fracture");
    if (newHeavyBleed) add("heavy bleeding");
    else if (newLightBleed) add("bleeding");
    if (s.empty()) add("wounded");
    return s;
}

void Health::reset() {
    for (int z = 0; z < kZoneCount; ++z) m_zones[z] = ZoneState{kMaxHp[z], kMaxHp[z]};
    m_pain = 0.0f;
    m_painkiller = 0.0f;
    m_dead = false;
    killer = -1;
    causeOfDeath.clear();
    damageTaken = 0.0f;
}

float Health::totalHp() const {
    float t = 0.0f;
    for (const ZoneState& z : m_zones) t += std::max(0.0f, z.hp);
    return t;
}

float Health::totalMax() const {
    float t = 0.0f;
    for (const ZoneState& z : m_zones) t += z.max;
    return t;
}

bool Health::bleeding() const {
    for (const ZoneState& z : m_zones)
        if (z.lightBleed || z.heavyBleed) return true;
    return false;
}

bool Health::heavyBleeding() const {
    for (const ZoneState& z : m_zones)
        if (z.heavyBleed) return true;
    return false;
}

bool Health::anyFracture() const {
    for (const ZoneState& z : m_zones)
        if (z.fracture) return true;
    return false;
}

bool Health::canSprint() const { return !legImpaired(0) && !legImpaired(1); }

float Health::moveSpeedScale() const {
    if (legsGone()) return 0.25f;
    float s = 1.0f;
    for (int side = 0; side < 2; ++side) {
        const ZoneState& z = m_zones[Z_LLeg + side];
        if (z.destroyed()) s *= 0.55f;
        else if (z.fracture) s *= 0.7f;
    }
    if (m_zones[Z_Stomach].destroyed()) s *= 0.8f;
    return s;
}

float Health::armShake() const {
    float s = pain() * 0.015f;
    for (int side = 0; side < 2; ++side) {
        const ZoneState& z = m_zones[Z_LArm + side];
        float w = side == 1 ? 1.0f : 0.6f;
        if (z.destroyed()) s += 0.03f * w;
        else if (z.fracture) s += 0.018f * w;
    }
    return s;
}

float Health::armSpeedScale() const {
    float s = 1.0f - 0.3f * pain();
    for (int side = 0; side < 2; ++side) {
        const ZoneState& z = m_zones[Z_LArm + side];
        if (z.destroyed()) s *= 0.45f;
        else if (z.fracture) s *= 0.6f;
    }
    return std::max(s, 0.2f);
}

void Health::checkDeath(int shooter, const char* cause) {
    if (m_dead) return;
    if (m_zones[Z_Head].destroyed() || m_zones[Z_Thorax].destroyed()) {
        m_dead = true;
        killer = shooter;
        causeOfDeath = cause;
    }
}

void Health::damageZone(int z, float dmg, int shooter, const char* cause) {
    damageTaken += dmg;
    ZoneState& s = m_zones[z];
    if (s.destroyed() && overflows(z)) {
        // A limb that is already gone passes the damage on to the rest of the body.
        float share = dmg * 0.7f / 6.0f;
        for (int o = 0; o < kZoneCount; ++o)
            if (o != z && !m_zones[o].destroyed()) m_zones[o].hp -= share * (o == Z_Head ? 0.5f : 1.0f);
    } else {
        s.hp -= dmg;
        if (s.hp < 0.0f && !overflows(z)) s.hp = 0.0f;
        if (s.hp < 0.0f) {
            float over = -s.hp;
            s.hp = 0.0f;
            float share = over * 0.7f / 6.0f;
            for (int o = 0; o < kZoneCount; ++o)
                if (o != z && !m_zones[o].destroyed()) m_zones[o].hp -= share * (o == Z_Head ? 0.5f : 1.0f);
        }
    }
    for (ZoneState& o : m_zones) o.hp = std::max(o.hp, 0.0f);
    checkDeath(shooter, cause);
}

HitOutcome Health::applyHit(int bodyPart, float energyJ, const AmmoDef& ammo, int shooter, std::mt19937& rng, bool blunt) {
    HitOutcome out;
    if (m_dead) return out;
    int z = zoneOfPart(bodyPart);
    float k = std::max(energyJ, 0.0f) / 1000.0f;
    float dmg = 45.0f * std::pow(k, 0.6f) * kZoneMul[z] * (blunt ? 1.0f : ammo.damage);
    out.damage = dmg;
    bool wasDestroyed = m_zones[z].destroyed();
    damageZone(z, dmg, shooter, z == Z_Head ? "head shot" : "gunshot wound");
    ZoneState& s = m_zones[z];
    out.destroyed = !wasDestroyed && s.destroyed();

    float bleedMul = blunt ? 0.0f : ammo.bleed * (z == Z_Thorax || z == Z_Stomach ? 1.3f : 1.0f);
    if (!s.heavyBleed && u01(rng) < (0.10f + 0.15f * k) * bleedMul) {
        s.heavyBleed = true;
        out.newHeavyBleed = true;
    } else if (!s.lightBleed && !s.heavyBleed && u01(rng) < (0.35f + 0.2f * k) * bleedMul) {
        s.lightBleed = true;
        out.newLightBleed = true;
    }
    if (isLimb(z) && !s.fracture && u01(rng) < 0.15f + 0.25f * k) {
        s.fracture = true;
        out.newFracture = true;
    }
    m_pain = std::min(1.0f, m_pain + 0.25f + 0.2f * k);
    out.killed = m_dead;
    return out;
}

void Health::applyFall(float speed, std::mt19937& rng) {
    if (speed < 6.0f || m_dead) return;
    float dmg = (speed - 6.0f) * 9.0f;
    for (int z : {Z_LLeg, Z_RLeg}) {
        damageZone(z, dmg * 0.5f, -1, "fall");
        if (u01(rng) < (speed - 6.0f) * 0.25f) m_zones[z].fracture = true;
    }
    m_pain = std::min(1.0f, m_pain + 0.3f);
}

void Health::update(float dt) {
    if (m_dead) return;
    for (int z = 0; z < kZoneCount; ++z) {
        ZoneState& s = m_zones[z];
        float rate = (s.heavyBleed ? kHeavyBleedRate : 0.0f) + (s.lightBleed ? kLightBleedRate : 0.0f);
        if (rate <= 0.0f) continue;
        if (s.destroyed()) {
            // Bleeding from a destroyed limb drains the torso.
            m_zones[Z_Thorax].hp -= rate * dt * 0.5f;
            m_zones[Z_Stomach].hp -= rate * dt * 0.5f;
        } else {
            s.hp = std::max(0.0f, s.hp - rate * dt);
        }
    }
    m_zones[Z_Stomach].hp = std::max(0.0f, m_zones[Z_Stomach].hp);
    if (m_zones[Z_Thorax].hp <= 0.0f) {
        m_zones[Z_Thorax].hp = 0.0f;
        checkDeath(killer, "blood loss");
    }
    m_painkiller = std::max(0.0f, m_painkiller - dt);
    m_pain = std::max(0.0f, m_pain - dt * 0.004f);
    // Wounds keep hurting while they are open.
    if (bleeding() || anyFracture()) m_pain = std::max(m_pain, 0.35f);
}

bool Health::canTreat(MedKind kind, int z) const {
    if (z < 0 || z >= kZoneCount) return false;
    const ZoneState& s = m_zones[z];
    switch (kind) {
    case MedKind::Bandage: return s.lightBleed;
    case MedKind::Tourniquet: return s.heavyBleed && isLimb(z);
    case MedKind::Hemostatic: return s.heavyBleed;
    case MedKind::Splint: return s.fracture;
    case MedKind::Painkillers: return m_pain > 0.05f && m_painkiller < 30.0f;
    case MedKind::FieldKit: return !s.destroyed() && (s.hp < s.max - 0.5f || s.lightBleed);
    case MedKind::Surgical: return s.destroyed() && isLimb(z);
    }
    return false;
}

int Health::bestZoneFor(MedKind kind) const {
    int best = -1;
    float bestScore = -1.0f;
    for (int z = 0; z < kZoneCount; ++z) {
        if (!canTreat(kind, z)) continue;
        const ZoneState& s = m_zones[z];
        float score = (s.heavyBleed ? 100.0f : 0.0f) + (s.lightBleed ? 50.0f : 0.0f) + (z == Z_Thorax || z == Z_Head ? 20.0f : 0.0f) +
                      (s.max - s.hp) / s.max * 10.0f;
        if (score > bestScore) {
            bestScore = score;
            best = z;
        }
    }
    return best;
}

void Health::treat(MedKind kind, int z, float& pool) {
    if (z < 0 || z >= kZoneCount) return;
    ZoneState& s = m_zones[z];
    switch (kind) {
    case MedKind::Bandage: s.lightBleed = false; break;
    case MedKind::Tourniquet:
        s.heavyBleed = false;
        s.hp = std::max(0.0f, s.hp - 5.0f); // a tourniquet costs the limb
        break;
    case MedKind::Hemostatic: s.heavyBleed = false; break;
    case MedKind::Splint: s.fracture = false; break;
    case MedKind::Painkillers: m_painkiller = 90.0f; break;
    case MedKind::FieldKit: {
        float heal = std::min({60.0f, s.max - s.hp, pool});
        s.hp += heal;
        pool -= std::max(heal, 5.0f);
        s.lightBleed = false;
        break;
    }
    case MedKind::Surgical:
        s.max *= 0.8f; // never quite the same
        s.hp = 0.5f * kMaxHp[z];
        s.hp = std::min(s.hp, s.max);
        s.heavyBleed = s.lightBleed = false;
        break;
    }
}
