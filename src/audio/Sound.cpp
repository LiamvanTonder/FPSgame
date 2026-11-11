#include "audio/Sound.h"

#include "physics/World.h"

#include <algorithm>
#include <cmath>

void SoundBus::emit(SoundEvent e) {
    e.time = m_time;
    e.id = m_nextId++;
    m_events.push_back(e);
}

void SoundBus::setTime(double t) {
    m_time = t;
    // Sound has crossed the whole map after ~3.5 s.
    while (!m_events.empty() && m_events.front().time < t - 4.0) m_events.pop_front();
}

Hearing SoundBus::hear(const SoundEvent& e, const glm::vec3& listener, const World* world) {
    Hearing h;
    h.distance = glm::length(e.position - listener);
    h.delay = h.distance / kSpeedOfSound;
    float d = std::max(h.distance, 1.0f);
    h.level = e.db - 20.0f * std::log10(d) - kAbsorption * d;
    if (world && h.distance > 2.0f) {
        // Blocked line: the sound diffracts around and through obstacles, muffled.
        SegmentQuery q;
        q.staticOnly = true;
        q.skipPenetrable = true;
        WorldHit hit;
        glm::vec3 a = e.position + glm::vec3(0.0f, 0.3f, 0.0f);
        if (world->segment(a, listener, hit, q) && hit.t < 0.98f) {
            h.occluded = true;
            h.level -= 9.0f;
        }
    }
    return h;
}
