#include "game/Weapon.h"

#include "physics/World.h"
#include "render/Camera.h"
#include "render/Renderer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace {

// Arm limits per stance for the reference carbine; each weapon scales them by its
// weight and balance. ADS is slower but steadier (stock locked in, cheek welded);
// the low-ready sprint carry is loose and slow to bring up.
//                         maxSpd maxAcc brake settle velGain ff   posK  posD
const ArmParams kHipArm    {7.0f, 60.0f, 0.70f, 14.0f, 45.0f, 0.85f, 250.0f, 24.0f};
const ArmParams kAdsArm    {3.3f, 38.0f, 0.75f, 11.0f, 40.0f, 0.90f, 420.0f, 36.0f};
const ArmParams kSprintArm {5.0f, 35.0f, 0.70f, 10.0f, 30.0f, 0.50f, 160.0f, 18.0f};
const ArmParams kReloadArm {4.0f, 30.0f, 0.70f, 10.0f, 30.0f, 0.50f, 200.0f, 20.0f};

ArmParams lerpParams(const ArmParams& a, const ArmParams& b, float t) {
    auto L = [t](float x, float y) { return x + (y - x) * t; };
    return {L(a.maxAngularSpeed, b.maxAngularSpeed), L(a.maxAngularAccel, b.maxAngularAccel),
            L(a.brakeFraction, b.brakeFraction),     L(a.settleRate, b.settleRate),
            L(a.velocityGain, b.velocityGain),       L(a.feedForward, b.feedForward),
            L(a.positionStiffness, b.positionStiffness), L(a.positionDamping, b.positionDamping)};
}

float approach(float v, float target, float maxDelta) {
    return v < target ? std::min(target, v + maxDelta) : std::max(target, v - maxDelta);
}

float smooth(float t) { return t * t * (3.0f - 2.0f * t); }
float ramp(float a, float b, float t) { return smooth(std::clamp((t - a) / (b - a), 0.0f, 1.0f)); }

const float kSlideAnimTime = 0.07f;
const float kSwitchOutTime = 0.35f;
const float kSwitchInTime = 0.40f;
const glm::vec3 kSleeve(0.28f, 0.30f, 0.22f);
const glm::vec3 kBrass(0.80f, 0.62f, 0.28f);
const glm::vec3 kCopper(0.72f, 0.40f, 0.22f);

// Hand-loading a single-shot bolt rifle (seconds into the sequence).
const float kLoadLift = 0.15f;      // bolt handle up
const float kLoadPull = 0.35f;      // bolt back, empty case flicked out
const float kLoadEject = 0.33f;
const float kLoadFetchMid = 0.575f; // hand at the ammo pouch
const float kLoadFetch = 0.80f;     // round at the ejection port
const float kLoadInsert = 1.15f;    // round pressed into the chamber
const float kLoadPush = 1.30f;      // bolt forward
const float kLoadLower = 1.45f;     // bolt handle down: locked
const float kLoadEnd = 1.60f;       // hand back on the grip
const float kLoadDelay = 0.25f;     // let recoil settle before reaching for the bolt
const float kBoltLiftAngle = 1.05f; // ~60 degrees
const glm::vec3 kBoltKnob(0.058f, 0.005f, -0.38f);
const glm::vec3 kAmmoPouch(0.13f, -0.22f, -0.02f); // weapon-local, roughly the chest rig

glm::mat4 segmentMatrix(const glm::vec3& a, const glm::vec3& b, float thickness) {
    glm::vec3 d = b - a;
    float len = glm::length(d);
    glm::mat4 m = glm::mat4_cast(lookRotation(d));
    m[3] = glm::vec4((a + b) * 0.5f, 1.0f);
    return glm::scale(m, glm::vec3(thickness, thickness, len));
}

glm::vec3 rotateAboutBore(const glm::vec3& p, float angle) {
    float c = std::cos(angle), s = std::sin(angle);
    return {p.x * c - p.y * s, p.x * s + p.y * c, p.z};
}

glm::mat4 partMatrix(const glm::mat4& base, const Part& part, float actionOffset, float boltLift, float boltTravel) {
    glm::mat4 m = base;
    glm::vec3 c = part.center;
    if (part.flags & Part::Action) c.z += actionOffset;
    if (part.flags & Part::Bolt) {
        m = glm::translate(m, glm::vec3(0.0f, 0.0f, boltTravel));
        m = glm::rotate(m, boltLift, glm::vec3(0, 0, 1));
    }
    m = glm::translate(m, c);
    if (part.tiltX != 0.0f) m = glm::rotate(m, part.tiltX, glm::vec3(1, 0, 0));
    return glm::scale(m, part.size);
}

} // namespace


static const WeaponDef& fallbackDef() { return weaponDefs()[kPistol]; }

Item* Weapon::item() const {
    if (!m_eq) return nullptr;
    Item* it = m_eq->slots[m_hand].get();
    return it && it->d().cat == ItemCat::Weapon ? it : nullptr;
}

const WeaponDef& Weapon::def() const {
    Item* it = item();
    return it ? weaponDefs()[it->d().weapon] : fallbackDef();
}

const OpticDef& Weapon::optic() const {
    Item* it = item();
    return opticDefs()[it ? it->optic : 0];
}

float Weapon::magnification() const {
    Item* it = item();
    return it ? it->magnification : 1.0f;
}

float Weapon::zeroRange() const {
    Item* it = item();
    return it ? it->zeroRange : 100.0f;
}

int Weapon::ammo() const {
    Item* it = item();
    return it ? loadedRounds(*it) : 0;
}

int Weapon::capacity() const {
    Item* it = item();
    return it ? loadedCapacity(*it) : 0;
}

bool Weapon::chamberLoaded() const { return roundReady(); }

bool Weapon::isFullAuto() const {
    Item* it = item();
    return it && it->fullAuto && def().canAuto;
}

bool Weapon::isHandLoading() const {
    Item* it = item();
    return it && def().reload == ReloadType::SingleLoad && it->loadT >= 0.0f;
}

