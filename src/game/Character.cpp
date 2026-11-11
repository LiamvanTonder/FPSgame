#include "game/Character.h"

#include "audio/Sound.h"
#include "game/Effects.h"
#include "physics/World.h"
#include "render/Camera.h"
#include "render/Renderer.h"
#include "world/MapData.h"
#include "world/Terrain.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace {

const float kTick = 1.0f / 240.0f;
const float kUpperArm = 0.28f, kForearm = 0.27f, kThigh = 0.42f, kShin = 0.44f;

const glm::vec3 kSkin(0.72f, 0.56f, 0.44f);
const glm::vec3 kBoots(0.12f, 0.10f, 0.08f);

float smooth(float t) { return t * t * (3.0f - 2.0f * t); }
float u01(std::mt19937& rng) { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
float u11(std::mt19937& rng) { return std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng); }
glm::vec3 randUnit(std::mt19937& rng) {
    glm::vec3 v(u11(rng), u11(rng), u11(rng));
    float l = glm::length(v);
    return l > 1e-4f ? v / l : glm::vec3(0, 1, 0);
}

// Two-bone IK. Returns the middle joint; 'end' is pulled in if out of reach.
glm::vec3 solveIK(const glm::vec3& root, glm::vec3& end, float a, float b, const glm::vec3& pole) {
    glm::vec3 d = end - root;
    float len = glm::length(d);
    glm::vec3 dir = len > 1e-4f ? d / len : glm::vec3(0, -1, 0);
    float L = std::clamp(len, std::abs(a - b) + 0.01f, (a + b) * 0.999f);
    end = root + dir * L;
    float cosA = std::clamp((a * a + L * L - b * b) / (2.0f * a * L), -1.0f, 1.0f);
    float sinA = std::sqrt(1.0f - cosA * cosA);
    glm::vec3 p = pole - dir * glm::dot(pole, dir);
    if (glm::length(p) < 1e-4f) p = glm::abs(dir.y) < 0.9f ? glm::cross(dir, glm::vec3(0, 1, 0)) : glm::vec3(1, 0, 0);
    p = glm::normalize(p);
    return root + dir * (a * cosA) + p * (a * sinA);
}

// Box along A->B (extended at both ends), oriented with 'side' as its X axis.
OBB segmentBox(const glm::vec3& A, const glm::vec3& B, float width, float depth, float extA, float extB, const glm::vec3& side) {
    glm::vec3 d = B - A;
    float len = glm::length(d);
    glm::vec3 axis = len > 1e-5f ? d / len : glm::vec3(0, 1, 0);
    glm::vec3 start = A - axis * extA, end = B + axis * extB;
    glm::vec3 x = side - axis * glm::dot(side, axis);
    if (glm::length(x) < 1e-4f) x = glm::abs(axis.y) < 0.9f ? glm::cross(glm::vec3(0, 1, 0), axis) : glm::vec3(1, 0, 0);
    x = glm::normalize(x);
    glm::vec3 z = glm::cross(x, axis);
    return OBB{(start + end) * 0.5f, glm::vec3(width * 0.5f, glm::length(end - start) * 0.5f, depth * 0.5f), glm::mat3(x, axis, z)};
}

void buildBodyBoxes(const JointArray& j, OBB out[kBodyPartCount]) {
    glm::vec3 shoulders = j[J_RShoulder] - j[J_LShoulder];
    glm::vec3 hips = j[J_RHip] - j[J_LHip];
    out[BP_Head] = segmentBox(j[J_Neck], j[J_Head], 0.19f, 0.22f, -0.04f, 0.11f, shoulders);
    out[BP_Chest] = segmentBox(j[J_Chest], j[J_Neck], 0.40f, 0.24f, 0.10f, 0.02f, shoulders);
    out[BP_Pelvis] = segmentBox(j[J_Pelvis], j[J_Chest], 0.36f, 0.22f, 0.10f, -0.12f, hips);
    out[BP_UpperArmL] = segmentBox(j[J_LShoulder], j[J_LElbow], 0.11f, 0.11f, 0.03f, 0.03f, shoulders);
    out[BP_UpperArmR] = segmentBox(j[J_RShoulder], j[J_RElbow], 0.11f, 0.11f, 0.03f, 0.03f, shoulders);
    out[BP_ForearmL] = segmentBox(j[J_LElbow], j[J_LHand], 0.09f, 0.09f, 0.0f, 0.08f, shoulders);
    out[BP_ForearmR] = segmentBox(j[J_RElbow], j[J_RHand], 0.09f, 0.09f, 0.0f, 0.08f, shoulders);
    out[BP_ThighL] = segmentBox(j[J_LHip], j[J_LKnee], 0.16f, 0.16f, 0.04f, 0.0f, hips);
    out[BP_ThighR] = segmentBox(j[J_RHip], j[J_RKnee], 0.16f, 0.16f, 0.04f, 0.0f, hips);
    out[BP_ShinL] = segmentBox(j[J_LKnee], j[J_LFoot], 0.12f, 0.12f, 0.0f, 0.06f, hips);
    out[BP_ShinR] = segmentBox(j[J_RKnee], j[J_RFoot], 0.12f, 0.12f, 0.0f, 0.06f, hips);
}

