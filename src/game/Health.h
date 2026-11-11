#pragma once

#include "items/ItemDefs.h"

#include <random>
#include <string>

enum Zone { Z_Head, Z_Thorax, Z_Stomach, Z_LArm, Z_RArm, Z_LLeg, Z_RLeg, kZoneCount };
const char* zoneName(int zone);
// Hitbox part (BodyPart) -> health zone.
int zoneOfPart(int bodyPart);

struct ZoneState {
    float hp = 0.0f, max = 0.0f;
    bool lightBleed = false, heavyBleed = false, fracture = false;
    bool destroyed() const { return hp <= 0.0f; }
};

struct HitOutcome {
    float damage = 0.0f;
    bool killed = false;
    bool newLightBleed = false, newHeavyBleed = false, newFracture = false, destroyed = false;
    std::string describe(int part) const;
};

// Per-zone hit points and conditions, shared by the player and bots.
// Damage scales with the projectile's energy, as the rest of the wound model does.
class Health {
public:
    void reset();
    HitOutcome applyHit(int bodyPart, float energyJ, const AmmoDef& ammo, int shooter, std::mt19937& rng);
    // Fall damage to the legs.
    void applyFall(float impactSpeed, std::mt19937& rng);
    void update(float dt);

    // Medical treatment. Returns false if it would do nothing for that zone.
    bool canTreat(MedKind kind, int zone) const;
    // Best zone for a med (-1 if none needs it).
    int bestZoneFor(MedKind kind) const;
    // Applies one use; 'pool' is the item's remaining HP for field kits (consumed).
    void treat(MedKind kind, int zone, float& pool);

    bool dead() const { return m_dead; }
    const ZoneState& zone(int z) const { return m_zones[z]; }
    float totalHp() const;
    float totalMax() const;
    bool bleeding() const;
    bool heavyBleeding() const;
    bool anyFracture() const;
    float pain() const { return m_painkiller > 0.0f ? m_pain * 0.15f : m_pain; }
    float painkillerTime() const { return m_painkiller; }

    // Effects on the body (1 = healthy).
    bool legImpaired(int side) const { return m_zones[Z_LLeg + side].fracture || m_zones[Z_LLeg + side].destroyed(); }
    bool canSprint() const;
    bool legsGone() const { return m_zones[Z_LLeg].destroyed() && m_zones[Z_RLeg].destroyed(); }
    float moveSpeedScale() const;
    // [0] support (left), [1] firing (right)
    bool armImpaired(int side) const { return m_zones[Z_LArm + side].fracture || m_zones[Z_LArm + side].destroyed(); }
    bool armGone(int side) const { return m_zones[Z_LArm + side].destroyed(); }
    float armShake() const;      // added tremor amplitude
    float armSpeedScale() const; // weapon handling

    int killer = -1;          // owner id of whoever caused death
    std::string causeOfDeath;
    float damageTaken = 0.0f;

private:
    void damageZone(int z, float dmg, int shooter, const char* cause);
    void checkDeath(int shooter, const char* cause);

    ZoneState m_zones[kZoneCount];
    float m_pain = 0.0f;       // 0..1
    float m_painkiller = 0.0f; // s remaining
    bool m_dead = false;
};
