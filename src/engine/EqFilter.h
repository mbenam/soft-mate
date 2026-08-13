#pragma once
#include <cmath>
#include <cstring>
#include <algorithm>

// -----------------------------------------------------------------------------
// 3-band parametric EQ (EQ_SPEC.md).
//
// The M8 puts one of these on each instrument (via a bank index), on the main
// mix, and on each of the three send effects. This header holds both the data
// -- the shape the .m8s carries -- and the DSP, so the engine can include it
// without depending on anything above it.
//
// Filter forms are the RBJ cookbook biquads, which is what every parametric EQ
// uses; the M8's own DSP is not published, so these are the standard reference
// rather than a port. Where the spec's numbers came from is recorded there:
// the byte encoding is confirmed from real files, the seven filter types and
// five stereo modes were read off a device, and the Q curve was measured from
// the device's own response display.
// -----------------------------------------------------------------------------

namespace m8 {
namespace engine {

// Filter types, confirmed on hardware (firmware 6.5.0).
enum class EqType : int {
    LOWCUT = 0, LOWSHELF = 1, BELL = 2, BANDPASS = 3,
    HISHELF = 4, HICUT = 5, ALLPASS = 6
};

// How a band is applied to the stereo signal.
enum class EqMode : int {
    STEREO = 0, MID = 1, SIDE = 2, LEFT = 3, RIGHT = 4
};

// One band of an EQ bank. Six bytes in the file; see EQ_SPEC.md §3-4 for the
// encoding, which is confirmed rather than inferred.
struct EqBand {
    int type = 1;        // EqType
    int mode = 0;        // EqMode
    int freq = 100;      // Hz, straight from the file's coarse/fine pair
    int gain = 0;        // HUNDREDTHS of a dB, signed. 600 = +6.00 dB
    int q = 50;          // 0..99 as shown on the device

    // The file packs type into bits 0-2 and mode into bits 5-7 of one byte.
    // Bits 3-4 have no known meaning, so the original byte is kept and used as
    // the base when writing back -- rebuilding it from type|mode alone would
    // zero those bits and break the byte-identical round-trip on any file that
    // uses them.
    int rawModeType = 0x01;
};

// A bank of three bands. Defaults are the M8's factory values, identical in
// every song file examined -- also what EDIT+OPTION resets to.
struct EqBank {
    EqBand low  { 1, 0,  100, 0, 50, 0x01 };   // LOWSHELF
    EqBand mid  { 2, 0, 1000, 0, 50, 0x02 };   // BELL
    EqBand high { 4, 0, 5000, 0, 50, 0x04 };   // HI.SHELF
};

// The file holds 32 banks on V4 and 128 on V4.1+. We always carry 128 and only
// load/save as many as the song actually has, so the count never needs storing.
inline constexpr int kMaxEqBanks = 128;

// Q byte -> filter Q. Measured from the device's own curve display: the byte
// runs 0..99 and spans about two decades, with the factory default of 50
// landing on Q = 1.0 (EQ_SPEC.md §8). An approximation read off screenshots,
// not an audio measurement -- but the default hitting exactly 1.0 is what makes
// it credible rather than merely fitted.
inline float eqQFromByte(int qByte) {
    const float b = static_cast<float>(std::clamp(qByte, 0, 99));
    return std::pow(10.0f, (b - 50.0f) / 50.0f);   // 0.1 .. ~9.8
}

// -----------------------------------------------------------------------------
// One biquad, transposed direct form II. No allocation, no branching in the
// hot path -- safe for the audio thread.
// -----------------------------------------------------------------------------
class EqBiquad {
public:
    void reset() { m_z1 = 0.0f; m_z2 = 0.0f; }