AABB bodyBox(const glm::vec3& feet, float radius, float height) {
    return AABB{feet - glm::vec3(radius, 0.0f, radius), feet + glm::vec3(radius, height, radius)};
}

// Clothing colours: everyone looks a little different (free-for-all, no uniforms).
glm::vec3 jacketColor(int id) {
    const glm::vec3 c[] = {{0.36f, 0.38f, 0.27f}, {0.42f, 0.36f, 0.26f}, {0.28f, 0.30f, 0.33f}, {0.30f, 0.34f, 0.22f},
                           {0.40f, 0.40f, 0.38f}, {0.24f, 0.26f, 0.20f}, {0.45f, 0.40f, 0.30f}, {0.22f, 0.24f, 0.28f}};
    return c[(id * 5 + 3) % 8];
}
glm::vec3 trouserColor(int id) {
    const glm::vec3 c[] = {{0.30f, 0.31f, 0.25f}, {0.25f, 0.24f, 0.22f}, {0.34f, 0.30f, 0.24f}, {0.20f, 0.21f, 0.24f}};
    return c[(id * 3 + 1) % 4];
}

} // namespace

const char* bodyPartName(int part) {
    static const char* names[kBodyPartCount] = {"head", "chest", "pelvis", "left upper arm", "right upper arm",
                                                "left forearm", "right forearm", "left thigh", "right thigh",
                                                "left shin", "right shin"};
    return part >= 0 && part < kBodyPartCount ? names[part] : "?";
}

void Character::create(World& world, int ownerId, bool fp) {
    id = ownerId;
    firstPerson = fp;
    rng.seed(9001u + unsigned(ownerId) * 104729u);
    for (int p = 0; p < kBodyPartCount; ++p) {
        Collider c;
        c.surface = Surface::Flesh;
        c.blocksPlayer = false;
        c.active = false;
        c.dynamic = true;
        c.owner = ownerId;
        c.bodyPart = p;
        m_colliders[p] = world.add(c);
    }
}

void Character::spawn(World& world, const glm::vec3& feet, float yawAngle) {
    position = feet;
    velocity = glm::vec3(0.0f);
    yaw = yawAngle;
    pitch = 0.0f;
    height = params.standHeight;
    lean = 0.0f;
    heartRate = params.restingHeartRate;
    grounded = true;
    crouching = sprinting = false;
    health.reset();
    dead = false;
    pooled = false;
    searched = false;
    posed = false;
    stagger = suppression = 0.0f;
    causeOfDeath.clear();
    killer = -1;
    kills = 0;
    active = true;
    m_action = Action::None;
    prevEye = eye();
    aimPoint = eye() + viewDir() * 100.0f;

    WeaponContext ctx;
    ctx.eye = eye();
    ctx.yaw = yaw;
    ctx.aimPoint = aimPoint;
    weapon.init(ctx, &equip, id, firstPerson);
    animate(0.0f);
    prevJoints = joints;
    for (int c : m_colliders) world.collider(c).active = true;
    updateColliders(world);
}

void Character::despawn(World& world) {
    active = false;
    for (int c : m_colliders) world.collider(c).active = false;
}

glm::vec3 Character::eyeUnleaned() const { return position + glm::vec3(0.0f, height - params.eyeBelowTop, 0.0f); }
glm::vec3 Character::leanPivot() const { return position + glm::vec3(0.0f, height * params.leanPivotFraction, 0.0f); }
glm::vec3 Character::eyeForLean(float leanAmount) const {
    glm::vec3 pivot = leanPivot();
    return pivot + glm::angleAxis(leanAmount * params.maxLeanAngle, flatForward()) * (eyeUnleaned() - pivot);
}
glm::vec3 Character::eye() const {
    if (dead) return joints[J_Head];
    return eyeForLean(lean);
}
glm::vec3 Character::viewDir() const { return directionFromYawPitch(yaw, pitch); }
glm::vec3 Character::flatForward() const { return glm::vec3(-std::sin(yaw), 0.0f, -std::cos(yaw)); }
glm::vec3 Character::right() const { return glm::vec3(std::cos(yaw), 0.0f, -std::sin(yaw)); }

float Character::exertion() const {
    float e = (heartRate - params.restingHeartRate) / (params.maxHeartRate - params.restingHeartRate);
    return std::clamp(e, 0.0f, 1.0f);
}

bool Character::overlapsWorld(const World& world) const {
    AABB box = bodyBox(position, params.radius, height);
    std::vector<int>& list = const_cast<std::vector<int>&>(m_overlap);
    world.overlap(box, list);
    for (int i : list) {
        const Collider& c = world.collider(i);
        if (c.blocksPlayer && c.active && box.overlaps(c.box.bounds())) return true;
    }
    return false;
}

