#pragma once
#include <cmath>

namespace m8 {
namespace engine {

// -----------------------------------------------------------------------------
// Zero-delay-feedback (ZDF) state-variable filter.
//
// This is the topology-preserving-transform SVF from Andrew Simper / Cytomic
// ("Solving the continuous SVF equations using the trapezoidal integration
// rule", 2013) — the standard reference for a ZDF SVF. It resolves the unit
// delay in the feedback path implicitly, so it stays accurate and stable right
// up to Nyquist, where a naive (Chamberlin) SVF detunes and can blow up. The M8
// exposes this as the "ZDF LP" / "ZDF HP" filter types; DaisySP's Svf is the
// non-ZDF form used for the ordinary LP/HP/BP/BS types.
//
// State is two integrator memories (ic1eq, ic2eq); no allocation, no branching
// in the hot path — safe for the audio thread.
// -----------------------------------------------------------------------------
class ZdfSvf {
public:
    void reset() { m_ic1eq = 0.0f; m_ic2eq = 0.0f; }

    // cutoff in Hz, res in [0,1] (0 = maximally damped, 1 = self-oscillating).
    void setParams(float cutoffHz, float res, float sampleRate) {
        // k = 1/Q. Map res 0..1 to a musical Q range: k from 2 (Q=0.5, well
        // damped) down toward 0 (Q -> inf, self-oscillation) as res -> 1.
        float g = std::tan(3.14159265358979f * cutoffHz / sampleRate);
        float k = 2.0f - 1.98f * res;
        m_g = g;
        m_k = k;
        m_a1 = 1.0f / (1.0f + g * (g + k));
        m_a2 = g * m_a1;
        m_a3 = g * m_a2;
    }

    // Process one sample; returns low-pass output and writes the high-pass
    // output through hpOut. band = v1, low = v2, high = v0 - k*v1 - v2.
    inline float process(float v0, float& hpOut) {
        float v3 = v0 - m_ic2eq;
        float v1 = m_a1 * m_ic1eq + m_a2 * v3;
        float v2 = m_ic2eq + m_a2 * m_ic1eq + m_a3 * v3;
        m_ic1eq = 2.0f * v1 - m_ic1eq;
        m_ic2eq = 2.0f * v2 - m_ic2eq;
        hpOut = v0 - m_k * v1 - v2;
        return v2; // low-pass
    }

private:
    float m_g = 0.0f, m_k = 2.0f, m_a1 = 1.0f, m_a2 = 0.0f, m_a3 = 0.0f;
    float m_ic1eq = 0.0f, m_ic2eq = 0.0f;
};

} // namespace engine
} // namespace m8
