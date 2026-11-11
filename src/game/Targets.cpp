#include "game/Targets.h"

#include "render/Renderer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace {
const float kSteelDensity = 7850.0f;
const float kChainLength = 0.3f;
const float kKnockdownLean = 0.08f;   // rests leaning toward the shooter against a stop
const float kKnockdownFallen = -1.45f;
const float kResetDelay = 3.0f;

const glm::vec3 kWood(0.45f, 0.32f, 0.20f);
const glm::vec3 kDarkSteel(0.22f, 0.23f, 0.25f);
const glm::vec3 kPlatePaint(0.90f, 0.90f, 0.86f);
const glm::vec3 kPlateFlash(1.0f, 0.45f, 0.1f);


glm::mat3 rotationX(float a) {
    return glm::mat3(glm::rotate(glm::mat4(1.0f), a, glm::vec3(1, 0, 0)));
}
} // namespace

void Targets::addProp(World& world, const OBB& box, const glm::vec3& color, Surface surface) {
    m_props.push_back({box, color});
    Collider c;
    c.box = box;
    c.surface = surface;
    world.add(c);
}

void Targets::addPaper(World& world, const std::string& name, const glm::vec3& faceCenter) {
    PaperTarget t;
    t.name = name;
    t.center = faceCenter;

    Collider c;
    c.box = OBB::fromCenterSize(faceCenter, glm::vec3(t.size, 0.004f));
    c.surface = Surface::Paper;
    c.penetrable = true;
    c.blocksPlayer = false;
    c.paperTarget = int(papers.size());
    t.collider = world.add(c);

    // Two wooden uprights and a crossbar under the sheet.
    float postX = t.size.x * 0.5f + 0.03f;
    float top = faceCenter.y + t.size.y * 0.5f;
    for (float side : {-1.0f, 1.0f}) {
        glm::vec3 mn(faceCenter.x + side * postX - 0.02f, 0.0f, faceCenter.z - 0.025f);
        addProp(world, OBB::fromMinMax(mn, mn + glm::vec3(0.04f, top, 0.04f)), kWood, Surface::Wood);
    }
    float barY = faceCenter.y - t.size.y * 0.5f - 0.03f;
    addProp(world, OBB::fromCenterSize(glm::vec3(faceCenter.x, barY, faceCenter.z - 0.005f), glm::vec3(postX * 2.0f, 0.04f, 0.03f)),
            kWood, Surface::Wood);
    papers.push_back(t);
}

void Targets::addKnockdown(World& world, const std::string& name, const glm::vec3& hinge, const glm::vec2& size) {
    SteelPlate p;
    p.name = name;
    p.hinge = hinge;
    p.size = size;
    p.thickness = 0.012f;
    p.hanging = false;
    p.restAngle = p.angle = kKnockdownLean;
    p.mass = kSteelDensity * size.x * size.y * p.thickness;

    Collider c;
    c.box = plateBox(p);
    c.surface = Surface::Steel;
    c.dynamic = true;
    c.steelPlate = int(plates.size());
    p.collider = world.add(c);

    // Stand: a steel post under the hinge on a wide base.
    addProp(world, OBB::fromCenterSize(glm::vec3(hinge.x, hinge.y * 0.5f, hinge.z), glm::vec3(0.08f, hinge.y, 0.08f)),
            kDarkSteel, Surface::Steel);
    addProp(world, OBB::fromCenterSize(glm::vec3(hinge.x, 0.02f, hinge.z), glm::vec3(0.6f, 0.04f, 0.4f)), kDarkSteel,
            Surface::Steel);
    plates.push_back(p);
}

void Targets::addGong(World& world, const std::string& name, const glm::vec3& hinge, const glm::vec2& size) {
    SteelPlate p;
    p.name = name;
    p.hinge = hinge;
    p.size = size;
    p.thickness = 0.010f;
    p.hanging = true;
    p.mass = kSteelDensity * size.x * size.y * p.thickness;

    Collider c;
    c.box = plateBox(p);
    c.surface = Surface::Steel;
    c.dynamic = true;
    c.steelPlate = int(plates.size());
    p.collider = world.add(c);

    // A-frame: two posts and a top beam.
    float postX = size.x * 0.5f + 0.35f;
    float beamY = hinge.y + 0.05f;
    for (float side : {-1.0f, 1.0f}) {
        glm::vec3 c0(hinge.x + side * postX, beamY * 0.5f, hinge.z);
        addProp(world, OBB::fromCenterSize(c0, glm::vec3(0.08f, beamY, 0.08f)), kWood, Surface::Wood);
    }
    addProp(world, OBB::fromCenterSize(glm::vec3(hinge.x, beamY + 0.04f, hinge.z), glm::vec3(postX * 2.0f + 0.08f, 0.08f, 0.08f)),
            kWood, Surface::Wood);
    plates.push_back(p);
}

glm::vec3 Targets::plateCenterLocal(const SteelPlate& p) {
    return p.hanging ? glm::vec3(0.0f, -(kChainLength + p.size.y * 0.5f), 0.0f) : glm::vec3(0.0f, p.size.y * 0.5f, 0.0f);
}

OBB Targets::plateBox(const SteelPlate& p) {
    glm::mat3 r = rotationX(p.angle);
    return OBB{p.hinge + r * plateCenterLocal(p), glm::vec3(p.size.x, p.size.y, p.thickness) * 0.5f, r};
}