void Character::moveAxis(int axis, float delta, const World& world) {
    if (delta == 0.0f) return;
    position[axis] += delta;
    AABB box = bodyBox(position, params.radius, height);
    AABB query = box;
    query.min -= glm::vec3(0.05f);
    query.max += glm::vec3(0.05f, params.stepHeight, 0.05f);
    world.overlap(query, m_overlap);
    std::vector<int> list = m_overlap;

    for (int i : list) {
        const Collider& c = world.collider(i);
        if (!c.blocksPlayer || !c.active) continue;
        AABB cb = c.box.bounds();
        if (!box.overlaps(cb)) continue;

        if (axis != 1 && grounded && cb.max.y - position.y <= params.stepHeight) {
            // Step onto the highest low surface touched (a floor laid on a foundation, stacked crates).
            float oldY = position.y;
            float top = cb.max.y;
            for (int j : list) {
                const Collider& o = world.collider(j);
                if (!o.blocksPlayer || !o.active) continue;
                AABB ob = o.box.bounds();
                if (box.overlaps(ob) && ob.max.y > top && ob.max.y - oldY <= params.stepHeight) top = ob.max.y;
            }
            position.y = top + 0.001f;
            if (!overlapsWorld(world)) {
                box = bodyBox(position, params.radius, height);
                continue;
            }
            position.y = oldY;
        }
        if (axis == 1) {
            if (delta < 0.0f) {
                position.y = cb.max.y;
                if (!grounded) landingSpeed = -velocity.y;
                grounded = true;
            } else {
                position.y = cb.min.y - height;
            }
        } else {
            float r = params.radius;
            position[axis] = delta > 0.0f ? cb.min[axis] - r - 0.0001f : cb.max[axis] + r + 0.0001f;
        }
        velocity[axis] = 0.0f;
        box = bodyBox(position, params.radius, height);
    }

    if (const Terrain* t = world.terrain()) {
        float h = t->height(position.x, position.z);
        if (axis != 1) {
            // Too steep to climb: the edge berms.
            glm::vec3 n = t->normal(position.x, position.z);
            float rise = h - position.y;
            if (n.y < 0.62f && rise > 0.02f) {
                position[axis] -= delta;
                velocity[axis] = 0.0f;
            }
        } else if (position.y < h) {
            if (delta < 0.0f && !grounded) landingSpeed = -velocity.y;
            position.y = h;
            grounded = true;
            velocity.y = std::max(velocity.y, 0.0f);
        }
    }
}

void Character::updateMovement(const CharacterInput& in, float dt, const World& world) {
    // Crouch / stand (only stand up if there is headroom). Without legs, the body stays low.
    crouching = in.crouch || health.legsGone();
    float targetHeight = crouching ? params.crouchHeight : params.standHeight;
    if (targetHeight > height) {
        float old = height;
        height = std::min(targetHeight, height + dt * 3.0f);
        if (overlapsWorld(world)) height = old;
    } else {
        height = std::max(targetHeight, height - dt * 4.0f);
    }

    glm::vec3 wish = flatForward() * in.move.y + right() * in.move.x;
    float wishLen = glm::length(wish);
    if (wishLen > 1.0f) wish /= wishLen;

    bool aiming = in.weapon.adsHeld;
    sprinting = in.sprint && in.move.y > 0.5f && !crouching && !aiming && grounded && health.canSprint() &&
                m_action != Action::Med && m_action != Action::LoadMag;
    float speed = sprinting ? params.sprintSpeed : crouching ? params.crouchSpeed : aiming ? params.aimWalkSpeed : params.walkSpeed;
    speed *= health.moveSpeedScale();
    if (stagger > 0.0f) speed *= 0.4f;
    if (m_action == Action::Med || m_action == Action::Search) speed = std::min(speed, params.aimWalkSpeed);

    glm::vec3 horiz(velocity.x, 0.0f, velocity.z);
    glm::vec3 target = wish * speed;
    float accel = grounded ? params.groundAccel : params.airAccel;
    glm::vec3 diff = target - horiz;
    float diffLen = glm::length(diff);
    float maxDelta = accel * dt;
    if (diffLen > maxDelta) diff *= maxDelta / diffLen;
    horiz += diff;
    velocity.x = horiz.x;
    velocity.z = horiz.z;

    if (grounded && in.jump && !crouching && health.canSprint()) {
        velocity.y = params.jumpSpeed;
        grounded = false;
    }
    velocity.y -= 9.81f * dt;

    bool wasGrounded = grounded;
    moveAxis(0, velocity.x * dt, world);
    moveAxis(2, velocity.z * dt, world);
    grounded = false;
    moveAxis(1, velocity.y * dt, world);
    if (!grounded && wasGrounded && velocity.y <= 0.0f) {
        // Walking downhill / off a small ledge: stay on the ground.
        float g = world.groundHeight(position + glm::vec3(0, 0.3f, 0), position.y + 0.3f, -1e9f);
        if (const Terrain* t = world.terrain()) g = std::max(g, t->height(position.x, position.z));
        if (g > position.y - 0.35f && g <= position.y + 0.01f) {
            position.y = g;
            grounded = true;
            velocity.y = 0.0f;
        }
    }
    if (wasGrounded) landingSpeed = 0.0f;
    if (landingSpeed > 6.0f) health.applyFall(landingSpeed, rng);

    if (grounded) {
        m_strideAccum += horizontalSpeed() * dt;
        float stride = params.strideLength * (sprinting ? 1.5f : 1.0f);
        if (m_strideAccum >= stride) {
            m_strideAccum -= stride;
            footstep = m_nextFoot;
            m_nextFoot = -m_nextFoot;
        }
    }
    walkPhase += dt * horizontalSpeed() / 0.7f * float(M_PI);
}