bool Weapon::roundReady() const {
    Item* it = item();
    if (!it) return false;
    const WeaponDef& d = def();
    if (d.reload == ReloadType::Tube && !d.chamberSeparate) return !it->rounds.empty();
    return it->chambered >= 0;
}

void Weapon::resetTransient() {
    m_reloadTimer = m_reloadTotal = 0.0f;
    m_tubeLoading = false;
    m_actionTimer = m_actionAnim = 0.0f;
    m_ejectPending = false;
    m_magOutSent = false;
    m_reloadFast = false;
    if (Item* it = item()) {
        if (it->loadT >= 0.0f) it->loadT = -1.0f; // interrupted hand-loading starts over
        rezero();
    }
}

void Weapon::init(const WeaponContext& ctx, Equipment* equipment, int owner, bool firstPerson) {
    m_eq = equipment;
    m_owner = owner;
    m_firstPerson = firstPerson;
    m_rng.seed(1337u + unsigned(owner) * 7919u);
    m_hand = 0;
    if (!item() && m_eq && m_eq->slots[1]) m_hand = 1;
    m_curUid = item() ? item()->uid : 0;
    resetTransient();
    glm::vec3 anchor;
    glm::quat target;
    World empty;
    computeTargetPose(ctx, empty, 0.0f, anchor, target);
    arm.reset(anchor, target);
    m_yaw = ctx.yaw;
}

const char* Weapon::fireModeName() const {
    if (!hasWeapon()) return "";
    switch (def().action) {
    case ActionType::Bolt: return def().reload == ReloadType::SingleLoad ? "SINGLE SHOT" : "BOLT";
    case ActionType::Pump: return "PUMP";
    default: return isFullAuto() ? "AUTO" : "SEMI";
    }
}

float Weapon::sightHeight() const {
    const OpticDef& o = optic();
    return o.type == OpticType::Iron ? def().ironSightHeight : def().railTopY + o.axisAboveRail;
}

glm::vec3 Weapon::sightEyeLocal() const {
    const WeaponDef& d = def();
    const OpticDef& o = optic();
    float h = sightHeight();
    if (o.type == OpticType::Magnified) return {0.0f, h, d.railZ + o.length * 0.5f + o.eyeRelief};
    return {0.0f, h, d.ironEyeZ}; // irons and red dots: normal cheek weld / arm extension
}

void Weapon::rezero() {
    Item* it = item();
    if (!it) return;
    m_zeroAngle = Ballistics::solveZeroAngle(def().projectile, sightHeight(), it->zeroRange);
}

glm::vec3 Weapon::boreDirection() const {
    // The barrel is angled up from the sight line by the zero angle.
    float z = zeroAngle();
    return arm.orientation * glm::vec3(0.0f, std::sin(z), -std::cos(z));
}

OpticView Weapon::opticView(float alpha) const {
    OpticView v;
    const OpticDef& o = optic();
    const WeaponDef& d = def();
    v.type = o.type;
    if (o.type == OpticType::Iron || !hasWeapon()) {
        v.type = OpticType::Iron;
        return v;
    }
    glm::vec3 pivot = glm::mix(arm.prevPivot, arm.pivot, alpha);
    glm::quat q = glm::slerp(arm.prevOrientation, arm.orientation, alpha);
    float h = sightHeight();
    float z = o.type == OpticType::Magnified ? d.railZ + o.length * 0.5f : d.railZ;
    v.axis = q;
    v.ocular = pivot + q * glm::vec3(0.0f, h, z);
    v.ocularRadius = o.ocularRadius;
    v.eyeRelief = o.eyeRelief;
    v.magnification = magnification();
    v.objectiveRadius = o.objectiveRadius;
    v.reticle = o.reticle;
    return v;
}

glm::vec3 Weapon::sightEyeWorld(float alpha) const {
    glm::vec3 pivot = glm::mix(arm.prevPivot, arm.pivot, alpha);
    glm::quat q = glm::slerp(arm.prevOrientation, arm.orientation, alpha);
    return pivot + q * sightEyeLocal();
}

glm::mat4 Weapon::worldTransform(float alpha) const {
    glm::mat4 m = glm::mat4_cast(glm::slerp(arm.prevOrientation, arm.orientation, alpha));
    m[3] = glm::vec4(glm::mix(arm.prevPivot, arm.pivot, alpha), 1.0f);
    return m;
}

void Weapon::holsterPose(const WeaponContext& ctx, glm::vec3& anchor, glm::quat& target) const {
    glm::quat yawQ = glm::angleAxis(ctx.yaw, glm::vec3(0, 1, 0));
    anchor = ctx.eye + yawQ * glm::vec3(0.15f, -0.65f, 0.0f);
    target = lookRotation(directionFromYawPitch(ctx.yaw + 0.3f, -1.25f));
}

