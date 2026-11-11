#include "game/ArmModel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

glm::vec3 rotationVector(glm::quat q) {
    if (q.w < 0.0f) q = -q;
    glm::vec3 v(q.x, q.y, q.z);
    float s = glm::length(v);
    if (s < 1e-7f) return v * 2.0f;
    return v / s * (2.0f * std::atan2(s, q.w));
}

glm::quat lookRotation(const glm::vec3& dir) {
    glm::vec3 f = glm::normalize(dir);
    glm::vec3 up = std::abs(f.y) > 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    glm::vec3 r = glm::normalize(glm::cross(f, up));
    glm::vec3 u = glm::cross(r, f);
    glm::mat3 m(r, u, -f); // columns: local X, Y, Z in world space
    return glm::normalize(glm::quat_cast(m));
}

static glm::vec3 clampLength(const glm::vec3& v, float maxLen) {
    float len = glm::length(v);
    return len > maxLen && len > 0.0f ? v * (maxLen / len) : v;
}

void ArmModel::reset(const glm::vec3& p, const glm::quat& q) {
    pivot = prevPivot = p;
    orientation = prevOrientation = q;
    pivotVelocity = angularVelocity = glm::vec3(0.0f);
    m_lastTarget = q;
    m_targetOmegaFiltered = glm::vec3(0.0f);
    m_hasTarget = true;
}

void ArmModel::step(const glm::vec3& anchor, const glm::vec3& anchorVelocity, const glm::quat& target,
                    const ArmParams& p, float dt) {
    prevOrientation = orientation;
    prevPivot = pivot;

    // --- Orientation -------------------------------------------------------
    // Estimate how fast the aim point is moving (for predictive tracking).
    if (!m_hasTarget) {
        m_lastTarget = target;
        m_hasTarget = true;
    }
    glm::vec3 targetOmega = rotationVector(target * glm::inverse(m_lastTarget)) / dt;
    m_lastTarget = target;
    targetOmega = clampLength(targetOmega, p.maxAngularSpeed);
    float filter = 1.0f - std::exp(-dt / 0.03f); // input arrives per rendered frame; smooth it
    m_targetOmegaFiltered += (targetOmega - m_targetOmegaFiltered) * filter;

    glm::vec3 error = rotationVector(target * glm::inverse(orientation));
    float err = glm::length(error);
    errorAngle = err;

    // Desired angular velocity: time-optimal profile limited by top speed, by the
    // ability to brake in time, and by a linear settle near the target.
    float speed = std::min({p.maxAngularSpeed, std::sqrt(2.0f * p.maxAngularAccel * p.brakeFraction * err),
                            p.settleRate * err});
    glm::vec3 desired = err > 1e-7f ? error * (speed / err) : glm::vec3(0.0f);

    // Prediction only helps when already roughly on target (tracking, not flicking).
    float ffWeight = p.feedForward * std::clamp(1.0f - err / 0.35f, 0.0f, 1.0f);
    desired = clampLength(desired + m_targetOmegaFiltered * ffWeight, p.maxAngularSpeed);

    glm::vec3 accel = clampLength((desired - angularVelocity) * p.velocityGain, p.maxAngularAccel);
    angularVelocity += accel * dt;

    float angle = glm::length(angularVelocity) * dt;
    if (angle > 1e-9f)
        orientation = glm::normalize(glm::angleAxis(angle, glm::normalize(angularVelocity)) * orientation);

    // --- Position (stock held against the shoulder by a spring-damper) ----
    glm::vec3 linAccel = p.positionStiffness * (anchor - pivot) + p.positionDamping * (anchorVelocity - pivotVelocity);
    pivotVelocity += linAccel * dt;
    pivot += pivotVelocity * dt;
}

bool ArmModel::selfTest() {
    ArmParams p{7.0f, 60.0f, 0.7f, 14.0f, 45.0f, 0.9f, 250.0f, 24.0f};
    ArmModel arm;
    arm.reset(glm::vec3(0.0f), lookRotation(glm::vec3(0, 0, -1)));

    const float stepAngle = glm::radians(90.0f);
    glm::quat target = lookRotation(glm::vec3(-std::sin(stepAngle), 0.0f, -std::cos(stepAngle)));
    const float dt = 1.0f / 240.0f;

    float maxOmega = 0.0f, maxOvershoot = 0.0f, settleTime = -1.0f;
    for (int i = 0; i < 480; ++i) {
        arm.step(glm::vec3(0.0f), glm::vec3(0.0f), target, p, dt);
        maxOmega = std::max(maxOmega, glm::length(arm.angularVelocity));
        glm::vec3 fwd = arm.orientation * glm::vec3(0, 0, -1);
        float yaw = std::atan2(-fwd.x, -fwd.z);
        maxOvershoot = std::max(maxOvershoot, yaw - stepAngle);
        if (settleTime < 0.0f && arm.errorAngle < glm::radians(1.0f)) settleTime = (i + 1) * dt;
    }
    bool ok = maxOvershoot < stepAngle * 0.05f && maxOmega <= p.maxAngularSpeed * 1.01f && arm.errorAngle < 1e-3f;
    std::printf("[ArmModel self-test] 90deg step: settle(<1deg) %.3f s, peak %.0f deg/s, overshoot %.2f deg -> %s\n",
                settleTime, glm::degrees(maxOmega), glm::degrees(std::max(maxOvershoot, 0.0f)), ok ? "PASS" : "FAIL");
    return ok;
}
