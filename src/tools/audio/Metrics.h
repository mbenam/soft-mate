#pragma once

// ===========================================================================
// Metrics.h — the audio measurements, written once.
//
// Why this is a header and not three copies
// -----------------------------------------
// Verifying the sampler on 2026-08-24 needed a loop period and a pitch. Both
// were written from scratch three times in one session and were wrong twice:
// once an onset detector locked onto the sequencer's ROW rate instead of the
// sampler's loop (at STEPS 0x40 the two are both a quarter beat, so it
// "passed" while measuring the wrong thing), once autocorrelation caught a
// sub-harmonic because the search range was a guess.
//
// Measuring FILTER 05 the same day needed band energies, and a fourth ad-hoc
// implementation -- one-pole splits at 400 Hz and 2.5 kHz -- turned out to be
// too blunt to resolve the differences it was aimed at. That is the pattern:
// ad-hoc measurement code is where the wrong answers live.
//
// So they live here, and m8_analyze and m8_sweep share them.
// ===========================================================================

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace m8 {
namespace audio {

// Peak-per-block envelope, the input to the period search.
inline std::vector<float> envelopeOf(const std::vector<float>& mono, int block) {
    std::vector<float> env;
    if (block <= 0) return env;
    for (size_t i = 0; i + static_cast<size_t>(block) < mono.size();
         i += static_cast<size_t>(block)) {
        float pk = 0.0f;
        for (int j = 0; j < block; ++j) pk = std::max(pk, std::fabs(mono[i + j]));
        env.push_back(pk);
    }
    return env;
}

// Repeat period in samples, by autocorrelation of the envelope.
//
// "How often does this repeat", which is what a loop length is. Returns 0 if
// nothing correlates. corrOut carries the confidence: a low value means the
// answer is noise, and the caller should say so rather than print it.
inline int measurePeriod(const std::vector<float>& mono, int loSamples, int hiSamples,
                         double& corrOut) {
    const int block = 16;
    std::vector<float> env = envelopeOf(mono, block);
    if (env.size() < 8) { corrOut = 0.0; return 0; }
    double mean = 0.0;
    for (float v : env) mean += v;
    mean /= double(env.size());
    for (float& v : env) v = static_cast<float>(v - mean);

    const int loLag = std::max(1, loSamples / block);
    const int hiLag = std::min<int>(hiSamples / block, static_cast<int>(env.size()) / 2);
    int bestLag = 0; double best = -2.0;
    for (int lag = loLag; lag < hiLag; ++lag) {
        double num = 0, da = 0, db = 0;
        for (size_t i = 0; i + static_cast<size_t>(lag) < env.size(); ++i) {
            num += double(env[i]) * env[i + lag];
            da  += double(env[i]) * env[i];
            db  += double(env[i + lag]) * env[i + lag];
        }
        const double c = (da > 0 && db > 0) ? num / std::sqrt(da * db) : 0.0;
        if (c > best) { best = c; bestLag = lag * block; }
    }
    corrOut = best;
    return bestLag;
}

// Fundamental in Hz, by zero-crossing rate over a steady window.
//
// "How fast is this playing", which is how a resampling ratio shows up. A
// different question from the period, and asking the wrong one returns a
// plausible number -- so both exist rather than one.
inline double measurePitch(const std::vector<float>& mono, int sampleRate,
                           size_t fromFrame, size_t toFrame) {
    toFrame = std::min(toFrame, mono.size());
    if (toFrame <= fromFrame + 64) return 0.0;
    int crossings = 0;
    for (size_t i = fromFrame + 1; i < toFrame; ++i)
        if ((mono[i - 1] < 0.0f) != (mono[i] < 0.0f)) ++crossings;
    if (crossings < 4) return 0.0;
    const double seconds = double(toFrame - fromFrame) / double(sampleRate);
    return (crossings / 2.0) / seconds;
}

struct Bands {
    double rms = 0.0, low = 0.0, mid = 0.0, high = 0.0;
    // Ratios are what a filter shape actually shows up as; absolute levels move
    // with the source and the send levels.
    double lowRatio()  const { return rms > 0 ? low  / rms : 0.0; }
    double midRatio()  const { return rms > 0 ? mid  / rms : 0.0; }
    double highRatio() const { return rms > 0 ? high / rms : 0.0; }
};

// Energy split by two one-pole corners.
//
// Blunt on purpose -- it is a shape indicator, not a spectrum. Measuring
// FILTER 05 with it produced differences of 0.02-0.07 against run-to-run noise
// of about 0.08, i.e. nothing. If a question needs better than that, it needs
// m8_spectrum and an FFT, not a wider gap between these corners.
inline Bands measureBands(const std::vector<float>& mono, int sampleRate,
                          double lowHz = 400.0, double highHz = 2500.0) {
    Bands b;
    if (mono.empty() || sampleRate <= 0) return b;
    const double aLow  = std::exp(-2.0 * 3.14159265358979 * lowHz  / sampleRate);
    const double aHigh = std::exp(-2.0 * 3.14159265358979 * highHz / sampleRate);
    double yLow = 0.0, yHigh = 0.0;
    double sTot = 0.0, sLow = 0.0, sMid = 0.0, sHigh = 0.0;
    for (float v : mono) {
        yLow  = aLow  * yLow  + (1.0 - aLow)  * v;
        yHigh = aHigh * yHigh + (1.0 - aHigh) * v;
        const double mid = yHigh - yLow;
        const double hi  = v - yHigh;
        sTot += double(v) * v; sLow += yLow * yLow; sMid += mid * mid; sHigh += hi * hi;
    }
    const double n = double(mono.size());
    b.rms  = std::sqrt(sTot / n);
    b.low  = std::sqrt(sLow / n);
    b.mid  = std::sqrt(sMid / n);
    b.high = std::sqrt(sHigh / n);
    return b;
}

} // namespace audio
} // namespace m8