void Weapon::computeTargetPose(const WeaponContext& ctx, const World& world, float dt, glm::vec3& anchor,
                               glm::quat& target) {
    const WeaponDef& d = def();
    const glm::vec3 up(0, 1, 0);
    glm::quat yawQ = glm::angleAxis(ctx.yaw, up);
    glm::vec3 right = yawQ * glm::vec3(1, 0, 0);
    glm::vec3 flatFwd = yawQ * glm::vec3(0, 0, -1);
    // Shoulder frame: follows the head pitch partially and tips with the lean.
    glm::quat leanQ = glm::angleAxis(ctx.leanAngle, flatFwd);
    glm::quat bodyQ = leanQ * glm::angleAxis(ctx.pitch * 0.5f, right) * yawQ;
    // Leaning shooters cant the weapon partway with the body.
    glm::quat cant = glm::angleAxis(-0.5f * ctx.leanAngle, glm::vec3(0, 0, 1));

    glm::vec3 aim = ctx.aimPoint;
    glm::vec3 toAim = aim - ctx.eye;
    float aimDist = glm::length(toAim);
    if (aimDist < 3.0f) aim = ctx.eye + (aimDist > 1e-4f ? toAim / aimDist : glm::vec3(0, 0, -1)) * 3.0f;

    // Ready / shouldered-but-not-sighted: bore pointed at the aim point.
    glm::vec3 hipOffset = d.hipOffset;
    if (ctx.supportArmGone && !d.oneHanded) hipOffset += glm::vec3(0.02f, -0.08f, -0.12f); // held out one-handed
    glm::vec3 hipAnchor = ctx.eye + bodyQ * hipOffset;
    glm::quat hipQ = lookRotation(aim - hipAnchor) * cant;

    // Aimed: weapon placed so the sight line (or optic axis) passes through the eye and the aim point.
    glm::quat adsQ = lookRotation(aim - ctx.eye) * cant;
    glm::vec3 adsAnchor = ctx.eye - adsQ * sightEyeLocal();

    float s = smooth(m_adsBlend);
    anchor = glm::mix(hipAnchor, adsAnchor, s);
    target = glm::slerp(hipQ, adsQ, s);

    // Sway (breathing + tremor) and drift (slow wander of where the shooter holds).
    {
        const float t = m_time;
        float breath = std::sin(t * 2.0f * float(M_PI) * 0.23f);
        float breathYaw = std::sin(t * 2.0f * float(M_PI) * 0.23f + 1.3f);
        float tremorP = 0.5f * std::sin(t * 10.7f + 0.3f) + 0.3f * std::sin(t * 19.3f + 2.1f) + 0.2f * std::sin(t * 33.1f);
        float tremorY = 0.5f * std::sin(t * 12.3f + 1.7f) + 0.3f * std::sin(t * 17.9f + 0.4f) + 0.2f * std::sin(t * 29.7f + 2.6f);

        float breathAmp = glm::mix(0.006f, 0.0018f, s) * (1.0f + 3.0f * m_exertion);
        float tremorAmp = glm::mix(0.0025f, 0.0007f, s) * (1.0f + 1.5f * m_exertion) * (1.0f + 0.6f * ctx.moveSpeed);
        tremorAmp *= d.swayScale;
        tremorAmp += m_armShake * 0.08f;
        if (m_holdingBreath) {
            breathAmp *= 0.05f;
            tremorAmp *= 0.5f;
        }
        float pitchOff = breath * breathAmp + tremorP * tremorAmp + m_drift.y;
        float yawOff = breathYaw * breathAmp * 0.35f + tremorY * tremorAmp + m_drift.x;
        target = target * glm::angleAxis(yawOff, glm::vec3(0, 1, 0)) * glm::angleAxis(pitchOff, glm::vec3(1, 0, 0));
    }

    // Feeding a round: rolled left so the ejection port faces up, muzzle dipped.
    float lb = smooth(m_loadBlend);
    if (lb > 0.0f) {
        target = target * glm::angleAxis(-0.07f * lb, glm::vec3(1, 0, 0)) * glm::angleAxis(0.26f * lb, glm::vec3(0, 0, 1));
        float fromHip = 1.0f - s;
        anchor += yawQ * glm::vec3(-0.08f * fromHip, 0.07f * fromHip - 0.03f, -0.04f * fromHip) * lb;
    }

    // Sprint carry (low ready, muzzle down and across the body).
    float sp = smooth(m_sprintBlend);
    if (sp > 0.0f) {
        glm::vec3 sprintAnchor = ctx.eye + yawQ * glm::vec3(0.12f, -0.45f, -0.05f);
        glm::quat sprintQ = lookRotation(directionFromYawPitch(ctx.yaw + 0.65f, -0.65f)) * glm::angleAxis(0.3f, glm::vec3(0, 0, 1));
        anchor = glm::mix(anchor, sprintAnchor, sp);
        target = glm::slerp(target, sprintQ, sp);
    }

    // Reload: canted and lowered so the feed faces the support hand.
    float rl = smooth(m_reloadBlend);
    if (rl > 0.0f) {
        glm::vec3 reloadAnchor = ctx.eye + yawQ * d.reloadOffset;
        glm::quat reloadQ = lookRotation(directionFromYawPitch(ctx.yaw + 0.2f, -0.45f)) * glm::angleAxis(0.6f, glm::vec3(0, 0, 1));
        anchor = glm::mix(anchor, reloadAnchor, rl);
        target = glm::slerp(target, reloadQ, rl);
    }

    // Weapon change / lowered (hands busy): the gun goes down out of view.
    float sw = smooth(std::max(m_switchBlend, m_lowerBlend));
    if (!hasWeapon()) sw = 1.0f;
    if (sw > 0.0f) {
        glm::vec3 hAnchor;
        glm::quat hQ;
        holsterPose(ctx, hAnchor, hQ);
        anchor = glm::mix(anchor, hAnchor, sw);
        target = glm::slerp(target, hQ, sw);
    }

    // Barrel vs. walls: if the weapon would poke into geometry, pull it back toward the
    // body, and past a point tilt the muzzle up. The bore never enters solid geometry.
    if (hasWeapon()) {
        glm::vec3 fwd = target * glm::vec3(0, 0, -1);
        glm::vec3 start = anchor - fwd * d.wallBack;
        const float reach = d.length + d.wallBack + 0.05f;
        WorldHit hit;
        SegmentQuery q;
        q.skipPenetrable = true;
        q.ignoreOwner = m_owner;
        float penetration = 0.0f;
        if (world.segment(start, start + fwd * reach, hit, q) && world.collider(hit.collider).owner == kOwnerWorld)
            penetration = (1.0f - hit.t) * reach;
        m_wallPush = dt > 0.0f ? approach(m_wallPush, penetration, dt * 3.0f) : penetration;
        anchor -= fwd * std::min(m_wallPush, 0.3f);
        if (m_wallPush > 0.2f) {
            float tilt = std::min((m_wallPush - 0.2f) * 3.0f, 1.1f);
            glm::vec3 axis = target * glm::vec3(1, 0, 0);
            target = glm::angleAxis(tilt, axis) * target;
        }
    }
}

