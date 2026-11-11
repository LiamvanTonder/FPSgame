#pragma once

#include "audio/Sound.h"
#include "audio/SoundSynth.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

class World;

// Plays the world's sound events for one listener (the camera): propagation delay,
// distance attenuation and air absorption, stereo direction and occlusion.
class Audio {
public:
    Audio();
    ~Audio();
    bool init();      // opens the output device (returns false if there is none; the game runs silently)
    void shutdown();
    // Schedules newly emitted events. 'self' is the listener's own owner id.
    void update(const SoundBus& bus, const World* world, const glm::vec3& pos, const glm::vec3& forward, const glm::vec3& right, int self);
    // Direct (non-spatial) sounds for the listener's own body.
    void playLocal(Clip clip, float gain, float pan = 0.0f);
    void setMasterVolume(float v) { m_master = v; }
    void reset(); // forget pending events (new world)
    void loadClips() { synthesizeClips(m_clips); } // headless (tests)
    void disableAmbience() { m_windStarted = true; }

    // Mixer core, public for headless tests.
    void mix(float* stereo, uint32_t frames);
    const std::vector<ClipData>& clips() const { return m_clips; }
    int activeVoices() const;

private:
    struct Voice {
        const ClipData* clip;
        int64_t start;      // sample clock at which it begins
        double pos = 0.0;   // loop playback position (source samples)
        float rate = 1.0f;  // playback speed (pitch variation)
        int delayL = 0, delayR = 0; // interaural time difference (output samples)
        float gainL, gainR;
        float lpAL, lpAR, lpL = 0.0f, lpR = 0.0f; // per-ear low-pass (head shadow on the far ear)
        bool loop = false;
        bool done = false;
    };
    void schedule(Clip clip, int64_t delaySamples, float gain, float pan, float lowpassHz, bool loop = false, float rate = 1.0f);
    uint32_t m_rngState = 12345;
    float rand01() {
        m_rngState = m_rngState * 1664525u + 1013904223u;
        return float(m_rngState >> 8) / 16777216.0f;
    }

    std::vector<ClipData> m_clips;
    std::vector<Voice> m_voices;
    mutable std::mutex m_mutex;
    int64_t m_clock = 0; // samples rendered
    uint32_t m_lastEvent = 0;
    float m_master = 0.8f;
    bool m_windStarted = false;
    struct Device;
    std::unique_ptr<Device> m_device;
};