void Character::updateLean(float target, float dt, const World& world) {
    if (sprinting) target = 0.0f;
    float step = dt / params.leanTime;
    float desired = lean + std::clamp(target - lean, -step, step);
    SegmentQuery q;
    q.staticOnly = true;
    for (int i = 0; i < 8 && desired != 0.0f; ++i) {
        glm::vec3 pivot = leanPivot();
        glm::vec3 d = eyeForLean(desired) - pivot;
        float len = glm::length(d);
        WorldHit hit;
        if (!world.segment(pivot, pivot + d / len * (len + 0.15f), hit, q)) break;
        desired *= 0.6f;
        if (std::abs(desired) < 0.02f) desired = 0.0f;
    }
    lean = desired;
}

void Character::updateHeart(const CharacterInput&, float dt, bool holdingBreath, bool gasp) {
    float target = params.restingHeartRate;
    if (grounded) target += std::min(horizontalSpeed() / params.walkSpeed, 1.0f) * 23.0f;
    if (sprinting) target = 168.0f;
    if (holdingBreath) target = std::max(target, heartRate + 6.0f);
    // Wounds and blood loss push the heart rate up.
    target += (1.0f - health.totalHp() / health.totalMax()) * 40.0f + health.pain() * 10.0f;
    float tau = target > heartRate ? params.heartRiseTau : params.heartRecoverTau;
    heartRate += (target - heartRate) * (1.0f - std::exp(-dt / tau));
    if (landingSpeed > 2.0f) heartRate += landingSpeed * 1.5f;
    if (gasp) heartRate += 12.0f;
    heartRate = std::clamp(heartRate, params.restingHeartRate - 5.0f, params.maxHeartRate);
}

bool Character::startMed(uint32_t uid, int zone) {
    Item* it = equip.find(uid);
    if (!it || it->d().cat != ItemCat::Med || dead) return false;
    MedKind kind = it->d().med;
    if (zone < 0) zone = health.bestZoneFor(kind);
    if (!health.canTreat(kind, zone)) return false;
    m_action = Action::Med;
    m_actionTime = 0.0f;
    m_actionDuration = it->d().useTime;
    m_actionItem = uid;
    m_actionZone = zone;
    return true;
}

bool Character::startLoadMagazine(uint32_t magUid, uint32_t ammoUid) {
    Item* mag = equip.find(magUid);
    Item* ammo = equip.find(ammoUid);
    if (!mag || !ammo || mag->d().cat != ItemCat::Magazine || ammo->d().cat != ItemCat::Ammo) return false;
    int cal = ammo->d().caliber;
    if (mag->d().caliber >= 0 && mag->d().caliber != cal) return false;
    if (mag->d().caliber < 0 && cal != Cal_556 && cal != Cal_300BLK) return false;
    if (!mag->rounds.empty() && ammoDefs()[mag->rounds.back()].caliber != cal) return false;
    if (int(mag->rounds.size()) >= mag->d().capacity) return false;
    m_action = Action::LoadMag;
    m_actionTime = 0.0f;
    m_actionDuration = 0.4f;
    m_actionItem = magUid;
    m_actionItem2 = ammoUid;
    return true;
}

void Character::setBusy(Action a, float duration) {
    m_action = a;
    m_actionTime = 0.0f;
    m_actionDuration = duration;
}

void Character::cancelAction() {
    m_action = Action::None;
    m_actionTime = m_actionDuration = 0.0f;
}

std::string Character::actionLabel() const {
    switch (m_action) {
    case Action::Med: {
        Item* it = const_cast<Equipment&>(equip).find(m_actionItem);
        return std::string(it ? it->d().name : "Treating") + " - " + zoneName(m_actionZone);
    }
    case Action::LoadMag: {
        Item* it = const_cast<Equipment&>(equip).find(m_actionItem);
        if (!it) return "Loading";
        return "Loading magazine " + std::to_string(it->rounds.size()) + "/" + std::to_string(it->d().capacity);
    }
    case Action::Search: return "Searching";
    case Action::Extract: return "Extracting";
    case Action::SendBack: return "Sending weapons home";
    default: return "";
    }
}

void Character::updateAction(float dt, const CharacterInput& in, SimContext& sc) {
    if (m_action == Action::None) return;
    bool interrupt = in.cancelAction || sprinting || in.weapon.firePressed;
    if (m_action == Action::Extract) interrupt = in.cancelAction; // you can fight while waiting for the ride
    if (interrupt) {
        cancelAction();
        return;
    }
    m_actionTime += dt;
    switch (m_action) {
    case Action::Med:
        if (m_actionTime >= m_actionDuration) {
            Item* it = equip.find(m_actionItem);
            if (it && it->d().cat == ItemCat::Med) {
                MedKind kind = it->d().med;
                float pool = it->pool;
                health.treat(kind, m_actionZone, pool);
                it->pool = pool;
                it->uses -= kind == MedKind::FieldKit ? 0 : 1;
                SoundEvent e;
                e.type = SoundType::Med;
                e.position = chest();
                e.db = 62.0f;
                e.owner = id;
                e.variant = int(kind);
                sc.sound.emit(e);
                if (it->uses <= 0 || (kind == MedKind::FieldKit && it->pool <= 0.0f)) equip.remove(m_actionItem);
            }
            cancelAction();
        }
        break;
    case Action::LoadMag:
        while (m_actionTime >= m_actionDuration && m_action == Action::LoadMag) {
            m_actionTime -= m_actionDuration;
            Item* mag = equip.find(m_actionItem);
            Item* ammo = equip.find(m_actionItem2);
            if (!mag || !ammo || ammo->count <= 0 || int(mag->rounds.size()) >= mag->d().capacity) {
                cancelAction();
                break;
            }
            mag->rounds.push_back(uint8_t(ammo->d().ammo));
            --ammo->count;
            SoundEvent e;
            e.type = SoundType::Reload;
            e.variant = 3;
            e.position = chest();
            e.db = 55.0f;
            e.owner = id;
            e.gain = 0.5f;
            sc.sound.emit(e);
            if (ammo->count <= 0) {
                equip.remove(m_actionItem2);
                cancelAction();
            } else if (int(mag->rounds.size()) >= mag->d().capacity) {
                cancelAction();
            }
        }
        break;
    default:
        m_actionTime = std::min(m_actionTime, m_actionDuration);
        break;
    }
}

