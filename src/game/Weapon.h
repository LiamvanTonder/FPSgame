#pragma once

#include "game/ArmModel.h"
#include "game/Ballistics.h"
#include "game/WeaponDefs.h"
#include "items/Item.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <random>
#include <vector>

class Renderer;
class World;

struct WeaponInput {
    bool adsHeld = false;
    bool fireHeld = false;
    bool holdBreath = false;
    // Latched edges, consumed by the first simulation tick of the frame
    bool firePressed = false;
    bool reloadPressed = false;
    bool toggleFireMode = false;
    int selectWeapon = -1;  // hand slot 0 / 1
    float zoomSteps = 0.0f; // mouse wheel notches
    int zeroSteps = 0;      // elevation turret clicks (+/- 25 m)
};

struct WeaponContext {
    glm::vec3 eye{0.0f};
    glm::vec3 bodyVelocity{0.0f};
    float yaw = 0.0f, pitch = 0.0f;
    float leanAngle = 0.0f;    // rad, + = right
    glm::vec3 aimPoint{0.0f};  // where the head is looking (world)
    bool sprinting = false;
    bool crouching = false;
    float moveSpeed = 0.0f;
    int footstep = 0;
    float landingSpeed = 0.0f;
    float heartRate = 72.0f;   // bpm
    float exertion = 0.0f;     // 0 rest .. 1 maximal heart rate
    // Injuries
    float armShake = 0.0f;     // extra tremor (rad/s kicks)
    float armSpeed = 1.0f;     // arm strength / speed scale
    bool supportArmGone = false;
    bool armsGone = false;
    bool busy = false;         // treating wounds, searching: weapon lowered
};

// Things the effects and sound systems react to.
struct WeaponEvent {
    enum Type { Shot, Eject, MagOut, MagIn, Cycle, ShellIn, DryFire } type;
    glm::vec3 position;  // muzzle or ejection port
    glm::vec3 direction; // bore direction
    glm::vec3 velocity;  // casing velocity / weapon velocity
    CasingType casing;
    float smokeScale;
    int owner = 0;
    int weapon = 0;      // weaponDefs() index
    bool suppressed = false;
};

// An item that left the weapon (dumped magazine).
struct DroppedItem {
    ItemPtr item;
    glm::vec3 position{0.0f}, velocity{0.0f};
    glm::quat rotation{1, 0, 0, 0};
};

// Optic state needed to render the sight picture.
struct OpticView {
    OpticType type = OpticType::Iron;
    glm::quat axis{1, 0, 0, 0};  // optical axis orientation (looks down -Z)
    glm::vec3 ocular{0.0f};      // eyepiece / window centre (world)
    float ocularRadius = 0.0f;
    float eyeRelief = 0.0f;
    float magnification = 1.0f;
    float objectiveRadius = 0.0f;
    Reticle reticle = Reticle::None;
};

// The weapon in a character's hands: a physical rig driven by the arms, fed from the
// character's real magazines and ammunition.
class Weapon {
public:
    void init(const WeaponContext& ctx, Equipment* equipment, int owner, bool firstPerson);
    void update(const WeaponInput& in, const WeaponContext& ctx, float dt, const World& world, Ballistics& ballistics,
                std::vector<Impact>& impacts, std::vector<WeaponEvent>& events);
    void draw(Renderer& r, float alpha) const;

    // Current simulated pose (world space).
    glm::vec3 muzzle() const { return toWorld(def().muzzle); }
    glm::vec3 boreDirection() const;
    OpticView opticView(float alpha) const;
    // Where the aiming eye sits on the weapon (cheek weld), interpolated for rendering.
    glm::vec3 sightEyeWorld(float alpha) const;

    Item* item() const;
    bool hasWeapon() const { return item() != nullptr; }
    const WeaponDef& def() const;
    const OpticDef& optic() const;
    int hand() const { return m_hand; }
    float magnification() const;
    float zeroRange() const;
    float zeroAngle() const { return m_zeroAngle; }
    int ammo() const;
    int capacity() const;
    bool chamberLoaded() const;
    bool isFullAuto() const;
    bool isAiming() const { return m_adsBlend > 0.5f; }
    bool isReloading() const { return m_reloadTimer > 0.0f; }
    bool isCycling() const { return m_actionTimer > 0.0f; }
    bool isHandLoading() const;
    bool isSwitching() const { return m_switchTo >= 0 || m_switchBlend > 0.0f; }
    bool isLowered() const { return m_lowerBlend > 0.5f; }
    bool isHoldingBreath() const { return m_holdingBreath; }
    float breathRemaining() const { return 1.0f - m_breathHeld / kMaxBreathHold; }
    float wallPush() const { return m_wallPush; }
    float adsBlend() const { return m_adsBlend; }
    glm::vec2 drift() const { return m_drift; }
    const char* fireModeName() const;
    // Status note for the HUD ("NO MAGAZINE", ...), empty if none.
    const char* note() const { return m_noteTimer > 0.0f ? m_note : ""; }
    // True once after held breath ran out (the body then gasps; heart rate spikes).
    bool consumeGasp() {
        bool g = m_gasp;
        m_gasp = false;
        return g;
    }
    std::vector<DroppedItem>& drops() { return m_drops; }
    // Rifle (in hands) world transform, for dropping it on death.
    glm::mat4 worldTransform(float alpha) const;

