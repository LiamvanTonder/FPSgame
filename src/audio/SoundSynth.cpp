#include "audio/SoundSynth.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace {

const float kPi = 3.14159265f;

struct Gen {
    std::mt19937 rng{12345};
    float noise() { return std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng); }
};

std::vector<float> buffer(float seconds) { return std::vector<float>(size_t(seconds * kSampleRate), 0.0f); }

// One-pole filters (cutoff in Hz)
struct LowPass {
    float a, y = 0.0f;
    explicit LowPass(float fc) : a(1.0f - std::exp(-2.0f * kPi * fc / kSampleRate)) {}
    float operator()(float x) { return y += a * (x - y); }
};
struct HighPass {
    LowPass lp;
    explicit HighPass(float fc) : lp(fc) {}
    float operator()(float x) { return x - lp(x); }
};
// Two-pole resonator (band-pass) for knocks and rings.
struct Resonator {
    float b1, b2, y1 = 0, y2 = 0, gain;
    Resonator(float f, float q) {
        float r = std::exp(-kPi * f / (q * kSampleRate));
        b1 = 2.0f * r * std::cos(2.0f * kPi * f / kSampleRate);
        b2 = -r * r;
        gain = 1.0f - r;
    }
    float operator()(float x) {
        float y = gain * x + b1 * y1 + b2 * y2;
        y2 = y1;
        y1 = y;
        return y;
    }
};

float env(float t, float tau) { return std::exp(-t / tau); }

void normalize(std::vector<float>& s, float peak = 0.9f) {
    float m = 1e-6f;
    for (float v : s) m = std::max(m, std::abs(v));
    for (float& v : s) v *= peak / m;
    // Short fade-out to avoid clicks.
    size_t fade = std::min<size_t>(s.size(), 240);
    for (size_t i = 0; i < fade; ++i) s[s.size() - 1 - i] *= float(i) / fade;
}

// Simple room/landscape echo: a few decaying, low-passed taps.
void addEcho(std::vector<float>& s, float decay, float taps, float spacing) {
    std::vector<float> dry = s;
    LowPass lp(1800.0f);
    for (int k = 1; k <= int(taps); ++k) {
        size_t d = size_t(spacing * k * kSampleRate * (0.8f + 0.4f * std::sin(float(k) * 1.7f)));
        float g = std::pow(decay, float(k));
        for (size_t i = 0; i + d < s.size(); ++i) s[i + d] += lp(dry[i]) * g;
    }
}

std::vector<float> gunshot(Gen& g, float boomHz, float boomTau, float bodyHz, float bodyTau, float tailTau, float crack,
                           float length) {
    std::vector<float> s = buffer(length);
    LowPass body(bodyHz), tail(900.0f), boomLp(400.0f);
    float phase = 0.0f;
    for (size_t i = 0; i < s.size(); ++i) {
        float t = float(i) / kSampleRate;
        float n = g.noise();
        float v = 0.0f;
        if (t < 0.002f) v += n * crack;                       // muzzle blast transient
        v += body(n) * 2.2f * env(t, bodyTau);                // blast body
        float f = boomHz * (1.0f + 0.6f * env(t, 0.03f));     // pressure wave thump, pitch falling
        phase += 2.0f * kPi * f / kSampleRate;
        v += boomLp(std::sin(phase) + 0.3f * n) * 1.4f * env(t, boomTau);
        if (t > 0.01f) v += tail(n) * 0.35f * env(t - 0.01f, tailTau); // reverberant tail
        s[i] = v;
    }
    addEcho(s, 0.45f, 3.0f, 0.11f);
    normalize(s, 0.95f);
    return s;
}

std::vector<float> click(Gen& g, float f1, float f2, float gap, float q, float length) {
    std::vector<float> s = buffer(length);
    Resonator r1(f1, q), r2(f2, q);
    size_t g2 = size_t(gap * kSampleRate);
    for (size_t i = 0; i < s.size(); ++i) {
        float t = float(i) / kSampleRate;
        float x1 = t < 0.0015f ? g.noise() : 0.0f;
        float x2 = (i >= g2 && i < g2 + 72) ? g.noise() : 0.0f;
        s[i] = r1(x1 * 4.0f) * 8.0f + r2(x2 * 4.0f) * 8.0f + (x1 + x2) * 0.3f;
    }
    normalize(s, 0.8f);
    return s;
}

