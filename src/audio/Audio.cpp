#include "audio/Audio.h"

#include "game/WeaponDefs.h"
#include "items/ItemDefs.h"
#include "physics/World.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#include "miniaudio/miniaudio.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>

struct Audio::Device {
    ma_device device;
    bool ok = false;
};

Audio::Audio() = default;
Audio::~Audio() { shutdown(); }

namespace {
void dataCallback(ma_device* device, void* output, const void*, ma_uint32 frames) {
    auto* self = static_cast<Audio*>(device->pUserData);
    self->mix(static_cast<float*>(output), frames);
}

Clip shotClip(int weapon, bool suppressed) {
    if (suppressed) return Clip_ShotSuppressed;
    const WeaponDef& w = weaponDefs()[std::clamp(weapon, 0, int(weaponDefs().size()) - 1)];
    switch (w.cls) {
    case WeaponClass::Pistol: return w.caliber == Cal_357 ? Clip_ShotMagnum : Clip_ShotPistol;
    case WeaponClass::Smg: return Clip_ShotPistol;
    case WeaponClass::Shotgun: return Clip_ShotShotgun;
    case WeaponClass::Rifle: case WeaponClass::Lmg: return w.caliber == Cal_762x51 ? Clip_ShotMagnum : Clip_ShotRifle;
    default: return Clip_ShotMagnum;
    }
}

Clip surfaceStep(int s) {
    switch (Surface(s)) {
    case Surface::Concrete: return Clip_StepConcrete;
    case Surface::Wood: return Clip_StepWood;
    case Surface::Metal: case Surface::Steel: return Clip_StepMetal;
    case Surface::Dirt: return Clip_StepDirt;
    case Surface::Foliage: return Clip_StepLeaves;
    default: return Clip_StepGrass;
    }
}

Clip surfaceHit(int s) {
    switch (Surface(s)) {
    case Surface::Concrete: return Clip_HitConcrete;
    case Surface::Wood: case Surface::Paper: return Clip_HitWood;
    case Surface::Metal: case Surface::Steel: return Clip_HitMetal;
    case Surface::Flesh: return Clip_HitFlesh;
    case Surface::Foliage: return Clip_HitFoliage;
    default: return Clip_HitDirt;
    }
}

// Playback boost per kind of sound: hearing adapts, and quiet things must stay audible.
float typeGain(SoundType t) {
    switch (t) {
    case SoundType::Shot: return 1.0f;
    case SoundType::Crack: return 1.0f;
    case SoundType::Impact: return 5.0f;
    case SoundType::Footstep: return 36.0f;
    case SoundType::Reload: case SoundType::DryFire: return 45.0f;
    case SoundType::Med: case SoundType::Loot: return 45.0f;
    case SoundType::Rustle: return 40.0f;
    case SoundType::Breath: return 60.0f;
    case SoundType::Beacon: case SoundType::Flare: return 6.0f;
    case SoundType::BodyFall: case SoundType::Pain: return 12.0f;
    default: return 1.0f;
    }
}
} // namespace

bool Audio::init() {
    synthesizeClips(m_clips);
    m_device = std::make_unique<Device>();
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = kSampleRate;
    cfg.dataCallback = dataCallback;
    cfg.pUserData = this;
    if (ma_device_init(nullptr, &cfg, &m_device->device) != MA_SUCCESS) return false;
    if (ma_device_start(&m_device->device) != MA_SUCCESS) {
        ma_device_uninit(&m_device->device);
        return false;
    }
    m_device->ok = true;
    return true;
}

void Audio::shutdown() {
    if (m_device && m_device->ok) {
        ma_device_uninit(&m_device->device);
        m_device->ok = false;
    }
}

void Audio::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(), [](const Voice& v) { return !v.loop; }), m_voices.end());
}

int Audio::activeVoices() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return int(m_voices.size());
}

void Audio::schedule(Clip clip, int64_t delay, float gain, float pan, float lowpassHz, bool loop, float rate) {
    if (m_clips.empty() || gain < 1e-4f) return;
    Voice v;
    v.clip = &m_clips[clip];
    pan = std::clamp(pan, -1.0f, 1.0f);
    v.gainL = gain * std::sqrt(0.5f * (1.0f - pan));
    v.gainR = gain * std::sqrt(0.5f * (1.0f + pan));
    v.rate = loop ? 1.0f : rate;
    // The far ear hears it up to ~0.65 ms later and duller (the head is in the way).
    int itd = int(std::abs(pan) * 0.00065f * kSampleRate);
    (pan > 0.0f ? v.delayL : v.delayR) = itd;
    auto coef = [](float hz) { return 1.0f - std::exp(-2.0f * 3.14159265f * std::min(hz, 20000.0f) / kSampleRate); };
    float nearHz = std::min(lowpassHz, 20000.0f), farHz = std::min(nearHz, 20000.0f / (1.0f + 4.0f * std::abs(pan)));
    v.lpAL = coef(pan > 0.0f ? farHz : nearHz);
    v.lpAR = coef(pan > 0.0f ? nearHz : farHz);
    v.loop = loop;
    std::lock_guard<std::mutex> lock(m_mutex);
    v.start = m_clock + delay;
    if (m_voices.size() > 96) {
        // Drop the quietest one-shot.
        auto q = std::min_element(m_voices.begin(), m_voices.end(), [](const Voice& a, const Voice& b) {
            return (a.loop ? 1e9f : a.gainL + a.gainR) < (b.loop ? 1e9f : b.gainL + b.gainR);
        });
        if (q != m_voices.end() && !q->loop) m_voices.erase(q);
    }
    m_voices.push_back(v);
}