    ArmModel arm;
    static constexpr float kMaxBreathHold = 6.0f;

private:
    // Bolt / hand keyframes of the single-load sequence at time t.
    struct LoadPose {
        float boltLift = 0.0f;   // rad about the bore axis
        float boltTravel = 0.0f; // m rearward
        glm::vec3 hand{0.0f};    // right hand, weapon-local
        bool handOnGrip = true;
        bool showCartridge = false;
        glm::vec3 cartridge{0.0f};
    };

    glm::vec3 toWorld(const glm::vec3& local) const { return arm.pivot + arm.orientation * local; }
    glm::vec3 sightEyeLocal() const; // eye position (weapon-local) when aimed
    float sightHeight() const;
    void rezero();
    void computeTargetPose(const WeaponContext& ctx, const World& world, float dt, glm::vec3& anchor, glm::quat& target);
    void holsterPose(const WeaponContext& ctx, glm::vec3& anchor, glm::quat& target) const;
    void fire(const World& world, Ballistics& ballistics, std::vector<Impact>& impacts, std::vector<WeaponEvent>& events);
    void eject(std::vector<WeaponEvent>& events);
    void event(std::vector<WeaponEvent>& events, WeaponEvent::Type type, const glm::vec3& local) const;
    void applyBodyMotion(const WeaponContext& ctx);
    void updateDrift(const WeaponContext& ctx, float dt);
    void updateReload(const WeaponInput& in, float dt, bool blocked, std::vector<WeaponEvent>& events);
    void updateSingleLoad(const WeaponInput& in, float dt, std::vector<WeaponEvent>& events);
    void setNote(const char* n) {
        m_note = n;
        m_noteTimer = 1.5f;
    }
    void resetTransient();
    LoadPose loadPose() const;
    float actionOffset() const; // current slide/bolt/pump travel (m)
    bool roundReady() const;

    Equipment* m_eq = nullptr;
    int m_owner = 0;
    bool m_firstPerson = true;
    int m_hand = 0;
    uint32_t m_curUid = 0;
    int m_switchTo = -1;
    float m_switchBlend = 0.0f;
    float m_lowerBlend = 0.0f;
    float m_zeroAngle = 0.0f;

    float m_adsBlend = 0.0f;
    float m_sprintBlend = 0.0f;
    float m_reloadBlend = 0.0f;
    float m_loadBlend = 0.0f;    // canted "feeding a round" pose
    float m_reloadTimer = 0.0f;  // magazine: time left; tube: time to next shell
    float m_reloadTotal = 0.0f;
    bool m_reloadFast = false;   // dump the old magazine
    bool m_magOutSent = false;
    float m_lastReloadPress = -10.0f;
    bool m_tubeLoading = false;
    float m_fireCooldown = 0.0f;
    float m_actionTimer = 0.0f;  // manual action cycle remaining
    float m_actionAnim = 0.0f;   // semi-auto slide animation remaining
    bool m_ejectPending = false;
    float m_wallPush = 0.0f;
    float m_breathHeld = 0.0f;
    float m_time = 0.0f;
    float m_flashTimer = 0.0f;
    float m_flashScale = 1.0f;
    bool m_holdingBreath = false;
    bool m_gasp = false;
    float m_exertion = 0.0f;     // from the body (heart rate), refreshed every tick
    float m_armShake = 0.0f;
    glm::vec2 m_drift{0.0f};     // slow wander of the intended aim (yaw, pitch rad)
    float m_heartPhase = 0.0f;
    float m_yaw = 0.0f;          // body yaw last tick, for drawing arms
    const char* m_note = "";
    float m_noteTimer = 0.0f;
    std::vector<DroppedItem> m_drops;
    std::mt19937 m_rng{1337};
};

// Draws a weapon item with its optic at a world transform (on backs, on the ground).
void drawWeaponItem(Renderer& r, const Item& item, const glm::mat4& base);