std::vector<float> footstep(Gen& g, float lp, float tau, float resF, float resAmt, float grit) {
    std::vector<float> s = buffer(0.18f);
    LowPass f(lp);
    Resonator res(resF, 6.0f);
    for (size_t i = 0; i < s.size(); ++i) {
        float t = float(i) / kSampleRate;
        float n = g.noise();
        // Heel then toe
        float e = env(t, tau) + 0.6f * (t > 0.05f ? env(t - 0.05f, tau * 0.8f) : 0.0f);
        float grain = (g.noise() > 1.0f - grit) ? g.noise() * 2.0f : 0.0f;
        s[i] = f(n + grain) * e + res(n * e) * resAmt;
    }
    normalize(s, 0.7f);
    return s;
}

std::vector<float> impact(Gen& g, float lp, float tau, float resF, float resQ, float resAmt, float length) {
    std::vector<float> s = buffer(length);
    LowPass f(lp);
    Resonator res(resF, resQ);
    for (size_t i = 0; i < s.size(); ++i) {
        float t = float(i) / kSampleRate;
        float n = g.noise() * env(t, tau);
        s[i] = f(n) + res(t < 0.002f ? g.noise() * 3.0f : 0.0f) * resAmt;
    }
    normalize(s, 0.8f);
    return s;
}

} // namespace