TargetHitResult Targets::hitPaper(int index, const glm::vec3& point) {
    PaperTarget& t = papers[index];
    glm::vec2 local(point.x - t.center.x, point.y - t.center.y);
    t.holes.push_back(local);
    if (t.holes.size() > 300) t.holes.erase(t.holes.begin());

    float r = glm::length(local);
    int ring = r < 0.05f ? 10 : r < 0.10f ? 9 : r < 0.15f ? 8 : r < 0.20f ? 7 : r < 0.25f ? 6 : 5;
    t.score += ring;
    t.hits++;
    return {t.name, ring};
}

TargetHitResult Targets::hitPlate(int index, const glm::vec3& point, const glm::vec3& impulse) {
    SteelPlate& p = plates[index];
    glm::vec3 c = plateCenterLocal(p);
    float d2 = glm::dot(c, c);
    float inertia = p.mass * (d2 + p.size.y * p.size.y / 12.0f); // parallel-axis about the hinge
    float torque = glm::cross(point - p.hinge, impulse).x;
    p.angularVelocity += torque / inertia;
    p.flash = 0.25f;
    p.hits++;
    return {p.name, -1};
}

void Targets::update(float dt, World& world) {
    const float g = 9.81f;
    for (SteelPlate& p : plates) {
        p.flash = std::max(0.0f, p.flash - dt);
        glm::vec3 c = plateCenterLocal(p);
        float inertia = p.mass * (glm::dot(c, c) + p.size.y * p.size.y / 12.0f);

        if (p.resetting) {
            p.angle = std::min(p.restAngle, p.angle + dt * 2.5f);
            p.angularVelocity = 0.0f;
            if (p.angle >= p.restAngle) p.resetting = false;
        } else {
            glm::vec3 r = rotationX(p.angle) * c;
            float gravityTorque = p.mass * g * r.z; // (r x (0,-mg,0)).x
            p.angularVelocity += gravityTorque / inertia * dt;
            p.angularVelocity *= std::exp(-(p.hanging ? 0.35f : 0.8f) * dt);
            p.angle += p.angularVelocity * dt;

            if (p.hanging) {
                p.angle = std::clamp(p.angle, -1.3f, 1.3f);
            } else {
                if (p.angle > p.restAngle) { // against the forward stop
                    p.angle = p.restAngle;
                    if (p.angularVelocity > 0.0f) p.angularVelocity *= -0.15f;
                }
                if (p.angle < kKnockdownFallen) { // lying on its back
                    p.angle = kKnockdownFallen;
                    p.angularVelocity = 0.0f;
                }
                if (p.angle < -1.2f) {
                    p.downTimer += dt;
                    if (p.downTimer > kResetDelay) {
                        p.resetting = true;
                        p.downTimer = 0.0f;
                    }
                } else {
                    p.downTimer = 0.0f;
                }
            }
        }
        world.collider(p.collider).box = plateBox(p);
    }
}


void Targets::draw(Renderer& r) const {
    for (const Prop& prop : m_props) r.drawBox(prop.box, prop.color);

    for (const PaperTarget& t : papers) {
        OBB face = OBB::fromCenterSize(t.center, glm::vec3(t.size, 0.004f));
        r.drawBox(face, glm::vec3(0.93f, 0.91f, 0.84f), Pattern::TargetRings, glm::vec4(t.center, 0.0f));
        for (const glm::vec2& h : t.holes) {
            glm::mat4 m = glm::translate(glm::mat4(1.0f), t.center + glm::vec3(h, 0.0025f));
            m = glm::scale(m, glm::vec3(0.012f, 0.012f, 0.002f));
            r.draw(r.cylinder, m, glm::vec3(0.35f, 0.05f, 0.05f), Pattern::Unlit, glm::vec4(0.0f), false);
        }
    }

    for (const SteelPlate& p : plates) {
        float flash = std::min(1.0f, p.flash * 4.0f);
        r.drawBox(plateBox(p), glm::mix(kPlatePaint, kPlateFlash, flash));

        glm::mat4 hingeM = glm::translate(glm::mat4(1.0f), p.hinge);
        hingeM = glm::rotate(hingeM, glm::half_pi<float>(), glm::vec3(0, 1, 0));
        hingeM = glm::scale(hingeM, glm::vec3(0.03f, 0.03f, p.size.x + (p.hanging ? 0.6f : 0.1f)));
        r.draw(r.cylinder, hingeM, kDarkSteel);

        if (p.hanging) {
            glm::mat3 rot = rotationX(p.angle);
            for (float side : {-1.0f, 1.0f}) {
                glm::vec3 local(side * p.size.x * 0.4f, -kChainLength * 0.5f, 0.0f);
                OBB chain{p.hinge + rot * local, glm::vec3(0.006f, kChainLength * 0.5f, 0.006f), rot};
                r.drawBox(chain, kDarkSteel);
            }
        }
    }
}

void Targets::clearPaper() {
    for (PaperTarget& t : papers) {
        t.holes.clear();
        t.score = 0;
        t.hits = 0;
    }
}

int Targets::totalScore() const {
    int s = 0;
    for (const PaperTarget& t : papers) s += t.score;
    return s;
}