    // Returns false if this band is a no-op, so the caller can skip it
    // entirely. A gain-based type (bell or shelf) at 0 dB changes nothing, and
    // the factory default EQ is exactly that -- three flat bands -- so an
    // untouched EQ must cost nothing and must not alter a single sample.
    bool setParams(int typeRaw, float freqHz, float gainDb, float q, float sampleRate) {
        const EqType type = static_cast<EqType>(std::clamp(typeRaw, 0, 6));
        const bool gainBased = (type == EqType::BELL || type == EqType::LOWSHELF
                                                     || type == EqType::HISHELF);
        if (gainBased && std::fabs(gainDb) < 1e-6f) return false;

        // Keep the corner inside the band; tan() blows up approaching Nyquist.
        const float f0 = std::clamp(freqHz, 10.0f, sampleRate * 0.45f);
        const float w0 = 6.28318530718f * f0 / sampleRate;
        const float cw = std::cos(w0);
        const float sw = std::sin(w0);
        const float qq = std::max(q, 0.05f);
        const float alpha = sw / (2.0f * qq);
        const float A = std::pow(10.0f, gainDb / 40.0f);   // amplitude, not power

        float b0, b1, b2, a0, a1, a2;
        switch (type) {
        case EqType::LOWCUT:                                   // high-pass
            b0 = (1.0f + cw) * 0.5f; b1 = -(1.0f + cw); b2 = b0;
            a0 = 1.0f + alpha; a1 = -2.0f * cw; a2 = 1.0f - alpha;
            break;
        case EqType::HICUT:                                    // low-pass
            b0 = (1.0f - cw) * 0.5f; b1 = 1.0f - cw; b2 = b0;
            a0 = 1.0f + alpha; a1 = -2.0f * cw; a2 = 1.0f - alpha;
            break;
        case EqType::BANDPASS:                                 // 0 dB peak gain
            b0 = alpha; b1 = 0.0f; b2 = -alpha;
            a0 = 1.0f + alpha; a1 = -2.0f * cw; a2 = 1.0f - alpha;
            break;
        case EqType::ALLPASS:                                  // flat magnitude
            b0 = 1.0f - alpha; b1 = -2.0f * cw; b2 = 1.0f + alpha;
            a0 = 1.0f + alpha; a1 = -2.0f * cw; a2 = 1.0f - alpha;
            break;
        case EqType::LOWSHELF: {
            const float s = 2.0f * std::sqrt(A) * alpha;
            b0 =        A * ((A + 1.0f) - (A - 1.0f) * cw + s);
            b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw);
            b2 =        A * ((A + 1.0f) - (A - 1.0f) * cw - s);
            a0 =             (A + 1.0f) + (A - 1.0f) * cw + s;
            a1 =    -2.0f * ((A - 1.0f) + (A + 1.0f) * cw);
            a2 =             (A + 1.0f) + (A - 1.0f) * cw - s;
            break;
        }
        case EqType::HISHELF: {
            const float s = 2.0f * std::sqrt(A) * alpha;
            b0 =         A * ((A + 1.0f) + (A - 1.0f) * cw + s);
            b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw);
            b2 =         A * ((A + 1.0f) + (A - 1.0f) * cw - s);
            a0 =              (A + 1.0f) - (A - 1.0f) * cw + s;
            a1 =      2.0f * ((A - 1.0f) - (A + 1.0f) * cw);
            a2 =              (A + 1.0f) - (A - 1.0f) * cw - s;
            break;
        }
        case EqType::BELL:
        default:
            b0 = 1.0f + alpha * A; b1 = -2.0f * cw; b2 = 1.0f - alpha * A;
            a0 = 1.0f + alpha / A; a1 = -2.0f * cw; a2 = 1.0f - alpha / A;
            break;
        }

        const float inv = 1.0f / a0;
        m_b0 = b0 * inv; m_b1 = b1 * inv; m_b2 = b2 * inv;
        m_a1 = a1 * inv; m_a2 = a2 * inv;
        return true;
    }

    inline float process(float x) {
        const float y = m_b0 * x + m_z1;
        m_z1 = m_b1 * x - m_a1 * y + m_z2;
        m_z2 = m_b2 * x - m_a2 * y;
        return y;
    }