void Character::animate(float dt) {
    // The hips follow the head with a lag: small looks twist the torso, big turns bring the feet round.
    float twistErr = yaw - bodyYaw;
    while (twistErr > float(M_PI)) twistErr -= 2.0f * float(M_PI);
    while (twistErr < -float(M_PI)) twistErr += 2.0f * float(M_PI);
    float follow = horizontalSpeed() > 0.3f ? 6.0f : (std::abs(twistErr) > 0.7f ? 5.0f : 0.8f);
    if (!posed || dt <= 0.0f) bodyYaw = yaw;
    else bodyYaw += twistErr * std::min(1.0f, follow * dt);
    const glm::quat yq = glm::angleAxis(yaw, glm::vec3(0, 1, 0));
    const glm::quat hq = glm::angleAxis(bodyYaw, glm::vec3(0, 1, 0));
    auto H = [&](const glm::vec3& local) { return position + hq * local; };
    auto W = [&](const glm::vec3& local) { return position + yq * local; };
    JointArray& j = joints;
    if (posed) prevJoints = j;

    float c = std::clamp((params.standHeight - height) / (params.standHeight - params.crouchHeight), 0.0f, 1.0f);
    float cs = smooth(c);
    bool kneel = health.legsGone();
    float spd = horizontalSpeed();
    float moving = std::clamp(spd / 1.0f, 0.0f, 1.0f);

    // Legs: walk cycle, limp, stance, kneel
    float stride = 0.32f * std::clamp(spd / 1.5f, 0.0f, 1.6f);
    float bob = 0.03f * std::abs(std::sin(walkPhase)) * moving;
    float limpDip = 0.0f;
    glm::vec3 feet[2];
    for (int li = 0; li < 2; ++li) {
        float side = li == 0 ? -1.0f : 1.0f;
        float a = walkPhase + (li == 0 ? 0.0f : float(M_PI));
        bool hurt = health.legImpaired(li);
        float s = stride * (hurt ? 0.5f : 1.0f);
        glm::vec3 f(side * 0.12f, std::max(0.0f, std::cos(a)) * 0.10f * moving, -std::sin(a) * s);
        f.z += (li == 0 ? -0.12f : 0.10f) * (1.0f - moving) + (li == 0 ? -0.15f : 0.15f) * cs;
        if (hurt && std::cos(a) < 0.0f) limpDip = 0.06f * moving * -std::cos(a);
        feet[li] = f;
    }
    // Lean the torso forward a little; aim the head at the eye.
    float tilt = 0.12f + 0.25f * cs - std::min(stagger, 0.5f) * 0.6f;
    glm::vec3 up(0.0f, std::cos(tilt), -std::sin(tilt));
    float eyeH = height - params.eyeBelowTop;
    float pelvisH = std::clamp(eyeH - 0.72f * up.y, 0.3f, 1.0f) - bob - limpDip;
    if (kneel) {
        feet[0] = glm::vec3(-0.13f, 0.0f, -0.30f);
        feet[1] = glm::vec3(0.12f, 0.06f, 0.45f);
    }
    glm::vec3 pelvis(0.0f, pelvisH, 0.0f);
    glm::vec3 neck = pelvis + up * 0.54f;
    bool armed = weapon.hasWeapon() && !weapon.isLowered();
    glm::quat twist = glm::angleAxis(armed ? -0.35f : 0.0f, glm::vec3(0, 1, 0)); // bladed stance behind a rifle

    j[J_Pelvis] = W(pelvis);
    j[J_Chest] = W(pelvis + up * 0.32f);
    j[J_Neck] = W(neck);
    j[J_Head] = W(pelvis + up * 0.72f);
    j[J_LShoulder] = W(neck + twist * glm::vec3(-0.19f, -0.06f, 0.0f));
    j[J_RShoulder] = W(neck + twist * glm::vec3(0.19f, -0.06f, 0.0f));
    j[J_LHip] = H(pelvis + glm::vec3(-0.10f, -0.06f, 0.0f));
    j[J_RHip] = H(pelvis + glm::vec3(0.10f, -0.06f, 0.0f));
    j[J_LFoot] = H(feet[0]);
    j[J_RFoot] = H(feet[1]);

    // Lean: the upper body rotates about the lean pivot.
    if (std::abs(lean) > 1e-3f) {
        glm::vec3 pivot = leanPivot();
        glm::quat lq = glm::angleAxis(leanAngle(), flatForward());
        for (int k : {J_Chest, J_Neck, J_Head, J_LShoulder, J_RShoulder}) j[k] = pivot + lq * (j[k] - pivot);
    }
    glm::vec3 fwd = yq * glm::vec3(0, 0, -1);
    glm::vec3 hipFwd = hq * glm::vec3(0, 0, -1);
    j[J_LKnee] = solveIK(j[J_LHip], j[J_LFoot], kThigh, kShin, hipFwd);
    j[J_RKnee] = solveIK(j[J_RHip], j[J_RFoot], kThigh, kShin, hipFwd);

    // Arms: on the weapon's grips, wherever the physical rig has put it.
    glm::vec3 rightV = yq * glm::vec3(1, 0, 0);
    auto hanging = [&](int shoulder, float side) {
        return j[shoulder] + glm::vec3(0.0f, -0.52f, 0.0f) + fwd * (0.05f + std::sin(walkPhase) * 0.08f * moving * side) +
               rightV * side * 0.04f;
    };
    glm::vec3 handR = hanging(J_RShoulder, 1.0f), handL = hanging(J_LShoulder, -1.0f);
    if (armed && !weapon.isSwitching()) {
        const WeaponDef& d = weapon.def();
        handR = weapon.arm.pivot + weapon.arm.orientation * d.rightHand;
        if (!health.armGone(0)) handL = weapon.arm.pivot + weapon.arm.orientation * d.leftHand;
    }
    j[J_RHand] = handR;
    j[J_LHand] = handL;
    j[J_RElbow] = solveIK(j[J_RShoulder], j[J_RHand], kUpperArm, kForearm, yq * glm::vec3(0.8f, -1.0f, 0.3f));
    j[J_LElbow] = solveIK(j[J_LShoulder], j[J_LHand], kUpperArm, kForearm, yq * glm::vec3(-0.8f, -1.0f, 0.3f));

    if (!posed) {
        prevJoints = j;
        posed = true;
    }
}

