#pragma once

#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>

extern "C" {
#include "kiss_fftr.h"
}

namespace m8::analysis {

inline std::vector<float> magnitudeSpectrum(const float* mono, size_t n) {
    if (n == 0) return {};

    const size_t nfft = n;
    const size_t specSize = nfft / 2 + 1;

    // Allocate kissfft config (freed automatically via RAII)
    kiss_fftr_cfg cfg = kiss_fftr_alloc(static_cast<int>(nfft), 0, nullptr, nullptr);
    if (!cfg) return {};

    // Apply Hann window and pack into time-domain buffer
    std::vector<float> windowed(nfft);
    const double pi = 3.141592653589793238462643383279502884197169399375105820974944;
    for (size_t i = 0; i < nfft; ++i) {
        float w = 0.5f * static_cast<float>(1.0 - std::cos(2.0 * pi * i / (nfft - 1)));
        windowed[i] = mono[i] * w;
    }

    // Forward real FFT
    std::vector<kiss_fft_cpx> freq(specSize);
    kiss_fftr(cfg, windowed.data(), freq.data());

    // Compute magnitude spectrum
    std::vector<float> mag(specSize);
    for (size_t i = 0; i < specSize; ++i) {
        mag[i] = std::sqrt(freq[i].r * freq[i].r + freq[i].i * freq[i].i);
    }

    kiss_fftr_free(cfg);
    return mag;
}

} // namespace m8::analysis