void Weapon::applyBodyMotion(const WeaponContext& ctx) {
    glm::quat yawQ = glm::angleAxis(ctx.yaw, glm::vec3(0, 1, 0));
    float aimDamp = glm::mix(1.0f, 0.45f, smooth(m_adsBlend));

    if (ctx.footstep != 0) {
        float k = std::clamp(ctx.moveSpeed / 2.4f, 0.3f, 2.2f) * aimDamp;
        float side = float(ctx.footstep);
        arm.addLinearVelocity(yawQ * glm::vec3(side * 0.05f, -0.10f, 0.0f) * k);
        arm.addAngularVelocity((glm::vec3(0, 1, 0) * side * 0.10f + (yawQ * glm::vec3(1, 0, 0)) * 0.05f) * k);
    }
    if (ctx.landingSpeed > 0.5f) {
        float k = std::min(ctx.landingSpeed, 8.0f) * aimDamp;
        arm.addLinearVelocity(glm::vec3(0.0f, -0.30f * k, 0.0f));
        arm.addAngularVelocity((yawQ * glm::vec3(1, 0, 0)) * (-0.25f * k));
    }
}

void Weapon::updateDrift(const WeaponContext& ctx, float dt) {
    // Ornstein-Uhlenbeck wander: the hold slowly drifts off the point of aim and is pulled
    // back. Large from the hip, small with a cheek weld, worse with a racing heart.
    const WeaponDef& d = def();
    float s = smooth(m_adsBlend);
    float amplitude = glm::mix(0.044f, 0.0015f, s); // ~2.5 deg hip, ~5 MOA aimed (std. dev.)
    amplitude *= (1.0f + 1.4f * m_exertion) * (1.0f + 0.5f * ctx.moveSpeed) * d.swayScale;
    amplitude *= 1.0f + 25.0f * ctx.armShake;
    if (ctx.supportArmGone && !d.oneHanded) amplitude *= 3.0f;
    if (ctx.crouching) amplitude *= 0.7f;
    if (m_holdingBreath) amplitude *= 0.4f;
    float tau = glm::mix(1.2f, 2.0f, s);

    std::normal_distribution<float> n(0.0f, 1.0f);
    float noise = amplitude * std::sqrt(2.0f * dt / tau);
    m_drift += -m_drift * (dt / tau) + glm::vec2(n(m_rng), n(m_rng) * 0.8f) * noise;
}

void Weapon::event(std::vector<WeaponEvent>& events, WeaponEvent::Type type, const glm::vec3& local) const {
    const WeaponDef& d = def();
    glm::vec3 p = toWorld(local);
    WeaponEvent e{type, p, arm.orientation * glm::vec3(0, 0, -1), arm.pointVelocity(p), d.casing, 0.0f};
    e.owner = m_owner;
    e.weapon = item() ? item()->d().weapon : 0;
    e.suppressed = d.suppressed;
    events.push_back(e);
}

void Weapon::eject(std::vector<WeaponEvent>& events) {
    const WeaponDef& d = def();
    std::uniform_real_distribution<float> u11(-1.0f, 1.0f);
    glm::vec3 port = toWorld(d.ejectionPort);
    bool slow = d.casing == CasingType::Shell || d.reload == ReloadType::SingleLoad; // flicked out by hand
    glm::vec3 kick = slow ? glm::vec3(1.3f, 0.9f, 0.3f) : glm::vec3(2.2f, 1.3f, 0.7f);
    kick += glm::vec3(u11(m_rng), u11(m_rng), u11(m_rng)) * 0.3f;
    glm::vec3 vel = arm.pointVelocity(port) + arm.orientation * kick;
    WeaponEvent e{WeaponEvent::Eject, port, arm.orientation * glm::vec3(0, 0, -1), vel, d.casing, 0.0f};
    e.owner = m_owner;
    e.weapon = item() ? item()->d().weapon : 0;
    events.push_back(e);
}

void Weapon::fire(const World& world, Ballistics& ballistics, std::vector<Impact>& impacts,
                  std::vector<WeaponEvent>& events) {
    Item* it = item();
    const WeaponDef& d = def();
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    std::uniform_real_distribution<float> u11(-1.0f, 1.0f);

    // Which round goes off.
    int round = -1;
    bool fromCylinder = d.reload == ReloadType::Tube && !d.chamberSeparate;
    if (fromCylinder) {
        round = it->rounds.back();
        it->rounds.pop_back();
    } else {
        round = it->chambered;
        it->chambered = -1;
    }
    const AmmoDef& a = ammoDefs()[round];
    BallisticParams bp = a.ballistics(d.projectile.muzzleVelocity);
    int pellets = a.pellets > 0 ? a.pellets : d.pellets;
    float spread = a.spread >= 0.0f ? a.spread : d.pelletSpread;
    if (pellets <= 1) spread = 0.0f;

    glm::vec3 bore = boreDirection();
    glm::vec3 right = arm.orientation * glm::vec3(1, 0, 0);
    glm::vec3 up = arm.orientation * glm::vec3(0, 1, 0);
    glm::vec3 mz = muzzle();
    glm::vec3 chamber = toWorld(d.chamber);
    // Projectiles leave the actual muzzle with the barrel's own motion added.
    glm::vec3 muzzleVel = arm.pointVelocity(mz);

    const float moa = 0.000291f;
    for (int i = 0; i < pellets; ++i) {
        float sr = spread * std::sqrt(u01(m_rng));
        float sa = u01(m_rng) * 2.0f * float(M_PI);
        float dr = moa * d.dispersionMoa * 0.5f * std::sqrt(u01(m_rng));
        float da = u01(m_rng) * 2.0f * float(M_PI);
        glm::vec3 offset = right * (std::cos(sa) * sr + std::cos(da) * dr) + up * (std::sin(sa) * sr + std::sin(da) * dr);
        glm::vec3 dir = glm::normalize(bore + offset);
        ballistics.fire(bp, chamber, mz, dir * bp.muzzleVelocity + muzzleVel, world, impacts, m_owner, round, a.penetration);
    }

    // Recoil goes into the weapon body; the arms then fight it back on target.
    float s = smooth(m_adsBlend);
    float hip = glm::mix(1.35f, 1.0f, s);
    float energyScale = std::sqrt(std::max(0.3f, (bp.mass * bp.muzzleVelocity) / (d.projectile.mass * d.projectile.muzzleVelocity)));
    hip *= energyScale;
    arm.addAngularVelocity(arm.orientation * glm::vec3(d.recoilRise * hip, u11(m_rng) * d.recoilYaw * hip, u11(m_rng) * 0.2f));
    arm.addLinearVelocity(arm.orientation * glm::vec3(0.0f, 0.04f, d.recoilBack));

    m_fireCooldown = 60.0f / d.cyclicRpm;
    m_flashTimer = 0.03f;
    m_flashScale = (0.8f + 0.4f * u01(m_rng)) * d.flashScale;
    WeaponEvent shot{WeaponEvent::Shot, mz, bore, muzzleVel, d.casing, d.smokeScale};
    shot.owner = m_owner;
    shot.weapon = it->d().weapon;
    shot.suppressed = d.suppressed;
    events.push_back(shot);

    if (d.reload == ReloadType::SingleLoad) {
        // The empty stays in the chamber until the shooter works the bolt and feeds a new round.
        it->caseInChamber = true;
        it->loadDelay = kLoadDelay;
        return;
    }
    if (fromCylinder) return; // cases stay in the cylinder / barrels until reloading

    // Feed the next round.
    if (d.reload == ReloadType::Magazine) {
        if (it->mag && !it->mag->rounds.empty()) {
            it->chambered = it->mag->rounds.back();
            it->mag->rounds.pop_back();
        }
    } else if (!it->rounds.empty()) {
        it->chambered = it->rounds.back();
        it->rounds.pop_back();
    }
    if (d.action == ActionType::SemiAuto) {
        eject(events);
        m_actionAnim = kSlideAnimTime;
    } else {
        // Manual action: the shooter works the bolt / pump, ejecting the empty mid-stroke.
        m_actionTimer = d.actionCycleTime;
        m_ejectPending = true;
    }
}

