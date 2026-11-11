#pragma once

#include <glm/glm.hpp>
#include <array>
#include <vector>

class World;

// Skeleton joints shared by the animated body and the ragdoll.
enum Joint {
    J_Head, J_Neck, J_Chest, J_Pelvis,
    J_LShoulder, J_RShoulder, J_LElbow, J_RElbow, J_LHand, J_RHand,
    J_LHip, J_RHip, J_LKnee, J_RKnee, J_LFoot, J_RFoot,
    kJointCount
};

using JointArray = std::array<glm::vec3, kJointCount>;

// Verlet particle ragdoll (position-based): particles at the joints, distance
// constraints for bones, braces that keep the torso rigid, and minimum-distance
// limits that stop limbs folding through themselves.
class Ragdoll {
public:
    // Starts from the animated pose with per-joint velocities so the transition is seamless.
    void init(const JointArray& positions, const JointArray& velocities, float dt);
    void step(float dt, const World& world);
    void applyImpulse(const glm::vec3& point, const glm::vec3& impulse);

    const JointArray& joints() const { return m_pos; }
    bool asleep() const { return m_asleep; }
    // Largest relative bone-length error (for self-tests).
    float maxBoneError() const;

private:
    struct Constraint {
        int a, b;
        float rest;
        float stiffness;
        bool minOnly; // only resists compression (joint limits)
    };
    void addBone(int a, int b, float stiffness = 1.0f);
    void addLimit(int a, int b, float fraction);
    void collide(const World& world);

    JointArray m_pos{}, m_prev{};
    std::array<float, kJointCount> m_invMass{};
    std::vector<Constraint> m_constraints;
    float m_dt = 1.0f / 240.0f;
    float m_stillTime = 0.0f;
    bool m_asleep = false;
};

// Mass of each joint particle (kg), ~80 kg in total.
float jointMass(int joint);
