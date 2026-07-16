#include "AudioMetrics.h"
#include "Fft.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <vector>

namespace m8::analysis {

Metrics analyze(const float* interleavedStereo, size_t frames, int sampleRate) {
    Metrics m{};
    if (frames == 0 || !interleavedStereo) return m;

    // Whole-file peak, RMS, DC, clipped, non-finite
    double sumSqL = 0.0, sumSqR = 0.0;
    double sumL = 0.0, sumR = 0.0;
    float peak = 0.0f;
    int clipped = 0;
    int nonFinite = 0;

    for (size_t i = 0; i < frames; ++i) {
        float l = interleavedStereo[i * 2];
        float r = interleavedStereo[i * 2 + 1];

        if (!std::isfinite(l) || !std::isfinite(r)) {
            ++nonFinite;
            continue;
        }

        float al = std::fabs(l);
        float ar = std::fabs(r);
        peak = std::max(peak, std::max(al, ar));

        if (al >= 0.999f || ar >= 0.999f) ++clipped;

        sumL += l;
        sumR += r;
        sumSqL += static_cast<double>(l) * l;
        sumSqR += static_cast<double>(r) * r;
    }

    m.peak = peak;
    m.clipped = clipped;
    m.nonFinite = nonFinite;
    m.dcL = static_cast<float>(sumL / frames);
    m.dcR = static_cast<float>(sumR / frames);

    double meanSqL = sumSqL / frames;
    double meanSqR = sumSqR / frames;
    float rmsL = static_cast<float>(std::sqrt(meanSqL));
    float rmsR = static_cast<float>(std::sqrt(meanSqR));
    m.rms = std::max(rmsL, rmsR);

    if (m.rms > 0.0f)
        m.crestDb = 20.0f * std::log10(m.peak / m.rms);
    else
        m.crestDb = 0.0f;

    // Mid/Side for stereo analysis
    double sumMid = 0.0, sumSide = 0.0;
    for (size_t i = 0; i < frames; ++i) {
        float l = interleavedStereo[i * 2];
        float r = interleavedStereo[i * 2 + 1];
        float mid = (l + r) * 0.5f;
        float side = (l - r) * 0.5f;
        sumMid += static_cast<double>(mid) * mid;
        sumSide += static_cast<double>(side) * side;
    }
    m.midRms = static_cast<float>(std::sqrt(sumMid / frames));
    m.sideRms = static_cast<float>(std::sqrt(sumSide / frames));

    // L/R correlation: Pearson correlation coefficient
    double sumLR = 0.0;
    for (size_t i = 0; i < frames; ++i) {
        sumLR += static_cast<double>(interleavedStereo[i * 2]) * interleavedStereo[i * 2 + 1];
    }
    double corrNum = frames * sumLR - sumL * sumR;
    double corrDen = std::sqrt((frames * sumSqL - sumL * sumL) * (frames * sumSqR - sumR * sumR));
    m.correlation = (corrDen > 0.0) ? static_cast<float>(corrNum / corrDen) : 0.0f;

    // Per-second DC windows: find worst |DC| across 1-second windows
    const size_t windowFrames = static_cast<size_t>(sampleRate);
    float worstWindowDC = 0.0f;
    for (size_t start = 0; start + windowFrames <= frames; start += windowFrames) {
        double wSumL = 0.0, wSumR = 0.0;
        for (size_t i = 0; i < windowFrames; ++i) {
            wSumL += interleavedStereo[(start + i) * 2];
            wSumR += interleavedStereo[(start + i) * 2 + 1];
        }
        float wDcL = static_cast<float>(std::fabs(wSumL / windowFrames));
        float wDcR = static_cast<float>(std::fabs(wSumR / windowFrames));
        worstWindowDC = std::max(worstWindowDC, std::max(wDcL, wDcR));
    }
    m.dcWorstWindow = worstWindowDC;

    // Longest silent gap: RMS below -60 dB (amp < 0.001)
    const float silenceThresh = 0.001f;
    size_t gapStart = 0;
    bool inGap = false;
    size_t maxGap = 0;
    for (size_t i = 0; i < frames; ++i) {
        float l = interleavedStereo[i * 2];
        float r = interleavedStereo[i * 2 + 1];
        float env = std::sqrt(static_cast<double>(l) * l + static_cast<double>(r) * r) * 0.7071f;
        if (env < silenceThresh) {
            if (!inGap) { gapStart = i; inGap = true; }
        } else {
            if (inGap) {
                maxGap = std::max(maxGap, i - gapStart);
                inGap = false;
            }
        }
    }
    if (inGap) maxGap = std::max(maxGap, frames - gapStart);
    m.longestSilenceSec = static_cast<float>(maxGap) / sampleRate;

    return m;
}

float pitchHz(const float* mono, size_t n, int sr, float expectedHz) {
    if (!mono || n < 4 || sr <= 0 || expectedHz <= 0.0f) return 0.0f;

    auto mag = magnitudeSpectrum(mono, n);
    if (mag.empty()) return 0.0f;

    const size_t bins = mag.size();
    const float binHz = static_cast<float>(sr) / n;

    // Search band: [expected * 0.85, expected * 1.18]
    float loHz = expectedHz * 0.85f;
    float hiHz = expectedHz * 1.18f;
    size_t loBin = static_cast<size_t>(std::max(1.0f, std::floor(loHz / binHz)));
    size_t hiBin = std::min(bins - 1, static_cast<size_t>(std::ceil(hiHz / binHz)));

    if (loBin >= hiBin) return 0.0f;

    // Find peak bin in band
    size_t peakBin = loBin;
    float peakMag = mag[loBin];
    for (size_t i = loBin + 1; i <= hiBin; ++i) {
        if (mag[i] > peakMag) {
            peakMag = mag[i];
            peakBin = i;
        }
    }

    if (peakMag < 1e-10f) return 0.0f;

    // Parabolic interpolation around the peak
    float f0 = static_cast<float>(peakBin) * binHz;
    if (peakBin > 0 && peakBin < bins - 1) {
        float a = mag[peakBin - 1];
        float b = mag[peakBin];
        float c = mag[peakBin + 1];
        float denom = 2.0f * (2.0f * b - a - c);
        if (std::fabs(denom) > 1e-12f) {
            float delta = (a - c) / denom;
            f0 += delta * binHz;
        }
    }

    return f0;
}

float spectralCentroidHz(const float* mono, size_t n, int sr) {
    if (!mono || n < 4 || sr <= 0) return 0.0f;

    auto mag = magnitudeSpectrum(mono, n);
    if (mag.empty()) return 0.0f;

    const size_t bins = mag.size();
    const float binHz = static_cast<float>(sr) / n;

    double sumWeight = 0.0, sumMag = 0.0;
    for (size_t i = 1; i < bins; ++i) {
        float freq = static_cast<float>(i) * binHz;
        sumWeight += freq * mag[i];
        sumMag += mag[i];
    }

    if (sumMag < 1e-12f) return 0.0f;
    return static_cast<float>(sumWeight / sumMag);
}

} // namespace m8::analysis