void Audio::playLocal(Clip clip, float gain, float pan) { schedule(clip, 0, gain, pan, 20000.0f); }

void Audio::update(const SoundBus& bus, const World* world, const glm::vec3& pos, const glm::vec3& forward, const glm::vec3& right,
                   int self) {
    if (!m_windStarted && !m_clips.empty()) {
        schedule(Clip_Wind, 0, 0.05f, 0.0f, 20000.0f, true);
        m_windStarted = true;
    }
    double now = bus.time();
    for (const SoundEvent& e : bus.events()) {
        if (e.id <= m_lastEvent) continue;
        m_lastEvent = e.id;
        bool own = e.owner == self && e.type != SoundType::Crack;
        Hearing h = SoundBus::hear(e, pos, own ? nullptr : world);
        if (h.level < 15.0f) continue;
        // Perceived loudness is compressed relative to physical level.
        float amp = std::pow(10.0f, (h.level - 160.0f) / 40.0f) * typeGain(e.type) * e.gain;
        if (own && e.type == SoundType::Shot) amp *= 0.8f;
        if (own && (e.type == SoundType::Footstep || e.type == SoundType::Rustle || e.type == SoundType::Breath)) amp *= 0.4f; // your own body, quieter
        glm::vec3 to = e.position - pos;
        float dist = glm::length(to);
        float pan = dist > 0.5f ? glm::dot(to / dist, right) * 0.85f : 0.0f;
        bool behind = dist > 0.5f && glm::dot(to / dist, forward) < -0.3f;
        float lp = 18000.0f / (1.0f + dist / 80.0f);
        if (h.occluded) lp = std::min(lp, 1500.0f);
        if (behind) lp *= 0.7f;
        int64_t delay = int64_t((h.delay - float(now - e.time)) * kSampleRate);
        delay = std::max<int64_t>(delay, 0);

        Clip clip;
        switch (e.type) {
        case SoundType::Shot: clip = shotClip(e.variant, e.suppressed); break;
        case SoundType::Crack: clip = Clip_Crack; break;
        case SoundType::Impact: clip = surfaceHit(e.variant); break;
        case SoundType::Footstep: clip = surfaceStep(e.variant); break;
        case SoundType::Rustle: clip = e.variant == 1 ? Clip_Gear : Clip_Leaves; break;
        case SoundType::Breath: clip = Clip_Breath; break;
        case SoundType::Reload: clip = e.variant == 0 ? Clip_MagOut : e.variant == 1 ? Clip_MagIn : e.variant == 2 ? Clip_Bolt : Clip_ShellIn; break;
        case SoundType::DryFire: clip = Clip_DryFire; break;
        case SoundType::Med: clip = MedKind(e.variant) == MedKind::Painkillers ? Clip_Pills : Clip_Bandage; break;
        case SoundType::Loot: clip = Clip_Rustle; break;
        case SoundType::Beacon: clip = Clip_Beacon; break;
        case SoundType::Flare: clip = Clip_Flare; break;
        case SoundType::BodyFall: clip = Clip_BodyFall; break;
        case SoundType::Pain: clip = e.variant == 1 ? Clip_Groan : Clip_Pain; break;
        default: continue;
        }
        // Natural variation: no two footsteps or impacts sound exactly alike.
        bool vary = e.type == SoundType::Footstep || e.type == SoundType::Impact || e.type == SoundType::Rustle || e.type == SoundType::Reload;
        float rate = vary ? 0.92f + 0.16f * rand01() : 1.0f;
        schedule(clip, delay, amp, pan, lp, false, rate);
        // Distant gunfire rolls around the landscape.
        if (e.type == SoundType::Shot && dist > 120.0f && !e.suppressed) schedule(Clip_DistantTail, delay + kSampleRate / 20, amp * 0.9f, pan * 0.5f, lp);
    }
}

void Audio::mix(float* out, uint32_t frames) {
    std::fill(out, out + frames * 2, 0.0f);
    std::lock_guard<std::mutex> lock(m_mutex);
    for (Voice& v : m_voices) {
        const std::vector<float>& s = v.clip->samples;
        if (s.empty()) {
            v.done = true;
            continue;
        }
        const double n = double(s.size());
        auto sampleAt = [&](double p) -> float {
            if (p < 0.0 || p >= n - 1.0) return 0.0f;
            size_t i0 = size_t(p);
            float f = float(p - double(i0));
            return s[i0] + (s[i0 + 1] - s[i0]) * f;
        };
        for (uint32_t i = 0; i < frames; ++i) {
            int64_t t = m_clock + i;
            if (t < v.start) continue;
            float xl, xr;
            if (v.loop) {
                xl = xr = s[size_t(v.pos)];
                v.pos += 1.0;
                if (v.pos >= n) v.pos = 0.0;
            } else {
                double local = double(t - v.start);
                double pl = (local - v.delayL) * v.rate, pr = (local - v.delayR) * v.rate;
                if (pl >= n && pr >= n) {
                    v.done = true;
                    break;
                }
                xl = sampleAt(pl);
                xr = sampleAt(pr);
            }
            v.lpL += v.lpAL * (xl * v.gainL - v.lpL);
            v.lpR += v.lpAR * (xr * v.gainR - v.lpR);
            out[i * 2] += v.lpL;
            out[i * 2 + 1] += v.lpR;
        }
    }
    m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(), [](const Voice& v) { return !v.loop && v.done; }), m_voices.end());
    m_clock += frames;
    for (uint32_t i = 0; i < frames * 2; ++i) out[i] = std::tanh(out[i] * m_master); // soft limiter
}
