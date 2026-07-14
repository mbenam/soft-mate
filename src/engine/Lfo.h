#pragma once
#include "Envelopes.h"
#include <cstdint>
#include <cmath>

namespace m8::engine {

enum class LfoShape : uint8_t {
    TRI = 0, SIN, RAMP_DN, RAMP_UP, EXP_DN, EXP_UP,
    SQR33, SQR50, SQR66, SQR75, SQR87, SQR95,
    RANDOM, DRUNK, DRUNK_TRI, DRUNK_SIN, DRUNK_OCT,
    TRI_ENV, SIN_ENV, RAMP_DN_ENV, RAMP_UP_ENV, EXP_DN_ENV, EXP_UP_ENV
};

inline bool isEnvShape(uint8_t shape) {
    return shape >= 0x11 && shape <= 0x16;
}

class Lfo {
public:
    void trigger() { m_phase = 0.0; m_done = false; m_lastOut = 0.0f; }
    bool done() const { return m_done; }
    float lastOutput() const { return m_lastOut; }

    float process(uint8_t shape, uint8_t freq, uint8_t trig, const EnvContext& ctx, bool noteOn) {
        if (trig == 1 && noteOn) { m_phase = 0.0; m_done = false; }

        const double samplesPerStep = ctx.samplesPerTick * 6.0;
        const double periodSamples = double(freq) * samplesPerStep;
        const double inc = (periodSamples > 0.0) ? 1.0 / periodSamples : 0.0;

        float out = 0.0f;
        if (!m_done) {
            m_phase += inc;
            if (m_phase >= 1.0) {
                if (trig == 3 || isEnvShape(shape)) { m_phase = 1.0; m_done = true; }
                else m_phase -= 1.0;
            }
            out = shapeValue(shape, m_phase);
        } else {
            out = startValue(shape);
        }

        if (trig == 2 && m_done) out = m_lastOut;
        m_lastOut = out;
        return out;
    }

private:
    float m_lastOut = 0.0f;
    double m_phase = 0.0;
    bool m_done = false;

    static float shapeValue(uint8_t shape, double phase) {
        float p = float(phase);
        switch (shape) {
        case 0x00: return (p < 0.25f) ? (p * 4.0f) : (p < 0.75f) ? (2.0f - p * 4.0f) : (p * 4.0f - 4.0f);
        case 0x01: return std::sin(float(6.2831853) * p);
        case 0x02: return 1.0f - p * 2.0f;
        case 0x03: return p * 2.0f - 1.0f;
        case 0x04: return std::pow(2.0f, -(p * 10.0f)) * 2.0f - 1.0f;
        case 0x05: return 1.0f - std::pow(2.0f, -(p * 10.0f)) * 2.0f;
        case 0x06: return (p < 0.33f) ? -1.0f : 1.0f;
        case 0x07: return (p < 0.50f) ? -1.0f : 1.0f;
        case 0x08: return (p < 0.66f) ? -1.0f : 1.0f;
        case 0x09: return (p < 0.75f) ? -1.0f : 1.0f;
        case 0x0A: return (p < 0.87f) ? -1.0f : 1.0f;
        case 0x0B: return (p < 0.95f) ? -1.0f : 1.0f;
        case 0x0C: return (float(std::rand()) / float(RAND_MAX)) * 2.0f - 1.0f;
        case 0x0D: case 0x0E: case 0x0F: case 0x10:
            return shapeValue(uint8_t(shape - 0x0D), phase);
        default: return shapeValue(0x00, phase);
        }
    }

    static float startValue(uint8_t shape) {
        switch (shape) {
        case 0x00: case 0x01: return 0.0f;
        case 0x02: return 1.0f;
        case 0x03: return -1.0f;
        case 0x04: return 1.0f;
        case 0x05: return -1.0f;
        default: return (shape <= 0x0B) ? -1.0f : 0.0f;
        }
    }
};

} // namespace m8::engine
