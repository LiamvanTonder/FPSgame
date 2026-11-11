#include "game/Effects.h"

#include "world/Terrain.h"

#include "game/ArmModel.h"
#include "render/Camera.h"
#include "render/Renderer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace {
const float kVaporLife = 0.5f;
const size_t kMaxDecals = 500;
const size_t kMaxCasings = 150;
const size_t kMaxParticles = 2500;

glm::mat4 segmentMatrix(const glm::vec3& a, const glm::vec3& b, float thickness) {
    glm::vec3 d = b - a;
    float len = std::max(glm::length(d), 1e-4f);
    glm::mat4 m = glm::mat4_cast(lookRotation(d));
    m[3] = glm::vec4((a + b) * 0.5f, 1.0f);
    return glm::scale(m, glm::vec3(thickness, thickness, len));
}

glm::mat4 trs(const glm::vec3& p, const glm::quat& q, const glm::vec3& s) {
    glm::mat4 m = glm::mat4_cast(q);
    m[3] = glm::vec4(p, 1.0f);
    return glm::scale(m, s);
}

// Rough muzzle-energy scale relative to a 5.56 round (~1 kJ).
float energyScale(const Impact& imp) {
    float v = glm::length(imp.velocity);
    float e = 0.5f * imp.mass * v * v;
    return std::clamp(std::sqrt(e / 1000.0f), 0.35f, 1.8f);
}
} // namespace

glm::vec3 Effects::randUnit() {
    glm::vec3 v(rand11(), rand11(), rand11());
    float l = glm::length(v);
    return l > 1e-4f ? v / l : glm::vec3(0, 1, 0);
}

void Effects::burst(Kind kind, int count, const glm::vec3& p, const glm::vec3& normal, const glm::vec3& color,
                    float speed, float spread, float size, float sizeEnd, float life, float opacity, float gravity,
                    float drag) {
    for (int i = 0; i < count; ++i) {
        glm::vec3 dir = glm::normalize(normal + randUnit() * spread + glm::vec3(0.0f, 1e-3f, 0.0f));
        float l = life * (0.6f + 0.4f * rand01());
        float s = size * (0.7f + 0.6f * rand01());
        Particle pt;
        pt.kind = kind;
        pt.position = p + normal * 0.01f;
        pt.velocity = dir * speed * (0.4f + 0.6f * rand01());
        pt.color = color * (0.85f + 0.3f * rand01());
        pt.life = pt.maxLife = l;
        pt.size = s;
        pt.sizeEnd = sizeEnd * (0.7f + 0.6f * rand01());
        pt.opacity = opacity;
        pt.gravity = gravity;
        pt.drag = drag;
        pt.rotation = glm::angleAxis(rand01() * 6.28f, randUnit());
        pt.spin = randUnit() * (kind == Kind::Debris || kind == Kind::Confetti ? 12.0f : 0.0f);
        m_particles.push_back(pt);
    }
    if (m_particles.size() > kMaxParticles)
        m_particles.erase(m_particles.begin(), m_particles.begin() + (m_particles.size() - kMaxParticles));
}

void Effects::addDecal(const Collider& c, int colliderIndex, const glm::vec3& point, const glm::vec3& normal,
                       float size, const glm::vec3& color, float targetSize, float growRate) {
    glm::mat3 inv = glm::transpose(c.box.rotation);
    m_decals.push_back({colliderIndex, inv * (point - c.box.center), inv * normal, size, color, std::max(size, targetSize), growRate});
    while (m_decals.size() > kMaxDecals) m_decals.pop_front();
}