Weapon::LoadPose Weapon::loadPose() const {
    LoadPose p;
    const WeaponDef& d = def();
    p.hand = d.rightHand;
    Item* it = item();
    float t = it ? it->loadT : -1.0f;
    if (d.reload != ReloadType::SingleLoad || t < 0.0f) return p;

    p.handOnGrip = false;
    p.boltLift = kBoltLiftAngle * (ramp(0.0f, kLoadLift, t) - ramp(kLoadPush, kLoadLower, t));
    p.boltTravel = d.actionTravel * (ramp(kLoadLift, kLoadPull, t) - ramp(kLoadInsert, kLoadPush, t));
    glm::vec3 knob = rotateAboutBore(kBoltKnob, p.boltLift) + glm::vec3(0.0f, 0.0f, p.boltTravel);
    glm::vec3 grip = d.rightHand;
    glm::vec3 port = d.ejectionPort + glm::vec3(-0.005f, 0.035f, 0.0f);
    glm::vec3 pressed = port + glm::vec3(0.0f, -0.025f, 0.0f);

    if (t < 0.08f) p.hand = glm::mix(grip, knob, ramp(0.0f, 0.08f, t));
    else if (t < kLoadPull) p.hand = knob;
    else if (t < kLoadFetchMid) p.hand = glm::mix(knob, kAmmoPouch, ramp(kLoadPull, kLoadFetchMid, t));
    else if (t < kLoadFetch) p.hand = glm::mix(kAmmoPouch, port, ramp(kLoadFetchMid, kLoadFetch, t));
    else if (t < kLoadInsert) p.hand = glm::mix(port, pressed, ramp(kLoadFetch, kLoadInsert, t));
    else if (t < kLoadLower) p.hand = glm::mix(pressed, knob, ramp(kLoadInsert, kLoadInsert + 0.08f, t));
    else p.hand = glm::mix(knob, grip, ramp(kLoadLower, kLoadEnd, t));

    // The round rides in the fingers, then drops into the port and is pushed home by the bolt.
    p.showCartridge = t >= kLoadFetchMid && t < kLoadPush;
    if (t < kLoadFetch) p.cartridge = p.hand + glm::vec3(0.0f, 0.01f, -0.02f);
    else p.cartridge = glm::mix(port + glm::vec3(0.0f, 0.01f, -0.02f), glm::vec3(0.0f, 0.0f, -0.41f), ramp(kLoadFetch, kLoadPush, t));
    return p;
}

void Weapon::updateSingleLoad(const WeaponInput& in, float dt, std::vector<WeaponEvent>& events) {
    Item* it = item();
    if (!it || def().reload != ReloadType::SingleLoad) return;
    if (isSwitching() || m_lowerBlend > 0.0f) {
        if (it->loadT >= 0.0f) it->loadT = -1.0f; // interrupted; starts over when the rifle comes back up
        return;
    }
    if (it->loadT < 0.0f) {
        if (it->chambered < 0) {
            it->loadDelay -= dt;
            bool want = it->loadDelay <= 0.0f || in.reloadPressed;
            if (want && (it->caseInChamber || m_eq->roundsAvailable(def().caliber) > 0)) it->loadT = 0.0f;
            else if (in.reloadPressed) setNote("NO AMMO");
        }
        return;
    }
    float before = it->loadT;
    it->loadT += dt;
    if (it->caseInChamber && before < kLoadEject && it->loadT >= kLoadEject) {
        eject(events);
        it->caseInChamber = false;
    }
    if (before < kLoadPush && it->loadT >= kLoadPush) event(events, WeaponEvent::Cycle, def().chamber);
    if (it->loadT >= kLoadEnd) {
        it->loadT = -1.0f;
        std::vector<uint8_t> r;
        if (m_eq->takeRounds(def().caliber, 1, r) > 0) it->chambered = r[0];
        else setNote("NO AMMO");
    }
}