void Character::updateColliders(World& world) const {
    OBB boxes[kBodyPartCount];
    buildBodyBoxes(joints, boxes);
    for (int i = 0; i < kBodyPartCount; ++i) {
        Collider& c = world.collider(m_colliders[i]);
        c.box = boxes[i];
        c.active = active;
    }
}

void Character::update(const CharacterInput& in, float dt, SimContext& sc) {
    if (!active) return;
    prevEye = eye();
    footstep = 0;
    landingSpeed = 0.0f;

    if (dead) {
        prevJoints = joints;
        ragdoll.step(dt, sc.world);
        joints = ragdoll.joints();
        prevEye = eye();
        if (!pooled && sc.time - deathTime > 1.2) {
            sc.fx.bloodPool(joints[J_Pelvis], sc.world, 0.5f + (health.bleeding() ? 0.3f : 0.0f));
            pooled = true;
        }
        updateColliders(sc.world);
        return;
    }

    health.update(dt);
    if (health.dead()) {
        kill(health.causeOfDeath, health.killer, joints[J_Chest], glm::vec3(0.0f), sc);
        return;
    }

    yaw = in.yaw;
    pitch = std::clamp(in.pitch, -1.5f, 1.5f);
    updateMovement(in, dt, sc.world);
    updateLean(in.lean, dt, sc.world);
    if (in.useMed && !(m_action == Action::Med && m_actionItem == in.useMed)) startMed(in.useMed, in.useMedZone);
    updateAction(dt, in, sc);

    // What the eyes are on: the weapon converges there.
    glm::vec3 e = eye(), dir = viewDir();
    WorldHit hit;
    SegmentQuery look;
    look.ignoreOwner = id;
    aimPoint = sc.world.segment(e, e + dir * 800.0f, hit, look) ? hit.point : e + dir * 800.0f;

    WeaponContext ctx;
    ctx.eye = e;
    ctx.bodyVelocity = velocity;
    ctx.yaw = yaw;
    ctx.pitch = pitch;
    ctx.aimPoint = aimPoint;
    ctx.sprinting = sprinting;
    ctx.moveSpeed = horizontalSpeed();
    ctx.footstep = footstep;
    ctx.landingSpeed = landingSpeed;
    ctx.leanAngle = leanAngle();
    ctx.crouching = crouching;
    ctx.heartRate = heartRate;
    ctx.exertion = exertion();
    ctx.armShake = health.armShake() + (stagger > 0.0f ? 0.02f : 0.0f);
    ctx.armSpeed = health.armSpeedScale();
    ctx.supportArmGone = health.armGone(0);
    ctx.armsGone = health.armGone(0) && health.armGone(1);
    ctx.busy = m_action == Action::Med || m_action == Action::LoadMag || m_action == Action::Search || m_action == Action::SendBack;
    weapon.update(in.weapon, ctx, dt, sc.world, sc.ballistics, sc.impacts, sc.events);
    updateHeart(in, dt, weapon.isHoldingBreath(), weapon.consumeGasp());
    for (DroppedItem& d : weapon.drops()) sc.drops.push_back(std::move(d));
    weapon.drops().clear();

    // Bleeding leaves a trail.
    if (health.bleeding()) {
        m_dripTimer -= dt * (health.heavyBleeding() ? 2.5f : 0.8f);
        if (m_dripTimer <= 0.0f) {
            sc.fx.bloodDrip(joints[J_Pelvis] + randUnit(rng) * 0.12f, sc.world);
            m_dripTimer = 1.0f;
        }
    }
    stagger = std::max(0.0f, stagger - dt);
    suppression = std::max(0.0f, suppression - 0.35f * dt);

    animate(dt);
    updateColliders(sc.world);

    if (footstep != 0) {
        SoundEvent s;
        s.type = SoundType::Footstep;
        s.position = position;
        s.owner = id;
        s.db = sprinting ? 76.0f : crouching ? 61.0f : in.weapon.adsHeld ? 65.0f : 70.0f;
        WorldHit g;
        SegmentQuery q;
        q.staticOnly = true;
        Surface surf = Surface::Grass;
        if (sc.world.segment(position + glm::vec3(0, 0.2f, 0), position - glm::vec3(0, 0.3f, 0), g, q)) {
            const Collider& col = sc.world.collider(g.collider);
            surf = col.surface;
            if (col.terrain && sc.map) {
                int gnd = sc.map->groundAt(position.x, position.z);
                surf = gnd == 1 ? Surface::Dirt : gnd == 2 ? Surface::Foliage : Surface::Grass;
            }
        }
        s.variant = int(surf);
        sc.sound.emit(s);

        // Kit rattles when running.
        if (sprinting) {
            SoundEvent r;
            r.type = SoundType::Rustle;
            r.variant = 1;
            r.position = chest();
            r.db = 60.0f;
            r.owner = id;
            sc.sound.emit(r);
        }
        // Pushing through bushes and low branches is loud.
        AABB body = bodyBox(position, params.radius, height);
        {
            sc.world.overlap(body, m_overlap);
            for (int ci : m_overlap) {
                const Collider& c = sc.world.collider(ci);
                if (c.surface != Surface::Foliage || c.blocksPlayer || !body.overlaps(c.box.bounds())) continue;
                SoundEvent r;
                r.type = SoundType::Rustle;
                r.variant = 0;
                r.position = chest();
                r.db = crouching ? 60.0f : 68.0f;
                r.owner = id;
                sc.sound.emit(r);
                break;
            }
        }
    }

    // The wounded groan now and then; the winded breathe hard.
    if (health.bleeding() || health.pain() > 0.5f) {
        m_groanTimer -= dt;
        if (m_groanTimer <= 0.0f) {
            SoundEvent s;
            s.type = SoundType::Pain;
            s.variant = 1;
            s.position = head();
            s.db = 68.0f;
            s.owner = id;
            sc.sound.emit(s);
            m_groanTimer = 6.0f + 6.0f * u01(rng);
        }
    }
    if (exertion() > 0.6f && !weapon.isHoldingBreath()) {
        m_breathTimer -= dt;
        if (m_breathTimer <= 0.0f) {
            SoundEvent s;
            s.type = SoundType::Breath;
            s.position = head();
            s.db = 54.0f;
            s.owner = id;
            sc.sound.emit(s);
            m_breathTimer = 2.2f + u01(rng) * 0.6f;
        }
    }
}

