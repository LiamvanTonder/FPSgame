#pragma once

#include "ai/Bots.h"
#include "ai/NavGrid.h"
#include "audio/Sound.h"
#include "game/Ballistics.h"
#include "game/Character.h"
#include "game/Effects.h"
#include "game/Level.h"
#include "game/Targets.h"
#include "physics/World.h"
#include "world/Loot.h"
#include "world/MapData.h"
#include "world/Terrain.h"

#include <memory>
#include <random>
#include <string>
#include <vector>

// Something a character can use with the interact key.
struct Interaction {
    enum Type { None, Container, Body, Item, Extract, SendBack, Stash, Deploy, Store } type = None;
    int index = -1; // container / character / world item / extract index
    bool operator==(const Interaction& o) const { return type == o.type && index == o.index; }
    bool operator!=(const Interaction& o) const { return !(*this == o); }
};

struct RaidStats {
    int kills = 0;
    float damageTaken = 0.0f;
    int itemsLooted = 0;
    int sentHome = 0;
    double duration = 0.0;
    std::string causeOfDeath, killedBy;
    int botsKilled = 0, botExtracts = 0, botSendBacks = 0, botLoots = 0, botRespawns = 0, botWeaponsTaken = 0;
};

struct FeedLine {
    std::string text;
    float timer;
};

// The world and everyone in it. Rendering-free, so it also runs headless (tests).
class Simulation {
public:
    enum class Mode { Base, Raid };

    void setupBase(Equipment& loadout);
    void setupRaid(unsigned seed, int botCount, Equipment& loadout, bool withPlayer = true);
    void tick(float dt, const CharacterInput& playerInput, bool interactHeld);

    Character& player() { return *chars[0]; }
    const Character& player() const { return *chars[0]; }
    bool hasPlayer() const { return m_withPlayer; }

    // Interaction (shared by player and bots)
    Interaction focusFor(const Character& c) const; // what the character is looking at / standing in
    std::string describe(const Interaction& i, const Character& c) const;
    // Holding interact on the target this tick; returns true when it completes (search done, extracted, ...).
    bool interact(Character& c, const Interaction& i, bool held, float dt);
    float interactProgress(int charId) const;
    // Grid / equipment of a loot source (nullptr if not lootable).
    Grid* containerGrid(int index);
    Equipment* bodyEquipment(int charId);
    // Picks up a world item into the character's gear; false if there is no room.
    bool pickUp(Character& c, int worldItem);
    void dropFromCharacter(Character& c, ItemPtr item);

    int spawnBot(int tier);
    SimContext context();
    void message(const std::string& s);
    std::vector<FeedLine>& feed() { return m_feed; }

    Mode mode = Mode::Base;
    World world;
    Terrain terrain;
    MapData map;
    std::vector<glm::vec3> terrainColors;
    Level level;
    Targets targets;
    std::vector<std::unique_ptr<Character>> chars; // index == owner id; [0] is the player
    Ballistics ballistics;
    Effects fx;
    SoundBus sound;
    Loot loot;
    NavGrid nav;
    BotManager bots;
    std::vector<Impact> impacts;
    std::vector<WeaponEvent> events;
    std::vector<DroppedItem> drops;
    double time = 0.0;
    std::mt19937 rng{2024};
    RaidStats stats;
    int botTarget = 8;

    // Player raid outcome
    bool playerExtracted = false;
    double playerDeathTime = -1.0;
    std::vector<ItemPtr> sentHome; // delivered to the stash
    // UI requests from the last tick
    Interaction openLoot;          // player finished searching: show the loot screen
    bool openStash = false, requestDeploy = false, openStore = false;

    // Incoming-fire feedback for the player
    float suppression = 0.0f;
    float hitFlash = 0.0f;
    glm::vec3 hitFrom{0.0f};
    glm::vec2 flinch{0.0f};
    std::string lastHitReport;
    float lastHitTimer = 0.0f;

private:
    void reset();
    void addCharacter(bool firstPerson);
    void handleImpacts();
    void handleEvents();
    void detectNearMisses(float dt);
    void updateRespawns(float dt);
    void cleanupBodies();

    struct InteractState {
        Interaction target;
        float progress = 0.0f;
        float beaconTimer = 0.0f;
    };
    std::vector<InteractState> m_interact;
    std::vector<std::pair<int, unsigned>> m_nearMiss; // (character, bullet id)
    std::vector<FeedLine> m_feed;
    std::vector<double> m_respawnAt;
    bool m_withPlayer = true;
};
