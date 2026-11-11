#pragma once

#include "physics/World.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

class Renderer;

struct PaperTarget {
    std::string name;
    glm::vec3 center{0.0f}; // centre of the face, facing +Z (toward the firing line)
    glm::vec2 size{0.6f, 0.6f};
    int collider = -1;
    std::vector<glm::vec2> holes; // face-local coordinates
    int score = 0;
    int hits = 0;
};

// Steel plate on a horizontal hinge (X axis), simulated as a rigid pendulum.
struct SteelPlate {
    std::string name;
    glm::vec3 hinge{0.0f};
    glm::vec2 size{0.3f, 0.5f}; // width, height
    float thickness = 0.01f;
    bool hanging = false;  // true: hangs below the hinge (gong); false: knockdown standing on it
    float angle = 0.0f;    // rotation about +X
    float angularVelocity = 0.0f;
    float restAngle = 0.0f;
    float mass = 10.0f;
    int collider = -1;
    float downTimer = 0.0f;
    bool resetting = false;
    float flash = 0.0f;
    int hits = 0;
};

struct TargetHitResult {
    std::string name;
    int ringScore = -1; // paper only
};

class Targets {
public:
    void addPaper(World& world, const std::string& name, const glm::vec3& faceCenter);
    void addKnockdown(World& world, const std::string& name, const glm::vec3& hinge, const glm::vec2& size);
    void addGong(World& world, const std::string& name, const glm::vec3& hinge, const glm::vec2& size);

    TargetHitResult hitPaper(int index, const glm::vec3& point);
    TargetHitResult hitPlate(int index, const glm::vec3& point, const glm::vec3& impulse);

    void update(float dt, World& world);
    void draw(Renderer& r) const;
    void clearPaper();
    int totalScore() const;

    std::vector<PaperTarget> papers;
    std::vector<SteelPlate> plates;

private:
    struct Prop {
        OBB box;
        glm::vec3 color;
    };
    // Static target furniture (posts, frames); also registered as colliders.
    void addProp(World& world, const OBB& box, const glm::vec3& color, Surface surface);
    std::vector<Prop> m_props;

    static OBB plateBox(const SteelPlate& p);
    static glm::vec3 plateCenterLocal(const SteelPlate& p);
};
