#pragma once

#include "game/Character.h"

#include <glm/glm.hpp>
#include <random>
#include <string>
#include <vector>

class Simulation;
struct Interaction;

// What a bot believes about another person.
struct Contact {
    int id = -1;
    glm::vec3 position{0.0f};  // best estimate (chest)
    glm::vec3 velocity{0.0f};
    double lastSeen = -1e9;    // eyes on
    double lastHeard = -1e9;   // heard / shot at by
    float awareness = 0.0f;    // 0..1 detection (>= 1 means spotted)
    float uncertainty = 0.0f;  // metres
    bool visible = false;
    bool dead = false;
};

struct Noise {
    glm::vec3 position{0.0f};
    double time = 0.0;
    float importance = 0.0f; // gunfire >> footsteps
    int owner = -1;
};

enum class Goal { Roam, Loot, Hunt, Fight, Heal, Extract, Flee, SendBack };
const char* goalName(Goal g);

struct Bot {
    int id = -1;
    // Personality
    float aggression = 0.5f, greed = 0.5f, caution = 0.5f, skill = 0.5f;

    std::vector<Contact> contacts;
    std::vector<Noise> noises;
    Goal goal = Goal::Roam;
    double goalSince = 0.0;

    // Navigation
    std::vector<glm::vec2> path;
    size_t pathIndex = 0;
    glm::vec2 destination{1e9f};
    double pathTime = -1e9;
    glm::vec2 stuckPos{0.0f};
    double stuckSince = 0.0;
    int stuckCount = 0;
    bool wantPath = false;

    // Looking and aiming
    float yaw = 0.0f, pitch = 0.0f;
    float wantYaw = 0.0f, wantPitch = 0.0f;
    float yawVel = 0.0f, pitchVel = 0.0f;
    float turnRate = 3.0f, lookStiffness = 8.0f;
    glm::vec2 moveSmooth{0.0f};
    float strafeSide = 1.0f;
    double strafeUntil = 0.0, crouchUntil = 0.0, adsUntil = 0.0, pauseUntil = 0.0;
    bool crouchHeld = false;
    double unstickUntil = 0.0;
    glm::vec2 unstickDir{0.0f};
    bool allowFinal = false; // walk the last metres to an object standing in a blocked cell
    glm::vec2 aimError{0.0f};
    float reaction = 0.0f;
    int target = -1;
    double nextShot = 0.0;
    int burst = 0;
    double scanTime = 0.0;
    float scanYaw = 0.0f;

    // Combat positioning
    bool hasCover = false;
    glm::vec2 cover{0.0f};
    double coverTime = 0.0;  // when the current cover phase started
    bool peeking = false;
    float peekLean = 0.0f;
    bool peekStand = false;
    double phaseUntil = 0.0;
    double lastDamaged = -1e9;
    glm::vec3 damagedFrom{0.0f};

    // Looting / objectives
    int lootType = 0; // 0 none, 1 container, 2 body, 3 world item
    int lootIndex = -1;
    std::vector<int> doneContainers, doneBodies;
    std::vector<int> doneHotspots;
    int extract = -1;
    bool sentBack = false;
    glm::vec2 roamTarget{1e9f};
    glm::vec2 fleeTo{1e9f};
    glm::vec2 progressDest{1e9f};
    float progressBest = 1e9f;
    double progressTime = 0.0;
    std::vector<glm::vec2> ignored; // places it could not reach
    double spawnTime = 0.0;

    // Scheduling
    double nextPerceive = 0.0, nextThink = 0.0, lastPerceive = 0.0;

    CharacterInput input;
    std::mt19937 rng;
    std::string debug; // last movement decision (diagnostics)
};

// The AI players: they want the same things the player wants (loot, kills, a way out)
// and only know what their own eyes and ears tell them.
class BotManager {
public:
    void clear() { bots.clear(); }
    void add(int charId, unsigned seed, double now);
    void remove(int charId);
    // Removal requested while bots are updating (applied after).
    void removeLater(int charId) { m_pendingRemove.push_back(charId); }
    Bot* find(int charId);
    void update(float dt, Simulation& sim);
    const CharacterInput* input(int charId) const;

    // Stimuli delivered by the simulation
    void onDamaged(int victim, int shooter, const glm::vec3& shotFrom, Simulation& sim);
    void onNearMiss(int victim, int shooter, const glm::vec3& shotFrom, Simulation& sim);

    std::string describe(int charId) const;
    std::vector<Bot> bots;
    int pathBudget = 0;

private:
    std::vector<int> m_pendingRemove;
    bool m_updating = false;
    double m_now = 0.0;
    void perceive(Bot& b, Simulation& sim);
    void think(Bot& b, Simulation& sim);
    void act(Bot& b, float dt, Simulation& sim);

    Contact& contact(Bot& b, int id);
    Contact* bestThreat(Bot& b, Simulation& sim);
    void moveTo(Bot& b, const glm::vec2& dest, Simulation& sim, bool sprintOk, float stopDist = 0.8f);
    void steer(Bot& b, Simulation& sim, const glm::vec2& dir, bool sprint);
    void fight(Bot& b, float dt, Simulation& sim, Contact& c);
    void loot(Bot& b, float dt, Simulation& sim);
    void heal(Bot& b, Simulation& sim);
    void extractGoal(Bot& b, float dt, Simulation& sim);
    void takeLoot(Bot& b, Simulation& sim);
    bool chooseLootTarget(Bot& b, Simulation& sim);
    bool wantsExtract(const Bot& b, const Simulation& sim) const;
    bool hasMeds(const Character& c) const;
};