std::string Character::onHit(int part, const Impact& imp, SimContext& sc) {
    float v = glm::length(imp.velocity);
    float energy = 0.5f * imp.mass * v * v;
    glm::vec3 impulse = imp.velocity * imp.mass;
    std::string who = name;
    if (dead) {
        ragdoll.applyImpulse(imp.point, impulse * 4.0f);
        return who + ": body hit";
    }
    int ammoIdx = imp.ammo >= 0 ? imp.ammo : defaultAmmo(Cal_556);
    HitOutcome o = health.applyHit(part, energy, ammoDefs()[ammoIdx], imp.shooter, rng);
    float k = energy / 1000.0f;
    if (part == BP_Chest || part == BP_Pelvis) stagger = std::max(stagger, 0.5f);
    else stagger = std::max(stagger, 0.2f);
    weapon.arm.addAngularVelocity(randUnit(rng) * std::min(3.0f, 1.5f + 1.5f * k));
    suppression = 1.0f;
    cancelAction();

    SoundEvent s;
    s.type = SoundType::Pain;
    s.position = joints[J_Head];
    s.db = 80.0f;
    s.owner = id;
    sc.sound.emit(s);

    // Both arms gone: the weapon falls out of the hands.
    if (health.armGone(0) && health.armGone(1) && weapon.hasWeapon()) {
        DroppedItem d;
        glm::mat4 m = weapon.worldTransform(1.0f);
        d.position = glm::vec3(m[3]);
        d.rotation = glm::quat_cast(glm::mat3(m));
        d.velocity = velocity + randUnit(rng) * 0.4f;
        d.item = std::move(equip.slots[weapon.hand()]);
        sc.drops.push_back(std::move(d));
    }
    if (health.dead()) kill(part == BP_Head ? "head shot" : "gunshot wound", imp.shooter, imp.point, impulse * (part == BP_Head ? 6.0f : 4.0f), sc);
    return who + ": " + bodyPartName(part) + " - " + o.describe(part);
}

