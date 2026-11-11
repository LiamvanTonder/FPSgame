#include "render/Camera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

glm::vec3 directionFromYawPitch(float yaw, float pitch) {
    return glm::vec3(-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch));
}

glm::vec3 Camera::forward() const {
    return useOrientation ? orientation * glm::vec3(0, 0, -1) : directionFromYawPitch(yaw, pitch);
}

glm::vec3 Camera::right() const {
    return useOrientation ? orientation * glm::vec3(1, 0, 0) : glm::vec3(std::cos(yaw), 0.0f, -std::sin(yaw));
}

glm::vec3 Camera::up() const { return glm::cross(right(), forward()); }

glm::mat4 Camera::view() const {
    if (useOrientation) {
        glm::mat4 rot = glm::mat4_cast(glm::conjugate(orientation));
        return rot * glm::translate(glm::mat4(1.0f), -position);
    }
    glm::vec3 f = forward();
    glm::vec3 up = glm::vec3(0, 1, 0);
    if (roll != 0.0f) up = glm::angleAxis(roll, f) * up; // rotating up about forward rolls the image
    return glm::lookAt(position, position + f, up);
}

glm::mat4 Camera::projection() const {
    return glm::perspective(fovY, aspect, nearPlane, farPlane);
}