void Effects::onImpact(const Impact& imp, const Collider& c, const glm::vec3& viewer, const World& world) {
    const glm::vec3 p = imp.point;
    const glm::vec3 n = imp.normal;
    const float es = energyScale(imp);
    const float dist = glm::length(p - viewer);
    // A brief flash at the strike point; kept at least a few pixels wide so distant hits read.
    auto flash = [&](const glm::vec3& color, float intensity, float size) {
        burst(Kind::Flash, 1, p, n, color, 0.0f, 0.0f, std::max(size, dist * 0.003f), std::max(size, dist * 0.003f) * 1.6f,
              0.06f, intensity, 0.0f, 0.0f);
    };

    switch (c.surface) {
    case Surface::Dirt:
    case Surface::Grass: {
        glm::vec3 dust = c.surface == Surface::Dirt ? glm::vec3(0.58f, 0.47f, 0.33f) : glm::vec3(0.50f, 0.46f, 0.34f);
        burst(Kind::Smoke, 6, p, n, dust, 1.3f * es, 0.7f, 0.10f * es, 0.6f * es, 2.0f, 0.6f, -0.02f, 2.2f);
        burst(Kind::Debris, 12, p, n, dust * 0.7f, 4.5f * es, 0.6f, 0.025f, 0.025f, 1.2f, 1.0f, 1.0f, 0.4f);
        flash(glm::vec3(1.0f, 0.8f, 0.5f), 0.8f, 0.04f);
        addDecal(c, imp.collider, p, n, 0.06f * es, glm::vec3(0.22f, 0.18f, 0.12f));
        break;
    }
    case Surface::Concrete: {
        burst(Kind::Smoke, 4, p, n, glm::vec3(0.78f, 0.78f, 0.75f), 1.5f * es, 0.6f, 0.06f * es, 0.35f * es, 1.3f, 0.55f, -0.01f, 2.5f);
        burst(Kind::Debris, 9, p, n, glm::vec3(0.70f, 0.70f, 0.68f), 5.5f * es, 0.7f, 0.015f, 0.015f, 1.0f, 1.0f, 1.0f, 0.3f);
        burst(Kind::Spark, 3, p, n, glm::vec3(1.0f, 0.75f, 0.35f), 6.0f, 0.8f, 0.0f, 0.0f, 0.15f, 2.5f, 1.0f, 0.5f);
        flash(glm::vec3(1.0f, 0.85f, 0.6f), 1.5f, 0.05f);
        addDecal(c, imp.collider, p, n, 0.035f * es, glm::vec3(0.72f, 0.72f, 0.70f)); // spalled crater
        addDecal(c, imp.collider, p + n * 0.0015f, n, 0.010f, glm::vec3(0.12f, 0.12f, 0.12f));
        break;
    }
    case Surface::Wood: {
        burst(Kind::Smoke, 2, p, n, glm::vec3(0.75f, 0.65f, 0.50f), 1.0f, 0.5f, 0.04f, 0.18f, 0.9f, 0.4f, 0.0f, 2.5f);
        burst(Kind::Debris, 10, p, n, glm::vec3(0.78f, 0.62f, 0.40f), 4.0f * es, 0.7f, 0.02f, 0.02f, 1.0f, 1.0f, 1.0f, 0.5f);
        flash(glm::vec3(1.0f, 0.8f, 0.5f), 0.8f, 0.03f);
        addDecal(c, imp.collider, p, n, 0.012f, glm::vec3(0.12f, 0.08f, 0.05f));
        break;
    }
    case Surface::Steel:
    case Surface::Metal: {
        int sparks = c.surface == Surface::Steel ? 18 : 9;
        burst(Kind::Spark, sparks, p, n, glm::vec3(1.0f, 0.70f, 0.30f), 9.0f * es, 0.9f, 0.0f, 0.0f, 0.28f, 3.0f, 1.0f, 0.8f);
        burst(Kind::Smoke, 2, p, n, glm::vec3(0.6f, 0.6f, 0.6f), 0.8f, 0.5f, 0.04f, 0.25f, 0.8f, 0.35f, -0.02f, 2.5f);
        flash(glm::vec3(1.0f, 0.85f, 0.55f), 4.0f, 0.08f * es);
        // Lead splash on painted steel; dark strike mark on bare metal
        glm::vec3 mark = c.surface == Surface::Steel ? glm::vec3(0.50f, 0.50f, 0.52f) : glm::vec3(0.08f, 0.08f, 0.08f);
        addDecal(c, imp.collider, p, n, c.surface == Surface::Steel ? 0.04f * es : 0.012f, mark);
        break;
    }
    case Surface::Flesh:
        bloodImpact(imp, c, world);
        break;
    case Surface::Foliage: {
        glm::vec3 out = glm::normalize(imp.velocity);
        burst(Kind::Confetti, 6, p, out, glm::vec3(0.25f, 0.38f, 0.16f), 1.2f, 0.8f, 0.015f, 0.015f, 1.5f, 1.0f, 0.12f, 2.5f);
        break;
    }
    case Surface::Paper: {
        glm::vec3 out = glm::normalize(imp.velocity);
        burst(Kind::Confetti, 5, p, out, glm::vec3(0.95f, 0.93f, 0.88f), 1.5f, 0.6f, 0.012f, 0.012f, 1.5f, 1.0f, 0.15f, 2.5f);
        break;
    }
    }
}

