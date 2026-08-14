#pragma once
#include <cmath>
#include <algorithm>

// -----------------------------------------------------------------------------
// SHIMMER -- the reverb's tail fed back through an octave-up pitch shifter, so
// it rises and blooms instead of only decaying. Shares the DECAY row on the
// device (`DECAY:SHIMMER C0:00`); its byte is effects +22 (hw_findings §UI-9).
//
// The shifter is the classic two-head crossfade: write at 1x, read at 2x with
// two heads half a window apart, cross-faded by Hann windows that sum to one.
// It is not a phase-vocoder and it does not pretend to be -- at 2x the artefacts
// are a slight warble on transients, which is exactly the texture a shimmer
// reverb is made of, and it costs one buffer and no FFT.
//
// NOT hardware-verified. Octave-up is the conventional choice and the M8's own
// interval is unmeasured; the amount curve and the feedback ceiling are ours.
//
// RT-safe: fixed buffer, no allocation.
// -----------------------------------------------------------------------------

namespace m8 {
namespace engine {

class OctaveUpShifter {
public:
    void reset() {
        for (int i = 0; i < kSize; ++i) m_buf[i] = 0.0f;
        m_write = 0;
        m_phase = 0.0f;
    }

    inline float process(float x) {
        m_buf[m_write] = x;

        // Reading twice as fast means the delay shrinks by one sample per
        // sample; the phase ramp is that, normalised to the window.
        m_phase += 1.0f / static_cast<float>(kWindow);
        if (m_phase >= 1.0f) m_phase -= 1.0f;
        float ph2 = m_phase + 0.5f;
        if (ph2 >= 1.0f) ph2 -= 1.0f;

        const float d1 = (1.0f - m_phase) * static_cast<float>(kWindow);
        const float d2 = (1.0f - ph2)     * static_cast<float>(kWindow);

        // Hann pair offset by half a window sums to exactly 1, so a steady
        // signal comes through without amplitude ripple.
        const float g1 = 0.5f - 0.5f * std::cos(6.28318530718f * m_phase);
        const float g2 = 0.5f - 0.5f * std::cos(6.28318530718f * ph2);

        const float y = g1 * read(d1) + g2 * read(d2);

        if (++m_write >= kSize) m_write = 0;
        return y;
    }

private:
    static constexpr int kSize   = 4096;
    static constexpr int kWindow = 2048;   // must be <= kSize/2

    float read(float delay) const {
        float pos = static_cast<float>(m_write) - delay;
        while (pos < 0.0f) pos += static_cast<float>(kSize);
        const int   i0 = static_cast<int>(pos);
        const float fr = pos - static_cast<float>(i0);
        const int   i1 = (i0 + 1) % kSize;
        return m_buf[i0] + (m_buf[i1] - m_buf[i0]) * fr;
    }

    float m_buf[kSize] = {};
    int   m_write = 0;
    float m_phase = 0.0f;
};

} // namespace engine
} // namespace m8