    // Magnitude of the frequency response at one frequency, as a linear gain.
    // Evaluates |H(z)| on the unit circle -- this is what draws the curve on
    // the EQ editor, so it must not require running audio through the filter.
    float magnitudeAt(float freqHz, float sampleRate) const {
        const float w = 6.28318530718f * freqHz / sampleRate;
        const float c1 = std::cos(w),  s1 = std::sin(w);
        const float c2 = std::cos(2.0f * w), s2 = std::sin(2.0f * w);
        // z^-1 = cos(w) - j sin(w)
        const float numRe = m_b0 + m_b1 * c1 + m_b2 * c2;
        const float numIm =      -(m_b1 * s1 + m_b2 * s2);
        const float denRe = 1.0f + m_a1 * c1 + m_a2 * c2;
        const float denIm =      -(m_a1 * s1 + m_a2 * s2);
        const float den = std::sqrt(denRe * denRe + denIm * denIm);
        if (den < 1e-12f) return 1.0f;
        return std::sqrt(numRe * numRe + numIm * numIm) / den;
    }

private:
    float m_b0 = 1.0f, m_b1 = 0.0f, m_b2 = 0.0f, m_a1 = 0.0f, m_a2 = 0.0f;
    float m_z1 = 0.0f, m_z2 = 0.0f;
};

// -----------------------------------------------------------------------------
// A whole EQ: three bands, each with its own stereo mode.
//
// Two biquads per band is enough for every mode -- STEREO uses both (one per
// channel), LEFT/RIGHT use one, and MID/SIDE use one on the encoded signal.
// -----------------------------------------------------------------------------
class EqProcessor {
public:
    void reset() {
        for (auto& b : m_bands) { b.a.reset(); b.b.reset(); }
    }

    // Recompute coefficients only when the bank actually changed. Cheap enough
    // to call once per render block; must NOT be called per sample.
    void configure(const EqBank& bank, float sampleRate) {
        if (m_configured && std::memcmp(&bank, &m_last, sizeof(EqBank)) == 0) return;
        m_last = bank;
        m_configured = true;
        m_anyActive = false;

        const EqBand* src[3] = { &bank.low, &bank.mid, &bank.high };
        for (int i = 0; i < 3; ++i) {
            const float gainDb = src[i]->gain / 100.0f;   // stored in hundredths
            const float q = eqQFromByte(src[i]->q);
            const bool active = m_bands[i].a.setParams(src[i]->type, float(src[i]->freq),
                                                       gainDb, q, sampleRate);
            m_bands[i].b.setParams(src[i]->type, float(src[i]->freq), gainDb, q, sampleRate);
            m_bands[i].mode = std::clamp(src[i]->mode, 0, 4);
            m_bands[i].active = active;
            if (active) m_anyActive = true;
        }
    }

    // True when every band is a no-op, so callers can skip the whole stage.
    bool isBypass() const { return !m_anyActive; }

    // Combined response in dB at one frequency -- the curve the editor draws.
    // Bands in series multiply, so their dB contributions add. Stereo mode is
    // ignored: the display shows one curve, as the device's does.
    float responseDbAt(float freqHz, float sampleRate) const {
        float db = 0.0f;
        for (const auto& band : m_bands) {
            if (!band.active) continue;
            db += 20.0f * std::log10(std::max(band.a.magnitudeAt(freqHz, sampleRate), 1e-9f));
        }
        return db;
    }

    inline void process(float& l, float& r) {
        if (!m_anyActive) return;
        for (auto& band : m_bands) {
            if (!band.active) continue;
            switch (static_cast<EqMode>(band.mode)) {
            case EqMode::LEFT:
                l = band.a.process(l);
                break;
            case EqMode::RIGHT:
                r = band.b.process(r);
                break;
            case EqMode::MID: {
                float m = 0.5f * (l + r), s = 0.5f * (l - r);
                m = band.a.process(m);
                l = m + s; r = m - s;
                break;
            }
            case EqMode::SIDE: {
                float m = 0.5f * (l + r), s = 0.5f * (l - r);
                s = band.a.process(s);
                l = m + s; r = m - s;
                break;
            }
            case EqMode::STEREO:
            default:
                l = band.a.process(l);
                r = band.b.process(r);
                break;
            }
        }
    }

private:
    struct Band {
        EqBiquad a, b;
        int mode = 0;
        bool active = false;
    };
    Band m_bands[3];
    EqBank m_last{};
    bool m_configured = false;
    bool m_anyActive = false;
};

} // namespace engine
} // namespace m8
