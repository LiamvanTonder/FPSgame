#pragma once

#include "game/Health.h"
#include "game/Ragdoll.h"
#include "game/Weapon.h"
#include "items/Item.h"

#include <glm/glm.hpp>
#include <random>
#include <string>
#include <vector>

class Renderer;
class World;
class Effects;
class SoundBus;

enum BodyPart {
    BP_Head, BP_Chest, BP_Pelvis,
    BP_UpperArmL, BP_UpperArmR, BP_ForearmL, BP_ForearmR,
    BP_ThighL, BP_ThighR, BP_ShinL, BP_ShinR,
    kBodyPartCount
};
const char* bodyPartName(int part);

struct BodyParams {
    float walkSpeed = 2.4f;    // brisk walk with a rifle up
    float aimWalkSpeed = 1.3f; // slow, deliberate steps while aiming
    float crouchSpeed = 1.2f;
    float sprintSpeed = 5.5f;
    float groundAccel = 9.0f;  // m/s^2, how quickly the body changes speed
    float airAccel = 1.0f;
    float jumpSpeed = 3.2f;
    float standHeight = 1.80f;
    float crouchHeight = 1.25f;
    float eyeBelowTop = 0.14f;
    float radius = 0.28f;
    float stepHeight = 0.35f;
    float strideLength = 0.8f;

    // Leaning: the upper body tips about the hips.
    float maxLeanAngle = 0.35f;      // ~20 degrees
    float leanPivotFraction = 0.55f; // pivot height as a fraction of body height
    float leanTime = 0.25f;          // s to lean fully

    // Heart: resting -> maximal, with slower recovery than rise.
    float restingHeartRate = 72.0f;
    float maxHeartRate = 185.0f;
    float heartRiseTau = 8.0f;
    float heartRecoverTau = 25.0f;
};

// Everything a controller (keyboard or AI) can ask a body to do.
struct CharacterInput {
    glm::vec2 move{0.0f}; // x = strafe right, y = forward (relative to yaw)
    float yaw = 0.0f, pitch = 0.0f;
    bool sprint = false;
    bool crouch = false;
    bool jump = false;
    float lean = 0.0f; // -1 left .. +1 right
    WeaponInput weapon;
    // Start treating a wound with a med item (0 = none).
    uint32_t useMed = 0;
    int useMedZone = -1;
    bool cancelAction = false;
};

// Shared state the bodies need while simulating.
struct SimContext {
    World& world;
    class Ballistics& ballistics;
    std::vector<Impact>& impacts;
    std::vector<WeaponEvent>& events;
    Effects& fx;
    SoundBus& sound;
    std::vector<DroppedItem>& drops;
    double time;
    const struct MapData* map = nullptr;
};

// A person: player or bot. Movement, posture, wounds, gear and the weapon rig all live
// here so both are simulated identically.
class Character {
public:
    enum class Action { None, Med, LoadMag, Search, Extract, SendBack };

    void create(World& world, int id, bool firstPerson);
    void spawn(World& world, const glm::vec3& feet, float yaw);
    void despawn(World& world);
    void update(const CharacterInput& in, float dt, SimContext& sc);
    void draw(Renderer& r, float alpha, bool skipBody) const;

    // Called for a bullet that hit one of this body's parts. Returns a short description.
    std::string onHit(int part, const Impact& imp, SimContext& sc);
    void kill(const std::string& cause, int killer, const glm::vec3& point, const glm::vec3& impulse, SimContext& sc);

    // Starts a timed action (meds, loading a magazine); cancelled by firing, sprinting or another action.
    bool startMed(uint32_t uid, int zone);
    bool startLoadMagazine(uint32_t magUid, uint32_t ammoUid);
    void setBusy(Action a, float duration); // externally timed actions (searching, extracting)
    void cancelAction();
    float actionProgress() const { return m_actionDuration > 0.0f ? m_actionTime / m_actionDuration : 0.0f; }
    Action action() const { return m_action; }
    std::string actionLabel() const;

    // Geometry
    glm::vec3 eye() const;          // includes lean
    glm::vec3 eyeUnleaned() const;
    glm::vec3 viewDir() const;
    glm::vec3 flatForward() const;
    glm::vec3 right() const;
    glm::vec3 interpEye(float alpha) const { return glm::mix(prevEye, eye(), alpha); }
    float horizontalSpeed() const { return glm::length(glm::vec2(velocity.x, velocity.z)); }
    float leanAngle() const { return lean * params.maxLeanAngle; }
    float exertion() const;
    glm::vec3 chest() const { return joints[J_Chest]; }
    glm::vec3 head() const { return joints[J_Head]; }
    bool alive() const { return active && !dead; }
    bool visibleStance() const { return !crouching; }
    int colliderOf(int part) const { return m_colliders[part]; }

    int id = 0;
    bool active = false;
    bool firstPerson = false;
    std::string name;
    BodyParams params;
    glm::vec3 position{0.0f}; // feet
    glm::vec3 velocity{0.0f};
    glm::vec3 prevEye{0.0f};
    float yaw = 0.0f, pitch = 0.0f;
    float height = 1.8f;
    float lean = 0.0f;
    float heartRate = 72.0f;
    bool grounded = false;
    bool crouching = false;
    bool sprinting = false;
    int footstep = 0;          // 0 none, -1 left foot, +1 right foot (this tick)
    float landingSpeed = 0.0f;
    glm::vec3 aimPoint{0.0f};

    Health health;
    Equipment equip;
    Weapon weapon;

    // Pose
    JointArray joints{}, prevJoints{};
    float walkPhase = 0.0f;
    float bodyYaw = 0.0f; // hips and legs lag behind where the head looks
    bool posed = false;
    float stagger = 0.0f;
    float suppression = 0.0f; // incoming fire nearby (0..1)

    // Death
    bool dead = false;
    double deathTime = 0.0;
    Ragdoll ragdoll;
    bool pooled = false;
    bool searched = false; // a body that has been gone through
    std::string causeOfDeath;
    int killer = -1;
    int kills = 0;
    double lastShot = -1e9; // last time this character fired (muzzle flash / report)
    int tier = 0;           // loadout tier (bots)

    std::mt19937 rng;

private:
    void moveAxis(int axis, float delta, const World& world);
    bool overlapsWorld(const World& world) const;
    glm::vec3 leanPivot() const;
    glm::vec3 eyeForLean(float leanAmount) const;
    void updateMovement(const CharacterInput& in, float dt, const World& world);
    void updateLean(float target, float dt, const World& world);
    void updateHeart(const CharacterInput& in, float dt, bool holdingBreath, bool gasp);
    void updateAction(float dt, const CharacterInput& in, SimContext& sc);
    void animate(float dt);
    void updateColliders(World& world) const;

    int m_colliders[kBodyPartCount] = {};
    float m_strideAccum = 0.0f;
    int m_nextFoot = 1;
    float m_dripTimer = 1.0f;
    float m_fallSpeed = 0.0f;
    float m_groanTimer = 8.0f, m_breathTimer = 3.0f;
    std::vector<int> m_overlap;

    Action m_action = Action::None;
    float m_actionTime = 0.0f, m_actionDuration = 0.0f;
    uint32_t m_actionItem = 0, m_actionItem2 = 0;
    int m_actionZone = -1;
};