void Weapon::updateReload(const WeaponInput& in, float dt, bool blocked, std::vector<WeaponEvent>& events) {
    Item* it = item();
    const WeaponDef& d = def();
    if (d.reload == ReloadType::Magazine) {
        bool full = it->mag && int(it->mag->rounds.size()) >= it->mag->d().capacity && it->chambered >= 0;
        if (in.reloadPressed) {
            bool doubleTap = m_time - m_lastReloadPress < 0.4f;
            m_lastReloadPress = m_time;
            if (m_reloadTimer > 0.0f && doubleTap && m_reloadTimer > m_reloadTotal * 0.5f) {
                // Second press early in the reload: dump the old magazine on the ground, faster.
                m_reloadFast = true;
                m_reloadTimer *= 0.7f;
            } else if (m_reloadTimer <= 0.0f && !blocked && !full) {
                if (m_eq->magazinesAvailable(d.magMask, d.caliber) > 0) {
                    m_reloadTotal = m_reloadTimer = it->chambered >= 0 ? d.reloadTime : d.emptyReloadTime;
                    m_reloadFast = false;
                    m_magOutSent = false;
                } else {
                    setNote("NO MAGAZINE");
                }
            }
        }
        if (m_reloadTimer > 0.0f) {
            if (blocked) {
                m_reloadTimer = 0.0f; // interrupted before the new magazine went in
                return;
            }
            m_reloadTimer -= dt;
            if (!m_magOutSent && m_reloadTimer < m_reloadTotal * 0.75f) {
                m_magOutSent = true;
                event(events, WeaponEvent::MagOut, d.chamber + glm::vec3(0.0f, -0.08f, -0.05f));
                // The old magazine comes out now.
                if (it->mag) {
                    ItemPtr old = std::move(it->mag);
                    if (m_reloadFast || !m_eq->stow(old)) {
                        DroppedItem drop;
                        drop.position = toWorld(d.chamber + glm::vec3(0.0f, -0.12f, -0.05f));
                        drop.velocity = arm.pivotVelocity + glm::vec3(0.0f, -0.5f, 0.0f);
                        drop.rotation = arm.orientation;
                        drop.item = std::move(old);
                        m_drops.push_back(std::move(drop));
                    }
                }
            }
            if (m_reloadTimer <= 0.0f) {
                m_reloadTimer = 0.0f;
                ItemPtr mag = m_eq->takeBestMagazine(d.magMask, d.caliber);
                if (mag) {
                    it->mag = std::move(mag);
                    event(events, WeaponEvent::MagIn, d.chamber + glm::vec3(0.0f, -0.08f, -0.05f));
                    if (it->chambered < 0 && !it->mag->rounds.empty()) {
                        it->chambered = it->mag->rounds.back();
                        it->mag->rounds.pop_back();
                        event(events, WeaponEvent::Cycle, d.chamber);
                    }
                }
            }
        }
    } else if (d.reload == ReloadType::Tube) {
        // Rounds go in one at a time; firing interrupts.
        int cap = d.magazine;
        bool tubeFull = int(it->rounds.size()) >= cap && (!d.chamberSeparate || it->chambered >= 0);
        if (in.reloadPressed && !m_tubeLoading && !tubeFull && !blocked) {
            if (m_eq->roundsAvailable(d.caliber) > 0) {
                m_tubeLoading = true;
                bool prep = d.chamberSeparate ? it->chambered < 0 : true; // open the action / dump empties
                m_reloadTimer = d.reloadTime + (prep ? d.emptyReloadTime : 0.0f);
                m_reloadTotal = m_reloadTimer;
            } else {
                setNote("NO AMMO");
            }
        }
        if (m_tubeLoading && ((in.firePressed && roundReady()) || blocked)) {
            m_tubeLoading = false;
            m_reloadTimer = 0.0f;
        }
        if (m_tubeLoading) {
            m_reloadTimer -= dt;
            if (m_reloadTimer <= 0.0f) {
                std::vector<uint8_t> r;
                if (m_eq->takeRounds(d.caliber, 1, r) > 0) {
                    event(events, WeaponEvent::ShellIn, d.chamber + glm::vec3(0.0f, -0.03f, -0.05f));
                    if (d.chamberSeparate && it->chambered < 0) it->chambered = r[0];
                    else it->rounds.push_back(r[0]);
                }
                bool nowFull = int(it->rounds.size()) >= cap && (!d.chamberSeparate || it->chambered >= 0);
                if (nowFull || r.empty() || m_eq->roundsAvailable(d.caliber) == 0) {
                    m_tubeLoading = false;
                    m_reloadTimer = 0.0f;
                    if (d.action == ActionType::Pump) event(events, WeaponEvent::Cycle, d.chamber);
                } else {
                    m_reloadTimer += d.reloadTime;
                }
            }
        }
    } else {
        updateSingleLoad(in, dt, events);
    }
}

float Weapon::actionOffset() const {
    const WeaponDef& d = def();
    if (d.action == ActionType::SemiAuto) {
        if (m_actionAnim <= 0.0f) return 0.0f;
        float t = 1.0f - m_actionAnim / kSlideAnimTime;
        return d.actionTravel * (t < 0.3f ? t / 0.3f : (1.0f - t) / 0.7f);
    }
    if (m_actionTimer <= 0.0f || d.actionCycleTime <= 0.0f) return 0.0f;
    float t = 1.0f - m_actionTimer / d.actionCycleTime;
    float f = t < 0.15f ? 0.0f : t < 0.5f ? smooth((t - 0.15f) / 0.35f) : t < 0.85f ? smooth(1.0f - (t - 0.5f) / 0.35f) : 0.0f;
    return d.actionTravel * f;
}

