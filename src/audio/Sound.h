#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <deque>

class World;

enum class SoundType {
    Shot,      // variant: weapon index
    Crack,     // supersonic bullet passing (emitted per listener)
    Impact,    // variant: Surface
    Footstep,  // variant: Surface
    Reload,    // variant: 0 mag out, 1 mag in, 2 cycle/bolt, 3 shell in
    DryFire,
    Med,       // variant: MedKind
    Loot,      // rummaging through a body or container
    Beacon,    // send-back uplink beeping
    Flare,     // extraction flare
    BodyFall,
    Pain,      // variant 1: a wounded groan
    Rustle,    // variant 0: brushing through leaves, 1: kit rattling
    Breath,    // heavy breathing
    kCount
};

struct SoundEvent {
    SoundType type = SoundType::Shot;
    glm::vec3 position{0.0f};
    float db = 100.0f;       // sound pressure level at 1 m
    int owner = -1;          // who made it (-1 world)
    int variant = 0;
    bool suppressed = false;
    float gain = 1.0f;       // playback-only tweak
    double time = 0.0;       // emission time (sim seconds)
    uint32_t id = 0;
};

// What a listener at some position perceives of an event.
struct Hearing {
    float level = -100.0f; // dB at the listener
    float delay = 0.0f;    // propagation time (s)
    float distance = 0.0f;
    bool occluded = false;
};

// All sounds made in the world. The audio device and the bots' ears both read it,
// so both hear the same things with the same propagation.
class SoundBus {
public:
    void emit(SoundEvent e);
    void setTime(double t);
    double time() const { return m_time; }
    const std::deque<SoundEvent>& events() const { return m_events; }
    void clear() { m_events.clear(); }

    static constexpr float kSpeedOfSound = 343.0f;
    static constexpr float kAbsorption = 0.05f; // dB per metre (air + ground effect)
    static Hearing hear(const SoundEvent& e, const glm::vec3& listener, const World* world);

private:
    std::deque<SoundEvent> m_events;
    double m_time = 0.0;
    uint32_t m_nextId = 1;
};