namespace {
const glm::vec3 kBlood(0.42f, 0.02f, 0.02f);
const glm::vec3 kBloodDark(0.25f, 0.01f, 0.01f);
} // namespace

void Effects::bloodImpact(const Impact& imp, const Collider& c, const World& world) {
    const glm::vec3 p = imp.point;
    float speed = glm::length(imp.velocity);
    glm::vec3 dir = speed > 1.0f ? imp.velocity / speed : -imp.normal;
    float es = energyScale(imp);

    // Entry: a puff of red mist back toward the shooter.
    burst(Kind::Smoke, 3, p, imp.normal, kBlood * 1.2f, 0.8f, 0.6f, 0.03f, 0.14f * es, 0.4f, 0.65f, 0.0f, 4.0f);
    // Exit: spray and mist thrown on along the bullet's path, droplets that fall and stain.
    glm::vec3 exitPt = p + dir * 0.25f;
    burst(Kind::Smoke, 4, exitPt, dir, kBlood, 2.5f * es, 0.5f, 0.05f, 0.45f * es, 0.6f, 0.5f, 0.05f, 3.0f);
    burst(Kind::Drop, int(10 + 10 * es), exitPt, dir, kBlood, 5.0f * es, 0.45f, 0.012f, 0.012f, 1.5f, 1.0f, 1.0f, 0.3f);
    burst(Kind::Drop, 5, p, imp.normal, kBlood, 1.5f, 0.8f, 0.01f, 0.01f, 1.2f, 1.0f, 1.0f, 0.3f);

    // Wound on the body part (follows it, alive or ragdoll).
    addDecal(c, imp.collider, p, imp.normal, 0.035f, kBloodDark);

    // Splatter on whatever is close behind.
    SegmentQuery q;
    q.staticOnly = true;
    WorldHit hit;
    if (world.segment(exitPt, exitPt + dir * 3.0f, hit, q)) {
        const Collider& wall = world.collider(hit.collider);
        float spread = 0.12f + 0.25f * es * (1.0f - hit.t);
        addDecal(wall, hit.collider, hit.point, hit.normal, spread, kBlood);
        for (int i = 0; i < 4; ++i) {
            glm::vec3 jitter = randUnit() * spread * 0.9f;
            jitter -= hit.normal * glm::dot(jitter, hit.normal);
            addDecal(wall, hit.collider, hit.point + jitter, hit.normal, spread * (0.2f + 0.3f * rand01()), kBlood);
        }
    }
}

void Effects::bloodDrip(const glm::vec3& p, const World&) {
    burst(Kind::Drop, 1, p, glm::vec3(0, -1, 0), kBlood, 0.3f, 0.3f, 0.009f, 0.009f, 2.0f, 1.0f, 1.0f, 0.2f);
}

void Effects::bloodPool(const glm::vec3& p, const World& world, float maxSize) {
    SegmentQuery q;
    q.staticOnly = true;
    WorldHit hit;
    if (!world.segment(p + glm::vec3(0, 0.3f, 0), p - glm::vec3(0, 1.5f, 0), hit, q)) return;
    addDecal(world.collider(hit.collider), hit.collider, hit.point, hit.normal, 0.05f, kBloodDark, maxSize, 0.025f);
}

