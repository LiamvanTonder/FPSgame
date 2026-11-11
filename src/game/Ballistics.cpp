#include "game/Ballistics.h"

#include <cmath>

static const glm::vec3 kGravity(0.0f, -9.81f, 0.0f);
static const size_t kMaxTrails = 120; // a shotgun shot alone makes nine

Trail* Ballistics::trailFor(const Bullet& b) {
    if (b.id < m_firstTrailId) return nullptr;
    size_t idx = b.id - m_firstTrailId;
    return idx < m_trails.size() ? &m_trails[idx] : nullptr;
}

void Ballistics::fire(const BallisticParams& p, const glm::vec3& chamber, const glm::vec3& muzzle,
                      const glm::vec3& velocity, const World& world, std::vector<Impact>& impacts, int owner, int ammo,
                      float penetration) {
    Bullet b;
    b.position = muzzle;
    b.velocity = velocity;
    b.origin = muzzle;
    b.boreDir = glm::normalize(velocity);
    b.drag = p.dragFactor();
    b.mass = p.mass;
    b.owner = owner;
    b.ammo = ammo;
    b.penetration = penetration;
    b.id = m_nextId++;

    // Trail ids stay contiguous with bullet ids.
    if (m_trails.empty()) m_firstTrailId = b.id;
    m_trails.push_back(Trail{{muzzle}, 0.0f});
    while (m_trails.size() > kMaxTrails) {
        m_trails.pop_front();
        ++m_firstTrailId;
    }

    // Inside the barrel: anything between chamber and muzzle stops the bullet.
    if (!traceSegment(b, chamber, muzzle, world, impacts)) return;
    m_bullets.push_back(b);
}

bool Ballistics::traceSegment(Bullet& b, const glm::vec3& from, const glm::vec3& to, const World& world,
                              std::vector<Impact>& impacts) {
    glm::vec3 start = from;
    for (int guard = 0; guard < 8; ++guard) {
        WorldHit hit;
        SegmentQuery q;
        q.ignoreCollider = b.lastPenetrated;
        q.ignoreOwner = b.owner;
        if (!world.segment(start, to, hit, q)) return true;

        const Collider& c = world.collider(hit.collider);
        Impact imp;
        imp.point = hit.point;
        imp.normal = hit.normal;
        imp.velocity = b.velocity;
        imp.collider = hit.collider;
        imp.shot = b.id;
        imp.shooter = b.owner;
        imp.ammo = b.ammo;
        imp.mass = b.mass;
        imp.timeOfFlight = b.age;
        imp.distance = glm::length(hit.point - b.origin);
        float along = glm::dot(hit.point - b.origin, b.boreDir);
        imp.dropBelowBore = (b.origin + b.boreDir * along - hit.point).y;
        // Armour-piercing rounds punch through wooden walls and sheet metal, losing speed.
        float keep = c.penetrable ? 0.98f : 0.0f;
        if (!c.penetrable && !c.terrain && c.owner == kOwnerWorld) {
            if (c.surface == Surface::Wood && b.penetration >= 0.4f) keep = 0.45f + 0.35f * b.penetration;
            else if (c.surface == Surface::Metal && b.penetration >= 0.7f) keep = 0.35f + 0.3f * b.penetration;
            if (keep > 0.0f && glm::length(b.velocity) * keep < 200.0f) keep = 0.0f;
        }
        imp.passedThrough = keep > 0.0f;
        impacts.push_back(imp);

        if (Trail* t = trailFor(b)) t->points.push_back(hit.point);
        if (keep <= 0.0f) return false;

        // Continue from just past the hit with what speed is left.
        b.lastPenetrated = hit.collider;
        b.velocity *= keep;
        start = hit.point + glm::normalize(b.velocity) * 0.002f;
    }
    return true;
}

void Ballistics::update(float dt, const World& world, std::vector<Impact>& impacts) {
    const float h = dt / float(substeps);

    for (size_t i = 0; i < m_bullets.size();) {
        Bullet& b = m_bullets[i];
        const float k = b.drag;
        bool alive = true;
        for (int s = 0; s < substeps && alive; ++s) {
            // Midpoint integration of gravity + quadratic drag.
            glm::vec3 a0 = kGravity - k * glm::length(b.velocity) * b.velocity;
            glm::vec3 vMid = b.velocity + a0 * (h * 0.5f);
            glm::vec3 aMid = kGravity - k * glm::length(vMid) * vMid;
            glm::vec3 next = b.position + vMid * h;
            b.age += h;

            alive = traceSegment(b, b.position, next, world, impacts);
            b.position = next;
            b.velocity += aMid * h;
        }
        if (alive) {
            if (Trail* t = trailFor(b)) t->points.push_back(b.position);
        }
        if (b.age > maxLifetime || glm::length(b.position - b.origin) > maxDistance) alive = false;

        if (!alive) {
            m_bullets[i] = m_bullets.back();
            m_bullets.pop_back();
        } else {
            ++i;
        }
    }
    for (Trail& t : m_trails) t.age += dt;
}

float Ballistics::solveZeroAngle(const BallisticParams& p, float sightHeight, float zeroRange) {
    // 2D simulation: x downrange, y up, bore at y=0, sight line at y=sightHeight
    // parallel to x. Find the elevation where the bullet is at sightHeight at zeroRange.
    const float k = p.dragFactor();
    auto heightAt = [&](float angle) {
        glm::vec2 pos(0.0f), v(std::cos(angle), std::sin(angle));
        v *= p.muzzleVelocity;
        const float h = 1.0f / 2000.0f;
        const glm::vec2 g(0.0f, -9.81f);
        for (int i = 0; i < 20000 && pos.x < zeroRange; ++i) {
            glm::vec2 a0 = g - k * glm::length(v) * v;
            glm::vec2 vm = v + a0 * (h * 0.5f);
            glm::vec2 am = g - k * glm::length(vm) * vm;
            glm::vec2 np = pos + vm * h;
            if (np.x >= zeroRange) {
                float f = (zeroRange - pos.x) / (np.x - pos.x);
                return pos.y + (np.y - pos.y) * f;
            }
            pos = np;
            v += am * h;
        }
        return pos.y;
    };
    float lo = 0.0f, hi = 0.05f;
    for (int i = 0; i < 40; ++i) {
        float mid = 0.5f * (lo + hi);
        if (heightAt(mid) < sightHeight) lo = mid;
        else hi = mid;
    }
    return 0.5f * (lo + hi);
}
