#include "game/Ragdoll.h"

#include "world/Terrain.h"

#include "physics/World.h"

#include <algorithm>
#include <cmath>

namespace {
const glm::vec3 kGravity(0.0f, -9.81f, 0.0f);
const float kParticleRadius = 0.06f;
const int kIterations = 6;
const float kDamping = 0.995f;
} // namespace

float jointMass(int joint) {
    switch (joint) {
    case J_Head: return 5.0f;
    case J_Neck: return 2.0f;
    case J_Chest: return 18.0f;
    case J_Pelvis: return 14.0f;
    case J_LShoulder: case J_RShoulder: return 3.5f;
    case J_LElbow: case J_RElbow: return 2.0f;
    case J_LHand: case J_RHand: return 0.8f;
    case J_LHip: case J_RHip: return 5.0f;
    case J_LKnee: case J_RKnee: return 4.0f;
    case J_LFoot: case J_RFoot: return 1.7f;
    default: return 2.0f;
    }
}

void Ragdoll::addBone(int a, int b, float stiffness) {
    m_constraints.push_back({a, b, glm::length(m_pos[a] - m_pos[b]), stiffness, false});
}

void Ragdoll::addLimit(int a, int b, float fraction) {
    m_constraints.push_back({a, b, glm::length(m_pos[a] - m_pos[b]) * fraction, 1.0f, true});
}

void Ragdoll::init(const JointArray& positions, const JointArray& velocities, float dt) {
    m_pos = positions;
    for (int i = 0; i < kJointCount; ++i) {
        m_prev[i] = positions[i] - velocities[i] * dt;
        m_invMass[i] = 1.0f / jointMass(i);
    }
    m_dt = dt;
    m_asleep = false;
    m_stillTime = 0.0f;

    m_constraints.clear();
    // Bones
    addBone(J_Head, J_Neck);
    addBone(J_Neck, J_Chest);
    addBone(J_Chest, J_Pelvis);
    addBone(J_Neck, J_LShoulder);
    addBone(J_Neck, J_RShoulder);
    addBone(J_LShoulder, J_LElbow);
    addBone(J_RShoulder, J_RElbow);
    addBone(J_LElbow, J_LHand);
    addBone(J_RElbow, J_RHand);
    addBone(J_Pelvis, J_LHip);
    addBone(J_Pelvis, J_RHip);
    addBone(J_LHip, J_LKnee);
    addBone(J_RHip, J_RKnee);
    addBone(J_LKnee, J_LFoot);
    addBone(J_RKnee, J_RFoot);
    // Torso braces: shoulders, chest and hips form a nearly rigid block
    addBone(J_LShoulder, J_RShoulder);
    addBone(J_LHip, J_RHip);
    addBone(J_LShoulder, J_Chest);
    addBone(J_RShoulder, J_Chest);
    addBone(J_LHip, J_Chest);
    addBone(J_RHip, J_Chest);
    addBone(J_LShoulder, J_Pelvis, 0.6f);
    addBone(J_RShoulder, J_Pelvis, 0.6f);
    addBone(J_LShoulder, J_RHip, 0.5f);
    addBone(J_RShoulder, J_LHip, 0.5f);
    addBone(J_Head, J_Chest, 0.4f); // neck stiffness
    // Joint limits: limbs and neck can bend but not fold back onto themselves
    addLimit(J_LHip, J_LFoot, 0.45f);
    addLimit(J_RHip, J_RFoot, 0.45f);
    addLimit(J_LShoulder, J_LHand, 0.35f);
    addLimit(J_RShoulder, J_RHand, 0.35f);
    addLimit(J_Head, J_Pelvis, 0.85f);
    addLimit(J_Head, J_LShoulder, 0.8f);
    addLimit(J_Head, J_RShoulder, 0.8f);
    addLimit(J_LKnee, J_RKnee, 0.3f);
    addLimit(J_LFoot, J_RFoot, 0.2f);
}

void Ragdoll::applyImpulse(const glm::vec3& point, const glm::vec3& impulse) {
    // Spread over the three nearest particles, weighted toward the closest.
    std::array<std::pair<float, int>, kJointCount> d;
    for (int i = 0; i < kJointCount; ++i) d[i] = {glm::length(m_pos[i] - point), i};
    std::partial_sort(d.begin(), d.begin() + 3, d.end());
    float wsum = 0.0f;
    float w[3];
    for (int k = 0; k < 3; ++k) {
        w[k] = 1.0f / (d[k].first + 0.05f);
        wsum += w[k];
    }
    for (int k = 0; k < 3; ++k) {
        int i = d[k].second;
        glm::vec3 dv = impulse * (w[k] / wsum) * m_invMass[i];
        m_prev[i] -= dv * m_dt; // Verlet: velocity = (pos - prev) / dt
    }
    m_asleep = false;
    m_stillTime = 0.0f;
}