void Effects::onWeaponEvent(const WeaponEvent& e) {
    if (e.type == WeaponEvent::Shot) {
        float s = e.smokeScale;
        for (int i = 0; i < 4; ++i) {
            Particle pt;
            pt.kind = Kind::Smoke;
            pt.position = e.position + e.direction * 0.05f;
            pt.velocity = e.velocity + e.direction * (2.0f + 3.0f * rand01()) * s + randUnit() * 0.3f;
            pt.color = glm::vec3(0.82f, 0.82f, 0.80f);
            pt.life = pt.maxLife = 0.8f + 0.5f * rand01();
            pt.size = 0.03f * s;
            pt.sizeEnd = (0.18f + 0.15f * rand01()) * s;
            pt.opacity = 0.22f;
            pt.gravity = -0.03f;
            pt.drag = 3.5f;
            pt.rotation = glm::quat(1, 0, 0, 0);
            pt.spin = glm::vec3(0.0f);
            m_particles.push_back(pt);
        }
        return;
    }

    // Ejected case
    Casing c;
    switch (e.casing) {
    case CasingType::Pistol: c.size = {0.019f, 0.0095f}; c.color = {0.80f, 0.62f, 0.28f}; break;
    case CasingType::Rifle: c.size = {0.045f, 0.0095f}; c.color = {0.80f, 0.62f, 0.28f}; break;
    case CasingType::Magnum: c.size = {0.051f, 0.012f}; c.color = {0.80f, 0.62f, 0.28f}; break;
    case CasingType::Shell: c.size = {0.07f, 0.020f}; c.color = {0.65f, 0.10f, 0.08f}; break;
    }
    c.position = e.position;
    c.velocity = e.velocity;
    c.rotation = lookRotation(e.direction);
    c.spin = randUnit() * (15.0f + 15.0f * rand01());
    c.life = 12.0f;
    c.resting = false;
    m_casings.push_back(c);
    if (m_casings.size() > kMaxCasings) m_casings.erase(m_casings.begin());

    Particle wisp;
    wisp.kind = Kind::Smoke;
    wisp.position = e.position;
    wisp.velocity = e.velocity * 0.2f;
    wisp.color = glm::vec3(0.85f);
    wisp.life = wisp.maxLife = 0.6f;
    wisp.size = 0.015f;
    wisp.sizeEnd = 0.08f;
    wisp.opacity = 0.15f;
    wisp.gravity = -0.05f;
    wisp.drag = 4.0f;
    wisp.rotation = glm::quat(1, 0, 0, 0);
    wisp.spin = glm::vec3(0.0f);
    m_particles.push_back(wisp);
}

