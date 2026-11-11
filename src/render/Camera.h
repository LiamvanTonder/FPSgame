#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Y-up, right-handed. yaw = 0 looks down -Z; positive yaw turns left; positive pitch looks up.
struct Camera {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f; // positive rolls the view clockwise (leaning right)
    float fovY = glm::radians(70.0f);
    float nearPlane = 0.05f;
    float farPlane = 1500.0f;
    float aspect = 16.0f / 9.0f;

    // When set, the camera looks along orientation's -Z (with its roll) instead of yaw/pitch.
    bool useOrientation = false;
    glm::quat orientation{1, 0, 0, 0};

    glm::vec3 forward() const;
    glm::vec3 right() const;
    glm::vec3 up() const;
    glm::mat4 view() const;
    glm::mat4 projection() const;
    glm::mat4 viewProjection() const { return projection() * view(); }
};

glm::vec3 directionFromYawPitch(float yaw, float pitch);