void synthesizeClips(std::vector<ClipData>& out) {
    out.assign(kClipCount, {});
    Gen g;
    {
        // Brushing through leaves: many tiny bright crackles under a soft swish
        std::vector<float> s = buffer(0.7f);
        HighPass hp(1800.0f);
        LowPass lp(6000.0f);
        for (size_t i = 0; i < s.size(); ++i) {
            float t = float(i) / kSampleRate;
            float env = std::sin(std::min(t / 0.7f, 1.0f) * kPi);
            float crackle = g.noise() > 0.93f ? g.noise() * 3.0f : 0.0f;
            s[i] = lp(hp(g.noise() * 0.5f + crackle)) * env;
        }
        normalize(s, 0.6f);
        out[Clip_Leaves].samples = s;
    }
    out[Clip_StepLeaves].samples = footstep(g, 4500.0f, 0.04f, 600.0f, 0.1f, 0.35f);
    {
        // Kit rattle: a few metallic ticks and a strap creak
        std::vector<float> s = buffer(0.25f);
        for (int k = 0; k < 4; ++k) {
            size_t at = size_t((0.01f + 0.05f * k + 0.01f * g.noise()) * kSampleRate);
            Resonator r(2200.0f + 900.0f * g.noise(), 25.0f);
            for (size_t i = 0; i < 2000 && at + i < s.size(); ++i) s[at + i] += r(i < 25 ? g.noise() * 3.0f : 0.0f) * 4.0f;
        }
        LowPass lp(900.0f);
        for (size_t i = 0; i < s.size(); ++i) s[i] += lp(g.noise()) * 0.3f * std::sin(std::min(float(i) / s.size(), 1.0f) * kPi);
        normalize(s, 0.5f);
        out[Clip_Gear].samples = s;
    }
    {
        // A low groan: voiced, falling pitch, breathy
        std::vector<float> s = buffer(0.9f);
        Resonator f1(450.0f, 5.0f), f2(900.0f, 6.0f);
        float phase = 0.0f;
        for (size_t i = 0; i < s.size(); ++i) {
            float t = float(i) / kSampleRate;
            float f0 = 115.0f - 30.0f * t + 4.0f * std::sin(t * 30.0f);
            phase += f0 / kSampleRate;
            float buzz = (phase - std::floor(phase)) * 2.0f - 1.0f;
            float e = std::sin(std::min(t / 0.9f, 1.0f) * kPi);
            float x = (buzz * 0.7f + 0.5f * g.noise()) * e;
            s[i] = f1(x) * 3.0f + f2(x) * 1.5f;
        }
        normalize(s, 0.55f);
        out[Clip_Groan].samples = s;
    }
    out[Clip_ShotPistol].samples = gunshot(g, 110.0f, 0.06f, 3200.0f, 0.020f, 0.25f, 1.0f, 0.7f);
    out[Clip_ShotRifle].samples = gunshot(g, 70.0f, 0.10f, 2400.0f, 0.030f, 0.40f, 1.2f, 1.0f);
    out[Clip_ShotMagnum].samples = gunshot(g, 55.0f, 0.14f, 2000.0f, 0.045f, 0.55f, 1.3f, 1.3f);
    out[Clip_ShotShotgun].samples = gunshot(g, 50.0f, 0.18f, 1500.0f, 0.055f, 0.50f, 1.0f, 1.3f);
    {
        // Suppressed: a muffled thump and the action clacking.
        std::vector<float> s = buffer(0.35f);
        LowPass lp(700.0f);
        Resonator mech(2800.0f, 12.0f);
        for (size_t i = 0; i < s.size(); ++i) {
            float t = float(i) / kSampleRate;
            float n = g.noise();
            s[i] = lp(n) * 2.0f * env(t, 0.025f) + mech((t > 0.012f && t < 0.0135f) ? n * 5.0f : 0.0f) * 3.0f;
        }
        normalize(s, 0.9f);
        out[Clip_ShotSuppressed].samples = s;
    }
    {
        // Supersonic crack: an N-wave, a few hundred microseconds long, then a little air.
        std::vector<float> s = buffer(0.12f);
        size_t n = size_t(0.0004f * kSampleRate);
        for (size_t i = 0; i < n; ++i) s[i] = 1.0f - 2.0f * float(i) / n;
        HighPass hp(2000.0f);
        for (size_t i = n; i < s.size(); ++i) s[i] = hp(g.noise()) * 0.25f * env(float(i - n) / kSampleRate, 0.015f);
        normalize(s, 0.95f);
        out[Clip_Crack].samples = s;
    }
    {
        std::vector<float> s = buffer(1.6f);
        LowPass lp(500.0f);
        for (size_t i = 0; i < s.size(); ++i) {
            float t = float(i) / kSampleRate;
            s[i] = lp(g.noise()) * (1.0f - env(t, 0.04f)) * env(t, 0.45f);
        }
        addEcho(s, 0.6f, 4.0f, 0.25f);
        normalize(s, 0.8f);
        out[Clip_DistantTail].samples = s;
    }
    out[Clip_StepGrass].samples = footstep(g, 1400.0f, 0.03f, 300.0f, 0.0f, 0.08f);
    out[Clip_StepDirt].samples = footstep(g, 1800.0f, 0.025f, 250.0f, 0.2f, 0.15f);
    out[Clip_StepConcrete].samples = footstep(g, 5000.0f, 0.012f, 900.0f, 0.3f, 0.02f);
    out[Clip_StepWood].samples = footstep(g, 2500.0f, 0.02f, 180.0f, 1.2f, 0.0f);
    out[Clip_StepMetal].samples = footstep(g, 6000.0f, 0.015f, 1400.0f, 2.0f, 0.0f);
    out[Clip_HitDirt].samples = impact(g, 900.0f, 0.03f, 150.0f, 3.0f, 0.5f, 0.25f);
    out[Clip_HitConcrete].samples = impact(g, 6000.0f, 0.02f, 1800.0f, 4.0f, 0.8f, 0.25f);
    out[Clip_HitWood].samples = impact(g, 2500.0f, 0.02f, 400.0f, 10.0f, 1.5f, 0.25f);
    out[Clip_HitMetal].samples = impact(g, 8000.0f, 0.01f, 2600.0f, 60.0f, 3.0f, 0.6f);
    out[Clip_HitFlesh].samples = impact(g, 600.0f, 0.04f, 120.0f, 2.0f, 1.0f, 0.2f);
    out[Clip_HitFoliage].samples = impact(g, 3000.0f, 0.08f, 2000.0f, 1.0f, 0.0f, 0.3f);
    out[Clip_MagOut].samples = click(g, 1800.0f, 900.0f, 0.06f, 20.0f, 0.25f);
    out[Clip_MagIn].samples = click(g, 2200.0f, 3200.0f, 0.03f, 25.0f, 0.25f);
    out[Clip_Bolt].samples = click(g, 1500.0f, 2600.0f, 0.15f, 18.0f, 0.35f);
    out[Clip_ShellIn].samples = click(g, 1200.0f, 700.0f, 0.04f, 10.0f, 0.2f);
    out[Clip_DryFire].samples = click(g, 3500.0f, 3500.0f, 0.5f, 40.0f, 0.12f);
    {
        // Bandage: tearing fabric (noise with rapid amplitude ripple)
        std::vector<float> s = buffer(0.9f);
        LowPass lp(3500.0f);
        for (size_t i = 0; i < s.size(); ++i) {
            float t = float(i) / kSampleRate;
            float ripple = 0.5f + 0.5f * std::sin(t * 2.0f * kPi * 55.0f + std::sin(t * 13.0f) * 3.0f);
            s[i] = lp(g.noise()) * ripple * std::sin(std::min(t / 0.9f, 1.0f) * kPi);
        }
        normalize(s, 0.6f);
        out[Clip_Bandage].samples = s;
    }
    {
        std::vector<float> s = buffer(0.5f);
        for (int k = 0; k < 14; ++k) {
            size_t at = size_t((0.02f + 0.03f * k + 0.01f * g.noise()) * kSampleRate);
            Resonator r(3000.0f + 800.0f * g.noise(), 30.0f);
            for (size_t i = 0; i < 1200 && at + i < s.size(); ++i) s[at + i] += r(i < 30 ? g.noise() * 3.0f : 0.0f) * 5.0f;
        }
        normalize(s, 0.5f);
        out[Clip_Pills].samples = s;
    }
    {
        std::vector<float> s = buffer(0.8f);
        LowPass lp(2500.0f);
        HighPass hp(400.0f);
        for (size_t i = 0; i < s.size(); ++i) {
            float t = float(i) / kSampleRate;
            float am = 0.4f + 0.6f * std::abs(std::sin(t * 9.0f) * std::sin(t * 23.0f));
            s[i] = hp(lp(g.noise())) * am;
        }
        normalize(s, 0.5f);
        out[Clip_Rustle].samples = s;
    }
    {
        std::vector<float> s = buffer(0.5f);
        for (size_t i = 0; i < s.size(); ++i) {
            float t = float(i) / kSampleRate;
            float on = (t < 0.12f || (t > 0.2f && t < 0.32f)) ? 1.0f : 0.0f;
            s[i] = std::sin(2.0f * kPi * 1800.0f * t) * on;
        }
        normalize(s, 0.6f);
        out[Clip_Beacon].samples = s;
    }
    {
        std::vector<float> s = buffer(2.0f);
        HighPass hp(1500.0f);
        for (size_t i = 0; i < s.size(); ++i) {
            float t = float(i) / kSampleRate;
            s[i] = hp(g.noise()) * (1.0f - env(t, 0.05f)) * (t > 1.7f ? (2.0f - t) / 0.3f : 1.0f);
        }
        normalize(s, 0.5f);
        out[Clip_Flare].samples = s;
    }
    out[Clip_BodyFall].samples = impact(g, 300.0f, 0.08f, 80.0f, 2.0f, 1.5f, 0.5f);
    {
        // Grunt: voiced buzz through two formants
        std::vector<float> s = buffer(0.35f);
        Resonator f1(600.0f, 5.0f), f2(1100.0f, 6.0f);
        float phase = 0.0f;
        for (size_t i = 0; i < s.size(); ++i) {
            float t = float(i) / kSampleRate;
            float f0 = 130.0f - 40.0f * t;
            phase += f0 / kSampleRate;
            float buzz = (phase - std::floor(phase)) * 2.0f - 1.0f;
            float e = (1.0f - env(t, 0.02f)) * env(t, 0.12f);
            float x = (buzz + 0.2f * g.noise()) * e;
            s[i] = f1(x) * 3.0f + f2(x) * 2.0f;
        }
        normalize(s, 0.6f);
        out[Clip_Pain].samples = s;
    }
    {
        std::vector<float> s = buffer(0.45f);
        for (size_t i = 0; i < s.size(); ++i) {
            float t = float(i) / kSampleRate;
            float a = std::sin(2.0f * kPi * 48.0f * t) * env(t, 0.05f);
            float b = t > 0.16f ? std::sin(2.0f * kPi * 40.0f * (t - 0.16f)) * env(t - 0.16f, 0.06f) * 0.7f : 0.0f;
            s[i] = a + b;
        }
        normalize(s, 0.9f);
        out[Clip_Heartbeat].samples = s;
    }
    {
        std::vector<float> s = buffer(1.4f);
        LowPass lp(1200.0f);
        for (size_t i = 0; i < s.size(); ++i) {
            float t = float(i) / kSampleRate;
            float e = std::sin(std::min(t / 0.6f, 1.0f) * kPi) * 0.8f + (t > 0.7f ? std::sin(std::min((t - 0.7f) / 0.7f, 1.0f) * kPi) * 0.5f : 0.0f);
            s[i] = lp(g.noise()) * e;
        }
        normalize(s, 0.5f);
        out[Clip_Breath].samples = s;
    }
    {
        // Wind bed: 8 s loop of slowly swelling low noise (cross-faded ends).
        std::vector<float> s = buffer(8.0f);
        LowPass lp(350.0f), lp2(120.0f);
        for (size_t i = 0; i < s.size(); ++i) {
            float t = float(i) / kSampleRate;
            float gust = 0.6f + 0.4f * std::sin(t * 0.785f) * std::sin(t * 0.39f + 1.0f);
            s[i] = (lp(g.noise()) * 0.7f + lp2(g.noise()) * 1.5f) * gust;
        }
        size_t xf = kSampleRate / 2;
        for (size_t i = 0; i < xf; ++i) {
            float a = float(i) / xf;
            s[i] = s[i] * a + s[s.size() - xf + i] * (1.0f - a);
        }
        s.resize(s.size() - xf);
        normalize(s, 0.5f);
        out[Clip_Wind].samples = s;
    }
}