void Weapon::update(const WeaponInput& in, const WeaponContext& ctx, float dt, const World& world,
                    Ballistics& ballistics, std::vector<Impact>& impacts, std::vector<WeaponEvent>& events) {
    m_time += dt;
    m_yaw = ctx.yaw;
    m_exertion = ctx.exertion;
    m_armShake = ctx.armShake;
    m_noteTimer = std::max(0.0f, m_noteTimer - dt);

    // The equipped item can change under us (inventory, looting).
    uint32_t uid = item() ? item()->uid : 0;
    if (uid != m_curUid) {
        m_curUid = uid;
        resetTransient();
    }

    // Weapon change: lower the current one, swap, raise the new one.
    if (in.selectWeapon >= 0 && in.selectWeapon < 2 && m_switchTo < 0 && (in.selectWeapon != m_hand || m_switchBlend > 0.0f)) {
        m_switchTo = in.selectWeapon;
        m_reloadTimer = 0.0f;
        m_tubeLoading = false;
    }
    if (m_switchTo >= 0) {
        m_switchBlend = approach(m_switchBlend, 1.0f, dt / kSwitchOutTime);
        if (m_switchBlend >= 1.0f || !hasWeapon()) {
            m_switchBlend = 1.0f;
            m_hand = m_switchTo;
            m_switchTo = -1;
            m_curUid = item() ? item()->uid : 0;
            resetTransient();
            glm::vec3 a;
            glm::quat q;
            holsterPose(ctx, a, q);
            arm.reset(a, q);
        }
    } else {
        m_switchBlend = approach(m_switchBlend, 0.0f, dt / kSwitchInTime);
    }
    bool switching = isSwitching();
    bool lowered = ctx.busy || ctx.armsGone || (ctx.supportArmGone && !def().oneHanded && false);
    m_lowerBlend = approach(m_lowerBlend, lowered ? 1.0f : 0.0f, dt / 0.35f);

    Item* it = item();
    const WeaponDef& d = def();
    bool blocked = switching || m_lowerBlend > 0.2f || !it;

    if (it) {
        if (in.toggleFireMode && d.canAuto) it->fullAuto = !it->fullAuto;
        if (in.zoomSteps != 0.0f && optic().maxMag > optic().minMag)
            it->magnification = std::clamp(it->magnification * std::pow(1.15f, in.zoomSteps), optic().minMag, optic().maxMag);
        if (in.zeroSteps != 0) {
            it->zeroRange = std::clamp(it->zeroRange + 25.0f * float(in.zeroSteps), 25.0f, 600.0f);
            rezero();
        }
        updateReload(in, dt, blocked, events);
    }
    bool reloading = m_reloadTimer > 0.0f;

    // Feeding a round takes the hand off the grip: the cheek weld breaks partway.
    float t = it ? it->loadT : -1.0f;
    bool feeding = t >= 0.25f && t < 1.35f;
    m_loadBlend = approach(m_loadBlend, feeding ? 1.0f : 0.0f, dt / 0.2f);

    bool wantAds = in.adsHeld && !ctx.sprinting && !reloading && !blocked;
    float adsTarget = wantAds ? 1.0f - 0.5f * smooth(m_loadBlend) : 0.0f;
    float adsTime = d.adsTime / std::max(0.3f, ctx.armSpeed);
    m_adsBlend = approach(m_adsBlend, adsTarget, dt / adsTime);
    m_sprintBlend = approach(m_sprintBlend, ctx.sprinting ? 1.0f : 0.0f, dt / 0.3f);
    m_reloadBlend = approach(m_reloadBlend, reloading ? 1.0f : 0.0f, dt / 0.35f);

    // Breath holding (the heart-rate side lives in the body)
    bool canHold = in.holdBreath && wantAds && m_breathHeld < kMaxBreathHold;
    if (m_holdingBreath && !canHold && m_breathHeld >= kMaxBreathHold) m_gasp = true;
    if (canHold && !m_holdingBreath && m_breathHeld > kMaxBreathHold * 0.5f) canHold = false; // need to recover first
    m_holdingBreath = canHold;
    m_breathHeld = m_holdingBreath ? m_breathHeld + dt : std::max(0.0f, m_breathHeld - dt * 0.6f);

    updateDrift(ctx, dt);

    // Drive the weapon toward where the shooter wants it.
    glm::vec3 anchor;
    glm::quat target;
    computeTargetPose(ctx, world, dt, anchor, target);
    ArmParams p = lerpParams(kHipArm, kAdsArm, smooth(m_adsBlend));
    p = lerpParams(p, kSprintArm, smooth(m_sprintBlend));
    p = lerpParams(p, kReloadArm, smooth(m_reloadBlend));
    p.maxAngularSpeed *= d.armSpeedScale * ctx.armSpeed;
    p.maxAngularAccel *= d.armAccelScale * ctx.armSpeed;
    if (ctx.supportArmGone && !d.oneHanded) {
        p.maxAngularAccel *= 0.5f;
        p.positionStiffness *= 0.5f;
    }
    arm.step(anchor, ctx.bodyVelocity, target, p, dt);
    applyBodyMotion(ctx);
    if (ctx.armShake > 0.0f) {
        std::normal_distribution<float> n(0.0f, 1.0f);
        arm.addAngularVelocity(glm::vec3(n(m_rng), n(m_rng), n(m_rng)) * ctx.armShake * std::sqrt(dt * 240.0f) * 0.25f);
    }

    // Pulse: every heartbeat gives the weapon a small jolt; obvious through a scope at high BPM.
    m_heartPhase += ctx.heartRate / 60.0f * dt;
    if (m_heartPhase >= 1.0f) {
        m_heartPhase -= 1.0f;
        std::uniform_real_distribution<float> u11(-1.0f, 1.0f);
        float hr = ctx.heartRate / 72.0f;
        float kick = 0.04f * hr * hr * glm::mix(1.0f, 0.6f, smooth(m_adsBlend));
        arm.addAngularVelocity(arm.orientation * glm::vec3(kick, kick * 0.25f * u11(m_rng), 0.0f));
    }

    // Action cycling
    m_fireCooldown = std::max(0.0f, m_fireCooldown - dt);
    m_flashTimer = std::max(0.0f, m_flashTimer - dt);
    m_actionAnim = std::max(0.0f, m_actionAnim - dt);
    if (m_actionTimer > 0.0f) {
        float before = m_actionTimer;
        m_actionTimer = std::max(0.0f, m_actionTimer - dt);
        float ejectAt = d.actionCycleTime * 0.55f; // remaining time when the bolt/pump is fully back
        if (m_ejectPending && before > ejectAt && m_actionTimer <= ejectAt) {
            eject(events);
            event(events, WeaponEvent::Cycle, d.chamber);
            m_ejectPending = false;
        }
    }

    // Trigger
    if (!it) return;
    bool trigger = isFullAuto() ? in.fireHeld : in.firePressed;
    bool mechanical = !reloading && !blocked && m_sprintBlend < 0.3f && m_fireCooldown <= 0.0f && m_actionTimer <= 0.0f &&
                      it->loadT < 0.0f && !ctx.armsGone;
    if (trigger && mechanical) {
        if (roundReady()) fire(world, ballistics, impacts, events);
        else if (in.firePressed) {
            event(events, WeaponEvent::DryFire, d.chamber);
            m_fireCooldown = 0.25f;
        }
    }
}

