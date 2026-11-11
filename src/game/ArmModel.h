#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Physical limits of the shooter's arms/shoulders moving the rifle.
// Angular values in radians.
struct ArmParams {
    float maxAngularSpeed;  // peak swing rate the arms can sustain
    float maxAngularAccel;  // torque limit / rifle+arm inertia
    float brakeFraction;    // fraction of max accel planned for decelerating (margin -> no overshoot)
    float settleRate;       // 1/s, final approach rate near the target
    float velocityGain;     // 1/s, how hard muscles correct angular velocity error
    float feedForward;      // 0..1, how well the shooter predicts a moving aim point
    float positionStiffness; // shoulder/arm spring holding the stock in place (1/s^2)
    float positionDamping;   // (1/s)
};

// The rifle is a rigid body hinged at the stock (shoulder pocket).
// It is driven toward a target orientation/position by a rate- and torque-limited
// controller, so it lags behind fast camera motion, decelerates physically and settles.
class ArmModel {
public:
    void reset(const glm::vec3& pivot, const glm::quat& orientation);

    void step(const glm::vec3& anchor, const glm::vec3& anchorVelocity, const glm::quat& target,
              const ArmParams& p, float dt);

    // Instantaneous changes (recoil, footsteps, landing).
    void addAngularVelocity(const glm::vec3& dOmegaWorld) { angularVelocity += dOmegaWorld; }
    void addLinearVelocity(const glm::vec3& dv) { pivotVelocity += dv; }

    // World-space velocity of a rigidly attached point.
    glm::vec3 pointVelocity(const glm::vec3& worldPoint) const {
        return pivotVelocity + glm::cross(angularVelocity, worldPoint - pivot);
    }

    glm::quat orientation{1, 0, 0, 0};
    glm::vec3 angularVelocity{0.0f};
    glm::vec3 pivot{0.0f};
    glm::vec3 pivotVelocity{0.0f};

    glm::quat prevOrientation{1, 0, 0, 0};
    glm::vec3 prevPivot{0.0f};

    float errorAngle = 0.0f; // radians between current and target orientation

    // Debug-build check that a step response converges without large overshoot.
    static bool selfTest();

private:
    glm::quat m_lastTarget{1, 0, 0, 0};
    glm::vec3 m_targetOmegaFiltered{0.0f};
    bool m_hasTarget = false;
};

// Rotation vector (axis * angle) of the shortest rotation represented by q.
glm::vec3 rotationVector(glm::quat q);
// Rotation that makes local -Z point along dir with no roll (world up = +Y).
glm::quat lookRotation(const glm::vec3& dir);