void Character::kill(const std::string& cause, int killerId, const glm::vec3& point, const glm::vec3& impulse, SimContext& sc) {
    if (dead) return;
    JointArray vel;
    for (int i = 0; i < kJointCount; ++i) {
        vel[i] = (joints[i] - prevJoints[i]) / kTick;
        if (glm::length(vel[i]) > 6.0f) vel[i] = glm::normalize(vel[i]) * 6.0f;
    }
    ragdoll.init(joints, vel, kTick);
    glm::vec3 fwd = flatForward();
    glm::vec3 fall = glm::length(impulse) > 1e-3f ? glm::normalize(glm::vec3(impulse.x, 0.0f, impulse.z) + glm::vec3(1e-4f, 0, 0))
                                                    : fwd * (u01(rng) < 0.5f ? -1.0f : 1.0f);
    ragdoll.applyImpulse(joints[J_Chest], fall * 22.0f);
    ragdoll.applyImpulse(joints[J_LKnee], fwd * 5.0f);
    ragdoll.applyImpulse(joints[J_RKnee], fwd * 5.0f);
    if (glm::length(impulse) > 1e-3f) ragdoll.applyImpulse(point, impulse);

    dead = true;
    deathTime = sc.time;
    causeOfDeath = cause;
    killer = killerId;
    cancelAction();

    // The gun in the hands falls where it was.
    if (weapon.hasWeapon()) {
        DroppedItem d;
        glm::mat4 m = weapon.worldTransform(1.0f);
        d.position = glm::vec3(m[3]);
        d.rotation = glm::quat_cast(glm::mat3(m));
        d.velocity = weapon.arm.pivotVelocity + randUnit(rng) * 0.4f;
        d.item = std::move(equip.slots[weapon.hand()]);
        sc.drops.push_back(std::move(d));
    }
    SoundEvent s;
    s.type = SoundType::BodyFall;
    s.position = joints[J_Pelvis];
    s.db = 72.0f;
    s.owner = id;
    sc.sound.emit(s);
}

void Character::draw(Renderer& r, float alpha, bool skipBody) const {
    if (!active || !posed) return;
    JointArray j;
    for (int i = 0; i < kJointCount; ++i) j[i] = glm::mix(prevJoints[i], joints[i], alpha);

    if (!skipBody) {
        OBB boxes[kBodyPartCount];
        buildBodyBoxes(j, boxes);
        glm::vec3 jacket = jacketColor(id), trousers = trouserColor(id);
        glm::vec3 vest = jacket * 0.75f;
        const glm::vec3 colors[kBodyPartCount] = {kSkin, vest, trousers, jacket, jacket, jacket, jacket,
                                                  trousers, trousers, trousers, trousers};
        for (int i = 0; i < kBodyPartCount; ++i) r.drawBox(boxes[i], colors[i]);
        OBB helmet = boxes[BP_Head];
        helmet.center += helmet.rotation[1] * 0.06f;
        helmet.half = glm::vec3(0.115f, 0.075f, 0.13f);
        r.drawBox(helmet, jacket * 0.7f);
        for (int h : {J_LHand, J_RHand}) r.drawBox(OBB{j[h], glm::vec3(0.045f), boxes[BP_ForearmL].rotation}, kSkin);
        for (int f : {J_LFoot, J_RFoot}) {
            OBB boot = boxes[f == J_LFoot ? BP_ShinL : BP_ShinR];
            boot.center = j[f] + boot.rotation[2] * -0.05f;
            boot.half = glm::vec3(0.06f, 0.05f, 0.13f);
            r.drawBox(boot, kBoots);
        }
        // Backpack
        const OBB& chestBox = boxes[BP_Chest];
        if (equip.slots[ES_Backpack]) {
            OBB pack = chestBox;
            pack.center += chestBox.rotation[2] * 0.20f;
            pack.half = glm::vec3(0.17f, 0.22f, 0.09f);
            r.drawBox(pack, equip.slots[ES_Backpack]->d().color);
        }
        // Slung weapons: back slots and the gun not in the hands.
        int slung = 0;
        for (int s : {ES_Hand0, ES_Hand1, ES_Back0, ES_Back1}) {
            if (!equip.slots[s]) continue;
            if (!dead && (s == ES_Hand0 || s == ES_Hand1) && s == weapon.hand() && !weapon.isSwitching()) continue;
            glm::mat3 rot = chestBox.rotation;
            // Diagonally across the back, muzzle up.
            float a = slung % 2 == 0 ? 0.6f : -0.6f;
            glm::mat3 sling = rot * glm::mat3_cast(glm::angleAxis(float(M_PI) * 0.5f, glm::vec3(1, 0, 0))) *
                              glm::mat3_cast(glm::angleAxis(a, glm::vec3(0, 0, 1)));
            glm::mat4 m(sling);
            glm::vec3 base = chestBox.center + rot[2] * (0.16f + 0.05f * slung) - rot[1] * 0.25f;
            m[3] = glm::vec4(base, 1.0f);
            drawWeaponItem(r, *equip.slots[s], m);
            ++slung;
        }
    }
    if (!dead) weapon.draw(r, alpha);
}
