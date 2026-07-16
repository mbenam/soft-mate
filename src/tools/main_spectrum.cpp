// ===========================================================================
// src/tools/main_spectrum.cpp
//
// A/B spectral comparison tool (M8_AUDIO_ANALYSIS_SPEC.md Part D). Given a
// hardware-captured reference WAV and a test WAV (usually from m8_render),
// reports where their spectra diverge: fundamental, a harmonic/sideband
// table (freq, ref dB, test dB, delta), spectral centroid, and a single
// scalar log-spectral distance to minimise while tuning a synth model.
//
//   m8_spectrum --ref m8_capture.wav --test my_render.wav
//   m8_spectrum --ref ref.wav --test mine.wav --no-align --json diff.json
//
// This tool does not render — it only compares two existing files.
// Links m8_engine (for kissfft + dr_wav). No SDL.
// ===========================================================================

#include "analysis/AudioMetrics.h"
#include "analysis/Fft.h"

#define DR_WAV_IMPLEMENTATION
#include "engine/dr_wav.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

using m8::analysis::magnitudeSpectrum;
using m8::analysis::spectralCentroidHz;

static void printUsage() {
    std::fprintf(stderr,
        "usage: m8_spectrum --ref <ref.wav> --test <test.wav> [--no-align] [--json <out.json>]\n");
}

// ---------------------------------------------------------------- helpers

static std::vector<float> downmix(const float* interleaved, size_t frames, unsigned channels) {
    std::vector<float> mono(frames);
    if (channels <= 1) {
        std::memcpy(mono.data(), interleaved, frames * sizeof(float));
        return mono;
    }
    for (size_t i = 0; i < frames; ++i) {
        double sum = 0.0;
        for (unsigned c = 0; c < channels; ++c) sum += interleaved[i * channels + c];
        mono[i] = static_cast<float>(sum / channels);
    }
    return mono;
}

// First sample index where the short-window RMS envelope exceeds a fixed
// fraction of the file's own peak envelope. Independent per-file onset
// detection is simpler and more robust for single-note captures than blind
// cross-correlation, and satisfies "align the onsets" without ever needing
// to physically shift or pad a buffer (see windowStart in main()).
static size_t detectOnset(const std::vector<float>& mono, int sr) {
    const size_t win = std::max<size_t>(1, static_cast<size_t>(sr) / 750); // ~1.3 ms
    if (mono.size() < win) return 0;

    const size_t nWin = mono.size() / win;
    std::vector<float> env(nWin);
    float peakEnv = 0.0f;
    for (size_t w = 0; w < nWin; ++w) {
        double sum = 0.0;
        for (size_t i = 0; i < win; ++i) {
            float s = mono[w * win + i];
            sum += static_cast<double>(s) * s;
        }
        env[w] = static_cast<float>(std::sqrt(sum / win));
        peakEnv = std::max(peakEnv, env[w]);
    }
    if (peakEnv <= 0.0f) return 0;

    const float thresh = peakEnv * 0.1f; // -20 dB relative to the loudest window
    for (size_t w = 0; w < nWin; ++w)
        if (env[w] >= thresh) return w * win;
    return 0;
}

static float hannWindowSum(size_t n) {
    if (n < 2) return static_cast<float>(n);
    const double pi = 3.141592653589793238462643383279502884197169399375105820974944;
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i)
        sum += 0.5 * (1.0 - std::cos(2.0 * pi * i / (n - 1)));
    return static_cast<float>(sum);
}

// Coherent-gain calibration so a full-scale sine centred on a bin reads
// ~0 dBFS, independent of window length. Floor keeps silence/noise bins from
// producing -inf.
static constexpr float kFloorDb = -120.0f;
static std::vector<float> toDbfs(const std::vector<float>& mag, float windowSum) {
    const float scale = 2.0f / windowSum;
    std::vector<float> db(mag.size());
    for (size_t i = 0; i < mag.size(); ++i) {
        float a = mag[i] * scale;
        db[i] = (a > 1e-9f) ? 20.0f * std::log10(a) : kFloorDb;
    }
    return db;
}

