#pragma once
#include <cmath>
#include <algorithm>

// -----------------------------------------------------------------------------
// ModFX: the M8's first send effect is not a chorus, it is a slot with three
// algorithms -- 00 CHORUS, 01 PHASER, 02 FLANGER (hw_findings.md §UI-7). All
// three share the same four controls, which is why the labels do not change as
// you cycle the type: MOD DEPTH, MOD FRQ, STEREO WIDTH and REVERB SEND. Only
// the processor underneath differs.
//
// Chorus stays with DaisySP's implementation. These two are ours.
//
// Neither is hardware-verified. They are the standard textbook forms -- an
// allpass chain for the phaser, a short modulated delay with feedback for the
// flanger -- in the same "reference approximation" class as the FM/Wav engines
// and the LIM/DJF/OTT curves. A capture could move the constants.
//
// RT-safe by construction: fixed-size members, no allocation, no branching on
// anything the audio thread does not already own.
// -----------------------------------------------------------------------------

namespace m8 {
namespace engine {

// First-order allpass. Passes every frequency at unity gain but rotates phase,
// which is the whole trick behind a phaser: sum the rotated copy with the dry
// signal and the frequencies that came back inverted cancel into notches.
class AllpassStage {
public:
    void reset() { m_z = 0.0f; }
    inline void setCoef(float a) { m_a = a; }
    inline float process(float x) {
        const float y = m_a * (x + m_z1) - m_z;
        m_z  = y;
        m_z1 = x;
        return y;
    }
private:
    float m_a = 0.0f, m_z = 0.0f, m_z1 = 0.0f;
};

// PHASER -- six allpass stages swept together. Six is the common voicing (three
// notch pairs); fewer sounds thin, more turns into a comb.
class Phaser {
public:
    void reset() {
        for (auto& s : m_l) s.reset();
        for (auto& s : m_r) s.reset();
        m_fbL = m_fbR = 0.0f;
    }

    // lfoL/lfoR are 0..1 sweep positions; depth is 0..1.
    void process(float inL, float inR, float lfoL, float lfoR, float depth,
                 float sampleRate, float& outL, float& outR) {
        outL = run(m_l, m_fbL, inL, lfoL, depth, sampleRate);
        outR = run(m_r, m_fbR, inR, lfoR, depth, sampleRate);
    }

private:
    static constexpr int   kStages = 6;
    static constexpr float kFmin   = 200.0f;    // bottom of the sweep
    static constexpr float kFmax   = 4000.0f;   // top
    static constexpr float kFeedback = 0.6f;    // resonance; no M8 control for it

    static float run(AllpassStage (&chain)[kStages], float& fb,
                     float x, float lfo, float depth, float sampleRate) {
        // Sweep exponentially -- the ear hears frequency logarithmically, and a
        // linear sweep spends most of its time at the top sounding static.
        const float t  = std::clamp(lfo, 0.0f, 1.0f) * std::clamp(depth, 0.0f, 1.0f);
        const float f  = kFmin * std::pow(kFmax / kFmin, t);
        const float tn = std::tan(3.14159265358979f * std::min(f, sampleRate * 0.45f) / sampleRate);
        const float a  = (tn - 1.0f) / (tn + 1.0f);

        float v = x + fb * kFeedback;
        for (auto& s : chain) { s.setCoef(a); v = s.process(v); }
        fb = v;
        // Equal parts dry and phase-rotated: the deepest notches happen when
        // the two are the same size.
        return 0.5f * (x + v);
    }

    AllpassStage m_l[kStages], m_r[kStages];
    float m_fbL = 0.0f, m_fbR = 0.0f;
};

// FLANGER -- a very short modulated delay summed with the dry signal, with
// feedback. The moving delay makes a comb whose teeth slide, which is the jet
// whoosh. The only thing separating it from the chorus is the delay range:
// under ~10 ms the comb is audible as timbre, above it as pitch wobble.
class Flanger {
public:
    void reset() {
        for (int i = 0; i < kSize; ++i) { m_bufL[i] = 0.0f; m_bufR[i] = 0.0f; }
        m_write = 0;
        m_fbL = m_fbR = 0.0f;
    }

    void process(float inL, float inR, float lfoL, float lfoR, float depth,
                 float sampleRate, float& outL, float& outR) {
        const float d = std::clamp(depth, 0.0f, 1.0f);
        const float lo = kMinMs * 0.001f * sampleRate;
        const float hi = kMaxMs * 0.001f * sampleRate;

        m_bufL[m_write] = inL + m_fbL * kFeedback;
        m_bufR[m_write] = inR + m_fbR * kFeedback;

        const float dl = lo + (hi - lo) * std::clamp(lfoL, 0.0f, 1.0f) * d;
        const float dr = lo + (hi - lo) * std::clamp(lfoR, 0.0f, 1.0f) * d;
        const float wl = read(m_bufL, dl);
        const float wr = read(m_bufR, dr);
        m_fbL = wl;
        m_fbR = wr;

        if (++m_write >= kSize) m_write = 0;
        outL = 0.5f * (inL + wl);
        outR = 0.5f * (inR + wr);
    }

private:
    static constexpr int   kSize     = 2048;   // ~42 ms at 48 kHz, ample
    static constexpr float kMinMs    = 0.5f;
    static constexpr float kMaxMs    = 9.0f;
    static constexpr float kFeedback = 0.7f;   // no M8 control for it either

    float read(const float* buf, float delay) const {
        float pos = static_cast<float>(m_write) - delay;
        while (pos < 0.0f) pos += static_cast<float>(kSize);
        const int   i0 = static_cast<int>(pos);
        const float fr = pos - static_cast<float>(i0);
        const int   i1 = (i0 + 1) % kSize;
        return buf[i0] + (buf[i1] - buf[i0]) * fr;
    }

    float m_bufL[kSize] = {}, m_bufR[kSize] = {};
    int   m_write = 0;
    float m_fbL = 0.0f, m_fbR = 0.0f;
};

} // namespace engine
} // namespace m8
