#pragma once

#include "game/Ballistics.h"
#include "game/Weapon.h"
#include "physics/World.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <deque>
#include <random>
#include <vector>

class Renderer;
struct Camera;

// Visual feedback: bullets in flight, impacts, decals, muzzle smoke and ejected cases.
class Effects {
public:
    void update(float dt, const World& world, const Ballistics& ballistics);
    void onImpact(const Impact& imp, const Collider& c, const glm::vec3& viewer, const World& world);
    // Blood falling from a wound; leaves a small spot where it lands.
    void bloodDrip(const glm::vec3& p, const World& world);
    // A pool that spreads under a body (attached to whatever is below p).
    void bloodPool(const glm::vec3& p, const World& world, float maxSize);
    void onWeaponEvent(const WeaponEvent& e);
    void draw(Renderer& r, const Camera& camera, const World& world) const;
    void clearDecals() { m_decals.clear(); }

private:
    enum class Kind { Debris, Spark, Flash, Smoke, Confetti, Drop };
    struct Particle {
        Kind kind;
        glm::vec3 position, velocity, color;
        float life, maxLife;
        float size, sizeEnd;
        float opacity;    // smoke opacity / glow intensity
        float gravity;    // multiplier of g
        float drag;       // 1/s
        glm::quat rotation;
        glm::vec3 spin;
    };
    struct Casing {
        glm::vec3 position, velocity, spin;
        glm::quat rotation;
        glm::vec2 size; // length, diameter
        glm::vec3 color;
        float life;
        bool resting;
    };
    // Attached to a collider so it moves with it (swinging plates).
    struct Decal {
        int collider;
        glm::vec3 localPos, localNormal;
        float size;
        glm::vec3 color;
        float targetSize; // decals can spread over time (blood pools)
        float growRate;
    };
    struct VaporTrail {
        unsigned bullet;
        std::vector<glm::vec4> points; // xyz + age
        float width;
        bool bulletAlive;
    };
    struct Streak {
        glm::vec3 position, velocity, origin;
        float width;
    };

    void burst(Kind kind, int count, const glm::vec3& p, const glm::vec3& normal, const glm::vec3& color, float speed,
               float spread, float size, float sizeEnd, float life, float opacity, float gravity, float drag);
    void addDecal(const Collider& c, int colliderIndex, const glm::vec3& point, const glm::vec3& normal, float size,
                  const glm::vec3& color, float targetSize = 0.0f, float growRate = 0.0f);
    void bloodImpact(const Impact& imp, const Collider& c, const World& world);
    float rand01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(m_rng); }
    float rand11() { return std::uniform_real_distribution<float>(-1.0f, 1.0f)(m_rng); }
    glm::vec3 randUnit();

    std::vector<Particle> m_particles;
    std::vector<Casing> m_casings;
    std::deque<Decal> m_decals;
    std::vector<VaporTrail> m_trails;
    std::vector<Streak> m_streaks;
    std::mt19937 m_rng{7};
};
