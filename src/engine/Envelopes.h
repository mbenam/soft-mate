#pragma once
#include <cstdint>
#include <algorithm>

namespace m8::engine {

struct EnvContext {
    double samplesPerTick = 1000.0;
};

inline double ticksToSamples(uint8_t ticks, const EnvContext& ctx) {
    return double(ticks) * ctx.samplesPerTick;
}

class AhdEnv {
public:
    void trigger() { m_stage = Stage::Attack; m_pos = 0.0; m_value = 0.0f; }
    void release() {}
    bool running() const { return m_stage != Stage::Idle; }
    float value() const { return m_value; }

    float process(uint8_t atk, uint8_t hold, uint8_t dec, const EnvContext& ctx) {
        if (m_stage == Stage::Idle) return 0.0f;

        const double atkSmp  = ticksToSamples(atk, ctx);
        const double holdSmp = ticksToSamples(hold, ctx);
        const double decSmp  = ticksToSamples(dec, ctx);

        switch (m_stage) {
        case Stage::Attack:
            if (atkSmp <= 0.0) { m_value = 1.0f; m_stage = Stage::Hold; m_pos = 0.0; }
            else { m_value = float(m_pos / atkSmp); m_pos += 1.0; if (m_pos >= atkSmp) { m_value = 1.0f; m_stage = Stage::Hold; m_pos = 0.0; } }
            break;
        case Stage::Hold:
            m_value = 1.0f;
            m_pos += 1.0;
            if (m_pos >= holdSmp) { m_stage = Stage::Decay; m_pos = 0.0; }
            break;
        case Stage::Decay:
            if (decSmp <= 0.0) { m_value = 0.0f; m_stage = Stage::Idle; }
            else { m_value = 1.0f - float(m_pos / decSmp); m_pos += 1.0; if (m_pos >= decSmp) { m_value = 0.0f; m_stage = Stage::Idle; } }
            break;
        case Stage::Idle: break;
        }
        return m_value;
    }

private:
    enum class Stage { Idle, Attack, Hold, Decay };
    Stage m_stage = Stage::Idle;
    double m_pos = 0.0;
    float m_value = 0.0f;
};

class AdsrEnv {
public:
    void gate(bool on) { m_gate = on; if (!on && m_stage == Stage::Sustain) { m_stage = Stage::Release; m_pos = 0.0; } }
    bool running() const { return m_stage != Stage::Idle; }
    float value() const { return m_value; }

    void retrigger() { m_stage = Stage::Attack; m_pos = 0.0; m_value = 0.0f; m_gate = true; }

    float process(uint8_t atk, uint8_t dec, uint8_t sus, uint8_t rel, const EnvContext& ctx) {
        if (m_stage == Stage::Idle) return 0.0f;

        const double atkSmp  = ticksToSamples(atk, ctx);
        const double decSmp  = ticksToSamples(dec, ctx);
        const double relSmp  = ticksToSamples(rel, ctx);
        const float susLevel = sus / 255.0f;

        switch (m_stage) {
        case Stage::Attack:
            if (atkSmp <= 0.0) { m_value = 1.0f; m_stage = Stage::Decay; m_pos = 0.0; }
            else { m_value = float(m_pos / atkSmp); m_pos += 1.0; if (m_pos >= atkSmp) { m_value = 1.0f; m_stage = Stage::Decay; m_pos = 0.0; } }
            break;
        case Stage::Decay:
            if (decSmp <= 0.0) { m_value = susLevel; m_stage = Stage::Sustain; }
            else { float t = float(m_pos / decSmp); m_value = 1.0f + (susLevel - 1.0f) * t; m_pos += 1.0; if (m_pos >= decSmp) { m_value = susLevel; m_stage = Stage::Sustain; } }
            break;
        case Stage::Sustain:
            m_value = susLevel;
            if (!m_gate) { m_stage = Stage::Release; m_pos = 0.0; }
            break;
        case Stage::Release:
            if (relSmp <= 0.0) { m_value = 0.0f; m_stage = Stage::Idle; }
            else { float startVal = m_value; m_value = startVal * (1.0f - float(m_pos / relSmp)); m_pos += 1.0; if (m_pos >= relSmp) { m_value = 0.0f; m_stage = Stage::Idle; } }
            break;
        case Stage::Idle: break;
        }
        return m_value;
    }

private:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };
    Stage m_stage = Stage::Idle;
    double m_pos = 0.0;
    float m_value = 0.0f;
    bool m_gate = false;
};

class DrumEnv {
public:
    void trigger() { m_stage = Stage::Peak; m_pos = 0.0; m_value = 0.0f; }
    void release() {}
    bool running() const { return m_stage != Stage::Idle; }
    float value() const { return m_value; }

    float process(uint8_t peak, uint8_t body, uint8_t dec, const EnvContext& ctx) {
        if (m_stage == Stage::Idle) return 0.0f;

        const double peakSmp = ticksToSamples(peak, ctx);
        const double bodySmp = ticksToSamples(body, ctx);
        const double decSmp  = ticksToSamples(dec, ctx);

        switch (m_stage) {
        case Stage::Peak:
            if (peakSmp <= 0.0) { m_value = 1.0f; m_stage = Stage::Body; m_pos = 0.0; }
            else { m_value = float(m_pos / peakSmp); m_pos += 1.0; if (m_pos >= peakSmp) { m_value = 1.0f; m_stage = Stage::Body; m_pos = 0.0; } }
            break;
        case Stage::Body:
            m_value = 1.0f;
            m_pos += 1.0;
            if (m_pos >= bodySmp) { m_stage = Stage::Decay; m_pos = 0.0; }
            break;
        case Stage::Decay:
            if (decSmp <= 0.0) { m_value = 0.0f; m_stage = Stage::Idle; }
            else { m_value = 1.0f - float(m_pos / decSmp); m_pos += 1.0; if (m_pos >= decSmp) { m_value = 0.0f; m_stage = Stage::Idle; } }
            break;
        case Stage::Idle: break;
        }
        return m_value;
    }

private:
    enum class Stage { Idle, Peak, Body, Decay };
    Stage m_stage = Stage::Idle;
    double m_pos = 0.0;
    float m_value = 0.0f;
};

} // namespace m8::engine