void Effects::update(float dt, const World& world, const Ballistics& ballistics) {
    const float g = 9.81f;

    for (Particle& p : m_particles) {
        p.velocity.y -= g * p.gravity * dt;
        p.velocity *= std::exp(-p.drag * dt);
        p.position += p.velocity * dt;
        p.life -= dt;
        float spin = glm::length(p.spin);
        if (spin > 1e-3f) p.rotation = glm::normalize(glm::angleAxis(spin * dt, p.spin / spin) * p.rotation);
        if (p.kind == Kind::Drop && p.velocity != glm::vec3(0.0f)) {
            // Droplets stick where they land and leave a stain.
            SegmentQuery q;
            q.staticOnly = true;
            WorldHit hit;
            glm::vec3 prevPos = p.position - p.velocity * dt;
            if (world.segment(prevPos, p.position, hit, q)) {
                p.position = hit.point + hit.normal * 0.004f;
                p.velocity = glm::vec3(0.0f);
                p.gravity = 0.0f;
                p.life = std::min(p.life, 0.3f);
                if (rand01() < 0.6f)
                    addDecal(world.collider(hit.collider), hit.collider, hit.point, hit.normal,
                             0.015f + 0.03f * rand01(), kBlood);
            }
        }
        float ground = world.terrain() ? world.terrain()->height(p.position.x, p.position.z) : 0.0f;
        if ((p.kind == Kind::Debris || p.kind == Kind::Confetti) && p.position.y < ground && p.velocity.y < 0.0f) {
            p.position.y = ground;
            p.velocity.y *= -0.3f;
            p.velocity.x *= 0.5f;
            p.velocity.z *= 0.5f;
            p.spin *= 0.5f;
        }
    }
    m_particles.erase(std::remove_if(m_particles.begin(), m_particles.end(), [](const Particle& p) { return p.life <= 0.0f; }),
                      m_particles.end());

    for (Decal& d : m_decals)
        if (d.size < d.targetSize) d.size = std::min(d.targetSize, d.size + d.growRate * dt);

    // Cases tumble, bounce off whatever they hit and come to rest.
    for (Casing& c : m_casings) {
        c.life -= dt;
        if (c.resting) continue;
        c.velocity.y -= g * dt;
        glm::vec3 next = c.position + c.velocity * dt;
        WorldHit hit;
        if (world.segment(c.position, next, hit, -1, true)) {
            c.position = hit.point + hit.normal * 0.004f;
            float vn = glm::dot(c.velocity, hit.normal);
            glm::vec3 tangential = c.velocity - hit.normal * vn;
            c.velocity = tangential * 0.6f - hit.normal * vn * 0.35f;
            c.spin *= 0.5f;
            if (glm::length(c.velocity) < 0.35f && hit.normal.y > 0.7f) {
                c.resting = true;
                c.rotation = glm::angleAxis(rand01() * 6.28f, glm::vec3(0, 1, 0)); // lies on its side
                c.position.y = hit.point.y + c.size.y * 0.5f;
            }
        } else {
            c.position = next;
        }
        float spin = glm::length(c.spin);
        if (spin > 1e-3f) c.rotation = glm::normalize(glm::angleAxis(spin * dt, c.spin / spin) * c.rotation);
    }
    m_casings.erase(std::remove_if(m_casings.begin(), m_casings.end(), [](const Casing& c) { return c.life <= 0.0f; }),
                    m_casings.end());

    // Bullets in flight: streaks and the faint shock/vapour trail they leave.
    m_streaks.clear();
    for (VaporTrail& t : m_trails) t.bulletAlive = false;
    for (const Ballistics::Bullet& b : ballistics.bullets()) {
        m_streaks.push_back({b.position, b.velocity, b.origin, 0.0f});
        auto it = std::find_if(m_trails.begin(), m_trails.end(), [&](const VaporTrail& t) { return t.bullet == b.id; });
        if (it == m_trails.end()) {
            m_trails.push_back({b.id, {glm::vec4(b.origin, 0.0f)}, 0.008f + b.mass * 1.6f, true});
            it = m_trails.end() - 1;
        }
        it->bulletAlive = true;
        it->points.push_back(glm::vec4(b.position, 0.0f));
    }
    for (VaporTrail& t : m_trails) {
        for (glm::vec4& p : t.points) p.w += dt;
        auto firstAlive = std::find_if(t.points.begin(), t.points.end(), [](const glm::vec4& p) { return p.w < kVaporLife; });
        t.points.erase(t.points.begin(), firstAlive);
    }
    m_trails.erase(std::remove_if(m_trails.begin(), m_trails.end(),
                                  [](const VaporTrail& t) { return !t.bulletAlive && t.points.size() < 2; }),
                   m_trails.end());
}