// 3-point parabolic interpolation around a magnitude-spectrum bin.
static float refineBinFreq(const std::vector<float>& mag, size_t bin, float binHz) {
    float f = static_cast<float>(bin) * binHz;
    if (bin > 0 && bin + 1 < mag.size()) {
        float a = mag[bin - 1], b = mag[bin], c = mag[bin + 1];
        float denom = 2.0f * (2.0f * b - a - c);
        if (std::fabs(denom) > 1e-12f) f += ((a - c) / denom) * binHz;
    }
    return f;
}

// Coarse global-max search over a plausible fundamental band, refined with
// refineBinFreq. Good enough for the single sustained note this tool expects
// (see M8_CAPTURE_SPEC.md / m8_makeprobe) — a harmonic-rich tone's loudest
// low partial is normally the fundamental.
static float findFundamentalHz(const std::vector<float>& mag, float binHz, int sr) {
    const float loHz = 20.0f, hiHz = std::min(5000.0f, sr * 0.5f);
    size_t loBin = std::max<size_t>(1, static_cast<size_t>(loHz / binHz));
    size_t hiBin = std::min(mag.size() - 1, static_cast<size_t>(hiHz / binHz));
    if (loBin >= hiBin) return 0.0f;

    size_t peakBin = loBin;
    for (size_t i = loBin + 1; i <= hiBin; ++i)
        if (mag[i] > mag[peakBin]) peakBin = i;

    return refineBinFreq(mag, peakBin, binHz);
}

struct Peak { size_t bin; float freqHz; float refDb; };

// Local maxima above (spectrumMaxDb - 60dB), merged within a few bins —
// under a Hann window a real partial often shows as a small extra local max
// in the bin next door; keep the louder of any pair that close together.
static std::vector<Peak> pickPeaks(const std::vector<float>& mag, const std::vector<float>& db, float binHz) {
    if (mag.size() < 3) return {};
    float maxDb = *std::max_element(db.begin(), db.end());
    float floorDb = maxDb - 60.0f;

    std::vector<Peak> peaks;
    for (size_t i = 1; i + 1 < mag.size(); ++i) {
        if (mag[i] > mag[i - 1] && mag[i] > mag[i + 1] && db[i] >= floorDb)
            peaks.push_back({ i, refineBinFreq(mag, i, binHz), db[i] });
    }

    const float mergeHz = binHz * 3.0f;
    std::vector<Peak> merged;
    for (const auto& p : peaks) {
        if (!merged.empty() && std::fabs(p.freqHz - merged.back().freqHz) < mergeHz) {
            if (p.refDb > merged.back().refDb) merged.back() = p;
        } else {
            merged.push_back(p);
        }
    }
    return merged;
}

// Mean |dB difference| across bins (skip bin 0 / DC) — the single scalar an
// agent can minimise while tuning a synth model.
static float logSpectralDistance(const std::vector<float>& dbRef, const std::vector<float>& dbTest) {
    size_t n = std::min(dbRef.size(), dbTest.size());
    if (n < 2) return 0.0f;
    double sum = 0.0;
    for (size_t i = 1; i < n; ++i) sum += std::fabs(dbRef[i] - dbTest[i]);
    return static_cast<float>(sum / (n - 1));
}

static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\' || c == '"') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

// ---------------------------------------------------------------- main

