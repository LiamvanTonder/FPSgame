#pragma once

#include "physics/World.h"

#include <glm/glm.hpp>
#include <deque>
#include <vector>

// Projectile description; each weapon supplies its own.
struct BallisticParams {
    float muzzleVelocity = 910.0f; // m/s
    float mass = 0.00356f;         // kg
    float diameter = 0.0057f;      // m
    float dragCoefficient = 0.25f; // simplified, constant
    float airDensity = 1.225f;     // kg/m^3

    // Quadratic drag factor k such that a_drag = -k |v| v.
    float dragFactor() const {
        float area = 3.14159265f * diameter * diameter * 0.25f;
        return 0.5f * airDensity * dragCoefficient * area / mass;
    }
};

struct Impact {
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f};
    glm::vec3 velocity{0.0f};
    int collider = -1;
    unsigned shot = 0;       // id of the bullet that made this impact
    int shooter = kOwnerPlayer; // owner id of whoever fired it
    int ammo = -1;           // ammo def (-1: unknown / default)
    float mass = 0.0f;       // projectile mass (kg)
    float timeOfFlight = 0.0f;
    float distance = 0.0f;   // straight-line from muzzle
    float dropBelowBore = 0.0f; // how far gravity pulled it below the bore axis
    bool passedThrough = false;
};

struct Trail {
    std::vector<glm::vec3> points;
    float age = 0.0f;
};

class Ballistics {
public:
    struct Bullet {
        glm::vec3 position, velocity;
        glm::vec3 origin, boreDir;
        float drag = 0.0f; // k in a = -k|v|v
        float mass = 0.0f;
        float age = 0.0f;
        int lastPenetrated = -1;
        int owner = kOwnerPlayer;
        int ammo = -1;
        float penetration = 0.0f;
        unsigned id = 0; // also identifies this bullet's trail
    };

    // A shot starts at the chamber so a muzzle buried in a wall hits the wall.
    void fire(const BallisticParams& p, const glm::vec3& chamber, const glm::vec3& muzzle, const glm::vec3& velocity,
              const World& world, std::vector<Impact>& impacts, int owner = kOwnerPlayer, int ammo = -1,
              float penetration = 0.0f);
    void update(float dt, const World& world, std::vector<Impact>& impacts);

    // Bore elevation (radians, above the sight line) that makes the bullet cross the
    // sight line again at zeroRange, for sights mounted sightHeight above the bore.
    static float solveZeroAngle(const BallisticParams& p, float sightHeight, float zeroRange);

    const std::vector<Bullet>& bullets() const { return m_bullets; }
    const std::deque<Trail>& trails() const { return m_trails; }
    int activeCount() const { return int(m_bullets.size()); }
    void clearTrails() { m_trails.clear(); }

    float maxLifetime = 4.0f;    // s
    float maxDistance = 1500.0f; // m
    int substeps = 4;            // per simulation tick

private:
    // Moves a segment through the world; returns false if the bullet stopped.
    bool traceSegment(Bullet& b, const glm::vec3& from, const glm::vec3& to, const World& world,
                      std::vector<Impact>& impacts);
    Trail* trailFor(const Bullet& b);

    std::vector<Bullet> m_bullets;
    std::deque<Trail> m_trails;
    unsigned m_nextId = 1;
    unsigned m_firstTrailId = 1; // id of m_trails.front()
};
