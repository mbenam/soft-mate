#pragma once

#include <cstddef>
#include <cstdint>

namespace m8::analysis {

struct Metrics {
    float peak;
    float rms;
    float crestDb;
    float dcL;
    float dcR;
    float dcWorstWindow;
    int   clipped;
    int   nonFinite;
    float longestSilenceSec;
    float midRms;
    float sideRms;
    float correlation;
};

// Analyse interleaved stereo audio (L,R,L,R,...).
// sampleRate is used for silence-duration calculations.
Metrics analyze(const float* interleavedStereo, size_t frames, int sampleRate);

// Measure pitch via FFT peak in a band around expectedHz.
// Returns measured frequency in Hz, or 0 if no peak found.
float pitchHz(const float* mono, size_t n, int sr, float expectedHz);

// Spectral centroid via FFT, weighted average of frequency by magnitude.
float spectralCentroidHz(const float* mono, size_t n, int sr);

} // namespace m8::analysis