int main(int argc, char** argv) {
    std::string refPath, testPath, jsonPath;
    bool align = true;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };

        if      (a == "--ref")      refPath = next();
        else if (a == "--test")     testPath = next();
        else if (a == "--json")     jsonPath = next();
        else if (a == "--no-align") align = false;
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); printUsage(); return 2; }
    }

    if (refPath.empty() || testPath.empty()) {
        printUsage();
        return 2;
    }

    unsigned int chRef = 0, srRef = 0, chTest = 0, srTest = 0;
    drwav_uint64 framesRef = 0, framesTest = 0;
    float* pcmRef = drwav_open_file_and_read_pcm_frames_f32(
        refPath.c_str(), &chRef, &srRef, &framesRef, nullptr);
    if (!pcmRef) { std::fprintf(stderr, "error: cannot read %s\n", refPath.c_str()); return 2; }

    float* pcmTest = drwav_open_file_and_read_pcm_frames_f32(
        testPath.c_str(), &chTest, &srTest, &framesTest, nullptr);
    if (!pcmTest) {
        std::fprintf(stderr, "error: cannot read %s\n", testPath.c_str());
        drwav_free(pcmRef, nullptr);
        return 2;
    }

    if (srRef != srTest) {
        std::fprintf(stderr, "error: sample rate mismatch: ref %u Hz vs test %u Hz\n", srRef, srTest);
        drwav_free(pcmRef, nullptr); drwav_free(pcmTest, nullptr);
        return 2;
    }
    const int sr = static_cast<int>(srRef);

    std::vector<float> monoRef  = downmix(pcmRef,  static_cast<size_t>(framesRef),  chRef);
    std::vector<float> monoTest = downmix(pcmTest, static_cast<size_t>(framesTest), chTest);
    drwav_free(pcmRef, nullptr);
    drwav_free(pcmTest, nullptr);

    const size_t onsetRef  = align ? detectOnset(monoRef,  sr) : 0;
    const size_t onsetTest = align ? detectOnset(monoTest, sr) : 0;

    // Skip the attack transient — first ~50 ms, where the two are guaranteed
    // to differ most and where the steady oscillator content (what this tool
    // is actually comparing) hasn't settled yet.
    const size_t attackSkip = static_cast<size_t>(sr) * 50 / 1000;
    const size_t startRef  = onsetRef  + attackSkip;
    const size_t startTest = onsetTest + attackSkip;

    if (startRef >= monoRef.size() || startTest >= monoTest.size()) {
        std::fprintf(stderr, "error: recording too short after onset+attack skip "
                              "(ref onset %zu, test onset %zu, skip %zu samples)\n",
                     onsetRef, onsetTest, attackSkip);
        return 2;
    }

    const size_t availRef  = monoRef.size()  - startRef;
    const size_t availTest = monoTest.size() - startTest;
    constexpr size_t kMaxWindow = 1 << 16;   // ~1.4 s at 48 kHz — plenty of frequency resolution
    constexpr size_t kMinWindow = 2048;
    const size_t windowLen = std::min({ availRef, availTest, kMaxWindow });

    if (windowLen < kMinWindow) {
        std::fprintf(stderr, "error: sustained portion too short to analyse "
                              "(%zu samples, need >= %zu)\n", windowLen, kMinWindow);
        return 2;
    }

    std::vector<float> refWin(monoRef.begin() + startRef, monoRef.begin() + startRef + windowLen);
    std::vector<float> testWin(monoTest.begin() + startTest, monoTest.begin() + startTest + windowLen);

    auto magRef  = magnitudeSpectrum(refWin.data(),  windowLen);
    auto magTest = magnitudeSpectrum(testWin.data(), windowLen);
    const float windowSum = hannWindowSum(windowLen);
    auto dbRef  = toDbfs(magRef,  windowSum);
    auto dbTest = toDbfs(magTest, windowSum);
    const float binHz = static_cast<float>(sr) / static_cast<float>(windowLen);

    const float fundRef  = findFundamentalHz(magRef,  binHz, sr);
    const float fundTest = findFundamentalHz(magTest, binHz, sr);
    const bool fundOk = fundRef > 0.0f && fundTest > 0.0f
                      && std::fabs(fundTest - fundRef) < fundRef * 0.02f; // within ~2% (~34 cents)

    const float centroidRef  = spectralCentroidHz(refWin.data(),  windowLen, sr);
    const float centroidTest = spectralCentroidHz(testWin.data(), windowLen, sr);

    const float lsd = logSpectralDistance(dbRef, dbTest);

    auto peaks = pickPeaks(magRef, dbRef, binHz);
    // Same window length -> same bin count/binHz for ref and test, so the
    // bin index found in ref's spectrum reads the matching frequency in
    // test's spectrum directly. (The parabolically-refined freqHz is for
    // display only — reading dbTest at that continuous frequency instead of
    // at the same integer bin would compare ref's peak to a slightly
    // different point in test's spectrum, which is wrong even when the two
    // signals are identical.)
    std::vector<float> testDbAtPeak(peaks.size());
    for (size_t i = 0; i < peaks.size(); ++i)
        testDbAtPeak[i] = dbTest[peaks[i].bin];

    // ---- report ----
    std::printf("ref:   %s  (%u ch, %u Hz, onset %.1f ms)\n",
                refPath.c_str(), chRef, srRef, 1000.0 * onsetRef / sr);
    std::printf("test:  %s  (%u ch, %u Hz, onset %.1f ms)\n",
                testPath.c_str(), chTest, srTest, 1000.0 * onsetTest / sr);
    std::printf("analysis window: %zu samples (%.1f ms), %.2f Hz/bin%s\n\n",
                windowLen, 1000.0 * windowLen / sr, binHz, align ? "" : "  [--no-align]");

    std::printf("fundamental   ref %8.1f Hz   test %8.1f Hz   %s\n",
                fundRef, fundTest, fundOk ? "OK" : "MISMATCH");
    std::printf("harmonics:\n");
    std::printf("  freq(Hz)   ref(dB)  test(dB)   delta\n");
    for (size_t i = 0; i < peaks.size(); ++i) {
        float delta = testDbAtPeak[i] - peaks[i].refDb;
        const char* flag = (std::fabs(delta) >= 3.0f) ? (delta < 0 ? "  <-- test low"  : "  <-- test high") : "";
        std::printf("  %8.1f   %6.1f   %7.1f   %+6.1f%s\n",
                    peaks[i].freqHz, peaks[i].refDb, testDbAtPeak[i], delta, flag);
    }
    std::printf("centroid      ref %6.0f Hz   test %6.0f Hz   (%+.0f, test %s)\n",
                centroidRef, centroidTest, centroidTest - centroidRef,
                centroidTest > centroidRef ? "brighter" : "darker");
    std::printf("log-spectral distance: %.2f dB\n", lsd);

    if (!jsonPath.empty()) {
        FILE* f = std::fopen(jsonPath.c_str(), "w");
        if (!f) {
            std::fprintf(stderr, "error: cannot write %s\n", jsonPath.c_str());
            return 2;
        }
        std::fprintf(f, "{\n");
        std::fprintf(f, "  \"ref\": \"%s\",\n", jsonEscape(refPath).c_str());
        std::fprintf(f, "  \"test\": \"%s\",\n", jsonEscape(testPath).c_str());
        std::fprintf(f, "  \"sample_rate\": %d,\n", sr);
        std::fprintf(f, "  \"window_samples\": %zu,\n", windowLen);
        std::fprintf(f, "  \"aligned\": %s,\n", align ? "true" : "false");
        std::fprintf(f, "  \"onset_ref_samples\": %zu,\n", onsetRef);
        std::fprintf(f, "  \"onset_test_samples\": %zu,\n", onsetTest);
        std::fprintf(f, "  \"fundamental_ref_hz\": %.3f,\n", fundRef);
        std::fprintf(f, "  \"fundamental_test_hz\": %.3f,\n", fundTest);
        std::fprintf(f, "  \"fundamental_ok\": %s,\n", fundOk ? "true" : "false");
        std::fprintf(f, "  \"centroid_ref_hz\": %.3f,\n", centroidRef);
        std::fprintf(f, "  \"centroid_test_hz\": %.3f,\n", centroidTest);
        std::fprintf(f, "  \"log_spectral_distance_db\": %.4f,\n", lsd);
        std::fprintf(f, "  \"harmonics\": [\n");
        for (size_t i = 0; i < peaks.size(); ++i) {
            std::fprintf(f, "    {\"freq_hz\": %.2f, \"ref_db\": %.2f, \"test_db\": %.2f, \"delta_db\": %.2f}%s\n",
                         peaks[i].freqHz, peaks[i].refDb, testDbAtPeak[i],
                         testDbAtPeak[i] - peaks[i].refDb,
                         (i + 1 < peaks.size()) ? "," : "");
        }
        std::fprintf(f, "  ]\n");
        std::fprintf(f, "}\n");
        std::fclose(f);
        std::printf("\nwrote %s\n", jsonPath.c_str());
    }

    return 0;
}