namespace {
void drawParts(Renderer& r, const WeaponDef& d, const OpticDef& o, const glm::mat4& base, float action, float boltLift,
               float boltTravel, bool hands, int movedHandPart, const glm::vec3& movedHand) {
    bool hasOptic = o.type != OpticType::Iron;
    for (int i = 0; i < int(d.parts.size()); ++i) {
        const Part& part = d.parts[i];
        if (hasOptic && (part.flags & Part::RearSight)) continue;
        if ((part.flags & Part::Hand) && !hands) continue;
        if (i == movedHandPart) {
            Part moved = part;
            moved.center = movedHand;
            moved.tiltX = 0.0f;
            r.draw(r.box, partMatrix(base, moved, 0.0f, 0.0f, 0.0f), part.color);
            continue;
        }
        r.draw(part.cylinder ? r.cylinder : r.box, partMatrix(base, part, action, boltLift, boltTravel), part.color);
    }
    std::vector<Part> opticParts;
    buildOpticParts(d, o, opticParts);
    for (const Part& part : opticParts)
        r.draw(part.cylinder ? r.cylinder : r.box, partMatrix(base, part, 0.0f, 0.0f, 0.0f), part.color);
}
} // namespace

void drawWeaponItem(Renderer& r, const Item& item, const glm::mat4& base) {
    if (item.d().cat != ItemCat::Weapon) return;
    const WeaponDef& d = weaponDefs()[item.d().weapon];
    drawParts(r, d, opticDefs()[item.optic], base, 0.0f, 0.0f, 0.0f, false, -1, glm::vec3(0.0f));
}

void Weapon::draw(Renderer& r, float alpha) const {
    if (!hasWeapon()) return;
    if (!m_firstPerson && m_switchBlend > 0.6f) return; // between hands and back
    const WeaponDef& d = def();
    glm::vec3 pivot = glm::mix(arm.prevPivot, arm.pivot, alpha);
    glm::quat q = glm::slerp(arm.prevOrientation, arm.orientation, alpha);
    glm::mat4 base = glm::mat4_cast(q);
    base[3] = glm::vec4(pivot, 1.0f);
    float action = actionOffset();
    LoadPose load = loadPose();

    // The firing-hand part is the hand nearest the grip point; it leaves the grip while loading.
    int rightHandPart = -1;
    if (!load.handOnGrip) {
        float best = 1e9f;
        for (int i = 0; i < int(d.parts.size()); ++i) {
            if (!(d.parts[i].flags & Part::Hand)) continue;
            float dist = glm::length(d.parts[i].center - d.rightHand);
            if (dist < best) {
                best = dist;
                rightHandPart = i;
            }
        }
    }

    r.setWeaponLayer(m_firstPerson);
    drawParts(r, d, optic(), base, action, load.boltLift, load.boltTravel, true, rightHandPart, load.hand);

    if (load.showCartridge) {
        glm::mat4 m = glm::translate(base, load.cartridge);
        r.draw(r.cylinder, glm::scale(m, glm::vec3(0.012f, 0.012f, 0.055f)), kBrass);
        r.draw(r.cylinder, glm::scale(glm::translate(m, glm::vec3(0.0f, 0.0f, -0.035f)), glm::vec3(0.008f, 0.008f, 0.02f)), kCopper);
    }

    if (m_firstPerson) {
        // Forearms from the hands to elbows that hang from the body.
        glm::quat yawQ = glm::angleAxis(m_yaw, glm::vec3(0, 1, 0));
        glm::vec3 leftLocal = d.leftHand + glm::vec3(0.0f, 0.0f, d.leftHandOnAction ? action : 0.0f);
        glm::vec3 rightHand = pivot + q * load.hand;
        glm::vec3 leftHand = pivot + q * leftLocal;
        r.draw(r.box, segmentMatrix(rightHand, rightHand + yawQ * d.rightElbowFromHand, d.forearmThickness), kSleeve);
        r.draw(r.box, segmentMatrix(leftHand, leftHand + yawQ * d.leftElbowFromHand, d.forearmThickness), kSleeve);
    }

    if (m_flashTimer > 0.0f) {
        glm::vec3 fwd = q * glm::vec3(0, 0, -1);
        glm::vec3 mz = pivot + q * d.muzzle;
        float fade = m_flashTimer / 0.03f;
        glm::mat4 m = glm::mat4_cast(q);
        m[3] = glm::vec4(mz + fwd * 0.07f * m_flashScale, 1.0f);
        r.draw(r.sphere, glm::scale(m, glm::vec3(0.045f, 0.045f, 0.12f) * m_flashScale), glm::vec3(1.0f, 0.7f, 0.3f),
               Pattern::Glow, glm::vec4(0, 0, 0, 2.0f * fade), false);
        m[3] = glm::vec4(mz + fwd * 0.02f, 1.0f);
        r.draw(r.sphere, glm::scale(m, glm::vec3(0.09f) * m_flashScale), glm::vec3(1.0f, 0.55f, 0.2f), Pattern::Glow,
               glm::vec4(0, 0, 0, 0.35f * fade), false);
    }
    r.setWeaponLayer(false);
}