void Effects::draw(Renderer& r, const Camera& camera, const World& world) const {
    const glm::vec3 cam = camera.position;
    auto glowSegment = [&](const glm::vec3& a, const glm::vec3& b, float width, const glm::vec3& color, float intensity) {
        r.draw(r.box, segmentMatrix(a, b, width), color, Pattern::Glow, glm::vec4(0, 0, 0, intensity), false);
    };

    // Vapour trails
    for (const VaporTrail& t : m_trails) {
        for (size_t i = 1; i < t.points.size(); ++i) {
            glm::vec3 a(t.points[i - 1]), b(t.points[i]);
            float age = 0.5f * (t.points[i - 1].w + t.points[i].w);
            float fade = 1.0f - age / kVaporLife;
            if (fade <= 0.0f) continue;
            float dist = glm::length(b - cam);
            float nearFade = std::clamp((dist - 2.0f) / 10.0f, 0.0f, 1.0f);
            float w = std::max(t.width * (1.0f + age * 6.0f), dist * 0.0010f);
            glowSegment(a, b, w, glm::vec3(0.85f, 0.88f, 0.92f), 0.14f * fade * nearFade);
        }
    }

    // Bullet streaks: the path covered over the last ~12 ms, drawn like a hot tracer.
    for (const Streak& s : m_streaks) {
        float speed = glm::length(s.velocity);
        if (speed < 1.0f) continue;
        glm::vec3 dir = s.velocity / speed;
        float travelled = glm::length(s.position - s.origin);
        float back = std::min(speed * 0.012f, travelled);
        // Fade in over the first metres so the shooter isn't looking down a solid beam.
        float fadeIn = std::clamp((travelled - 2.0f) / 15.0f, 0.0f, 1.0f);
        if (back < 0.05f || fadeIn <= 0.0f) continue;
        float dist = glm::length(s.position - cam);
        float w = std::max(0.004f, dist * 0.0015f);
        glowSegment(s.position - dir * back, s.position, w, glm::vec3(1.0f, 0.72f, 0.38f), 1.4f * fadeIn);
    }

    for (const Particle& p : m_particles) {
        float t = 1.0f - p.life / p.maxLife;
        float dist = glm::length(p.position - cam);
        switch (p.kind) {
        case Kind::Debris:
            r.draw(r.box, trs(p.position, p.rotation, glm::vec3(p.size, p.size * 0.6f, p.size * 1.4f)), p.color,
                   Pattern::Plain, glm::vec4(0.0f), false);
            break;
        case Kind::Drop:
            r.draw(r.sphere, trs(p.position, p.rotation, glm::vec3(p.size)), p.color, Pattern::Plain, glm::vec4(0.0f), false);
            break;
        case Kind::Confetti:
            r.draw(r.box, trs(p.position, p.rotation, glm::vec3(p.size, p.size * 0.1f, p.size)), p.color,
                   Pattern::Plain, glm::vec4(0.0f), false);
            break;
        case Kind::Spark:
            glowSegment(p.position - p.velocity * 0.015f, p.position, std::max(0.003f, dist * 0.0008f), p.color,
                        p.opacity * (1.0f - t));
            break;
        case Kind::Flash: {
            float s = glm::mix(p.size, p.sizeEnd, t);
            float k = 1.0f - t;
            r.draw(r.sphere, trs(p.position, glm::quat(1, 0, 0, 0), glm::vec3(s)), p.color, Pattern::Glow,
                   glm::vec4(0, 0, 0, p.opacity * k * k), false);
            break;
        }
        case Kind::Smoke: {
            float s = glm::mix(p.size, p.sizeEnd, std::sqrt(t));
            float fadeIn = std::min(1.0f, (p.maxLife - p.life) / 0.06f);
            float alpha = p.opacity * fadeIn * (1.0f - t);
            r.draw(r.sphere, trs(p.position, glm::quat(1, 0, 0, 0), glm::vec3(s)), p.color, Pattern::Smoke,
                   glm::vec4(0, 0, 0, alpha), false);
            break;
        }
        }
    }

    for (const Casing& c : m_casings)
        r.draw(r.cylinder, trs(c.position, c.rotation, glm::vec3(c.size.y, c.size.y, c.size.x)), c.color,
               Pattern::Plain, glm::vec4(0.0f), false);

    // Decals follow their collider (plates swing, the marks go with them).
    const auto& colliders = world.colliders();
    for (const Decal& d : m_decals) {
        if (d.collider < 0 || d.collider >= int(colliders.size())) continue;
        const OBB& box = colliders[d.collider].box;
        glm::vec3 wp = box.center + box.rotation * d.localPos;
        if (glm::length(wp - cam) > 160.0f) continue;
        glm::vec3 wn = box.rotation * d.localNormal;
        float offset = 0.002f + glm::length(wp - cam) * 0.00012f; // beat depth precision at range
        r.draw(r.cylinder, trs(wp + wn * offset, lookRotation(wn), glm::vec3(d.size, d.size, 0.001f)), d.color,
               Pattern::Plain, glm::vec4(0.0f), false);
    }
}
