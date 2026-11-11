#include "physics/Collision.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

AABB OBB::bounds() const {
    glm::vec3 extent(0.0f);
    for (int axis = 0; axis < 3; ++axis)
        extent += glm::abs(rotation[axis]) * half[axis];
    return AABB{center - extent, center + extent};
}

glm::mat4 OBB::modelMatrix() const {
    glm::mat4 m(rotation);
    m[3] = glm::vec4(center, 1.0f);
    return glm::scale(m, half * 2.0f);
}

bool intersectSegmentOBB(const glm::vec3& a, const glm::vec3& b, const OBB& box, float& tOut, glm::vec3& normalOut) {
    glm::mat3 invRot = glm::transpose(box.rotation);
    glm::vec3 p = invRot * (a - box.center);
    glm::vec3 d = invRot * (b - a);

    float tEnter = 0.0f, tExit = 1.0f;
    int enterAxis = -1;
    float enterSign = 0.0f;

    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-9f) {
            if (std::abs(p[i]) > box.half[i]) return false;
            continue;
        }
        float inv = 1.0f / d[i];
        float t1 = (-box.half[i] - p[i]) * inv;
        float t2 = (box.half[i] - p[i]) * inv;
        float sign = -1.0f; // entering through the negative face
        if (t1 > t2) {
            std::swap(t1, t2);
            sign = 1.0f;
        }
        if (t1 > tEnter) {
            tEnter = t1;
            enterAxis = i;
            enterSign = sign;
        }
        tExit = std::min(tExit, t2);
        if (tEnter > tExit) return false;
    }

    tOut = tEnter;
    if (enterAxis < 0) {
        // Started inside the box.
        float len = glm::length(b - a);
        normalOut = len > 1e-9f ? -(b - a) / len : glm::vec3(0, 1, 0);
    } else {
        glm::vec3 nLocal(0.0f);
        nLocal[enterAxis] = enterSign;
        normalOut = box.rotation * nLocal;
    }
    return true;
}