void Ragdoll::collide(const World& world) {
    glm::vec3 mn(1e9f), mx(-1e9f);
    for (const glm::vec3& p : m_pos) {
        mn = glm::min(mn, p);
        mx = glm::max(mx, p);
    }
    AABB bounds{mn - glm::vec3(kParticleRadius), mx + glm::vec3(kParticleRadius)};

    static std::vector<int> list;
    world.overlap(bounds, list);
    for (int ci : list) {
        const Collider& c = world.collider(ci);
        if (!c.active || c.owner != kOwnerWorld || c.penetrable || !c.blocksPlayer) continue;
        if (!bounds.overlaps(c.box.bounds())) continue;
        for (int i = 0; i < kJointCount; ++i) {
            glm::vec3 local = c.box.toLocal(m_pos[i]);
            glm::vec3 ext = c.box.half + glm::vec3(kParticleRadius);
            glm::vec3 pen = ext - glm::abs(local);
            if (pen.x <= 0.0f || pen.y <= 0.0f || pen.z <= 0.0f) continue;
            // Push out along the axis of least penetration.
            int axis = pen.x < pen.y ? (pen.x < pen.z ? 0 : 2) : (pen.y < pen.z ? 1 : 2);
            glm::vec3 nLocal(0.0f);
            nLocal[axis] = local[axis] >= 0.0f ? 1.0f : -1.0f;
            glm::vec3 n = c.box.rotation * nLocal;
            m_pos[i] += n * pen[axis];
            // Friction: kill most of the tangential motion on contact.
            glm::vec3 vel = m_pos[i] - m_prev[i];
            glm::vec3 vn = n * glm::dot(vel, n);
            glm::vec3 vt = vel - vn;
            m_prev[i] = m_pos[i] - (vt * 0.4f + (glm::dot(vel, n) < 0.0f ? -vn * 0.1f : vn));
        }
    }
    if (const Terrain* t = world.terrain()) {
        for (int i = 0; i < kJointCount; ++i) {
            float h = t->height(m_pos[i].x, m_pos[i].z) + kParticleRadius;
            if (m_pos[i].y >= h) continue;
            glm::vec3 n = t->normal(m_pos[i].x, m_pos[i].z);
            m_pos[i].y = h;
            glm::vec3 vel = m_pos[i] - m_prev[i];
            glm::vec3 vn = n * glm::dot(vel, n);
            glm::vec3 vt = vel - vn;
            m_prev[i] = m_pos[i] - (vt * 0.4f + (glm::dot(vel, n) < 0.0f ? -vn * 0.1f : vn));
        }
    }
}

void Ragdoll::step(float dt, const World& world) {
    if (m_asleep) return;
    m_dt = dt;
    float maxMove = 0.0f;
    for (int i = 0; i < kJointCount; ++i) {
        glm::vec3 vel = (m_pos[i] - m_prev[i]) * kDamping;
        m_prev[i] = m_pos[i];
        m_pos[i] += vel + kGravity * dt * dt;
        maxMove = std::max(maxMove, glm::length(vel));
    }
    for (int it = 0; it < kIterations; ++it) {
        for (const Constraint& c : m_constraints) {
            glm::vec3 delta = m_pos[c.b] - m_pos[c.a];
            float len = glm::length(delta);
            if (len < 1e-6f) continue;
            if (c.minOnly && len >= c.rest) continue;
            float wa = m_invMass[c.a], wb = m_invMass[c.b];
            float diff = (len - c.rest) / (len * (wa + wb)) * c.stiffness;
            m_pos[c.a] += delta * (wa * diff);
            m_pos[c.b] -= delta * (wb * diff);
        }
        collide(world);
    }

    // Sleep once the body has settled.
    if (maxMove < 0.0004f) {
        m_stillTime += dt;
        if (m_stillTime > 1.0f) m_asleep = true;
    } else {
        m_stillTime = 0.0f;
    }
}

float Ragdoll::maxBoneError() const {
    float worst = 0.0f;
    for (const Constraint& c : m_constraints) {
        if (c.minOnly || c.stiffness < 1.0f) continue;
        float len = glm::length(m_pos[c.b] - m_pos[c.a]);
        worst = std::max(worst, std::abs(len - c.rest) / c.rest);
    }
    return worst;
}
