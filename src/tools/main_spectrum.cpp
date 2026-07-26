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

#include <fstream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <ctime>

static uint64_t computeFnv1a64(const std::string& filepath, size_t& byteCount) {
    byteCount = 0;
    std::ifstream f(filepath, std::ios::binary);
    if (!f) return 0;
    uint64_t hash = 14695981039346656037ULL;
    char buffer[4096];
    while (f.read(buffer, sizeof(buffer)) || f.gcount() > 0) {
        std::streamsize count = f.gcount();
        byteCount += static_cast<size_t>(count);
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<uint8_t>(buffer[i]);
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

static std::string hexHash(uint64_t hash) {
    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return ss.str();
}

static std::string getIsoTimestampUtc() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &now_time);
#else
    gmtime_r(&now_time, &tm_buf);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return std::string(buf);
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

static void writeRecordFile(const std::string& recordPath,
                            const std::string& fullArgv,
                            const std::string& refPath, size_t bytesRef, uint64_t hashRef,
                            const std::string& testPath, size_t bytesTest, uint64_t hashTest,
                            double peakRef, double peakTest,
                            double saturationThresh, bool refSat, bool testSat,
                            double normRefDb, double normTestDb,
                            double ratio, const std::string& status) {
    if (recordPath.empty()) return;
    FILE* f = std::fopen(recordPath.c_str(), "w");
    if (!f) return;
    std::fprintf(f, "{\n");
    std::fprintf(f, "  \"tool\": \"m8_spectrum\",\n");
    std::fprintf(f, "  \"argv\": \"%s\",\n", jsonEscape(fullArgv).c_str());
    std::fprintf(f, "  \"timestamp_utc\": \"%s\",\n", getIsoTimestampUtc().c_str());
    std::fprintf(f, "  \"inputs\": {\n");
    std::fprintf(f, "    \"ref\":  { \"path\": \"%s\", \"bytes\": %zu, \"fnv1a64\": \"%s\" },\n",
                 jsonEscape(refPath).c_str(), bytesRef, hexHash(hashRef).c_str());
    std::fprintf(f, "    \"test\": { \"path\": \"%s\", \"bytes\": %zu, \"fnv1a64\": \"%s\" }\n",
                 jsonEscape(testPath).c_str(), bytesTest, hexHash(hashTest).c_str());
    std::fprintf(f, "  },\n");
    std::fprintf(f, "  \"peaks\": { \"ref\": %.9f, \"test\": %.9f },\n", peakRef, peakTest);
    std::fprintf(f, "  \"saturation\": { \"threshold\": %.3f, \"ref_saturated\": %s, \"test_saturated\": %s },\n",
                 saturationThresh, refSat ? "true" : "false", testSat ? "true" : "false");
    std::fprintf(f, "  \"norm_gain_db\": { \"ref\": %.6f, \"test\": %.6f },\n", normRefDb, normTestDb);
    std::fprintf(f, "  \"result\": { \"ratio\": %.9f, \"status\": \"%s\" }\n", ratio, status.c_str());
    std::fprintf(f, "}\n");
    std::fclose(f);
}

static void printUsage() {
    std::fprintf(stderr,
        "usage: m8_spectrum --ref <ref.wav> --test <test.wav> [--no-align] [--json <out.json>] [--record <rec.json>]\n"
        "       m8_spectrum --verify-record <record.json>\n");
}

static int runVerifyRecord(const std::string& recordPath) {
    std::ifstream f(recordPath);
    if (!f.is_open()) {
        std::fprintf(stderr, "error: cannot open record file '%s'\n", recordPath.c_str());
        return 2;
    }
    std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();

    struct InputRecord {
        std::string key;
        std::string path;
        size_t bytes = 0;
        std::string hash;
    };
    std::vector<InputRecord> inputs;

    // Find "inputs" section
    size_t inputsPos = json.find("\"inputs\"");
    if (inputsPos != std::string::npos) {
        size_t braceStart = json.find('{', inputsPos);
        if (braceStart != std::string::npos) {
            int depth = 0;
            size_t braceEnd = std::string::npos;
            for (size_t i = braceStart; i < json.size(); ++i) {
                if (json[i] == '{') depth++;
                else if (json[i] == '}') {
                    depth--;
                    if (depth == 0) {
                        braceEnd = i;
                        break;
                    }
                }
            }
            if (braceEnd != std::string::npos) {
                std::string inputsBlock = json.substr(braceStart, braceEnd - braceStart + 1);

                // Find all "path" occurrences in inputsBlock
                size_t pPos = 0;
                while ((pPos = inputsBlock.find("\"path\"", pPos)) != std::string::npos) {
                    // Find object start '{' before pPos
                    size_t objStart = inputsBlock.rfind('{', pPos);
                    // Find object end '}' after pPos
                    size_t objEnd = inputsBlock.find('}', pPos);

                    if (objStart != std::string::npos && objEnd != std::string::npos && objStart < objEnd) {
                        std::string objText = inputsBlock.substr(objStart, objEnd - objStart + 1);

                        // Find key name before objStart
                        std::string key = "input";
                        size_t keyQuoteEnd = inputsBlock.rfind('"', objStart);
                        if (keyQuoteEnd != std::string::npos && keyQuoteEnd > 0) {
                            size_t keyQuoteStart = inputsBlock.rfind('"', keyQuoteEnd - 1);
                            if (keyQuoteStart != std::string::npos) {
                                key = inputsBlock.substr(keyQuoteStart + 1, keyQuoteEnd - keyQuoteStart - 1);
                            }
                        }

                        InputRecord rec;
                        rec.key = key;

                        // Extract path
                        size_t pathValStart = objText.find(':', objText.find("\"path\""));
                        if (pathValStart != std::string::npos) {
                            size_t q1 = objText.find('"', pathValStart);
                            if (q1 != std::string::npos) {
                                std::string val;
                                bool escaped = false;
                                for (size_t i = q1 + 1; i < objText.size(); ++i) {
                                    char c = objText[i];
                                    if (escaped) {
                                        if (c == '\\' || c == '"') val.push_back(c);
                                        else if (c == 'n') val.push_back('\n');
                                        else if (c == 't') val.push_back('\t');
                                        else val.push_back(c);
                                        escaped = false;
                                    } else if (c == '\\') {
                                        escaped = true;
                                    } else if (c == '"') {
                                        break;
                                    } else {
                                        val.push_back(c);
                                    }
                                }
                                rec.path = val;
                            }
                        }

                        // Extract bytes
                        size_t bytesValStart = objText.find("\"bytes\"");
                        if (bytesValStart != std::string::npos) {
                            size_t colon = objText.find(':', bytesValStart);
                            if (colon != std::string::npos) {
                                size_t dStart = objText.find_first_of("0123456789", colon);
                                if (dStart != std::string::npos) {
                                    rec.bytes = std::stoull(objText.substr(dStart));
                                }
                            }
                        }

                        // Extract fnv1a64
                        size_t hashValStart = objText.find("\"fnv1a64\"");
                        if (hashValStart != std::string::npos) {
                            size_t colon = objText.find(':', hashValStart);
                            if (colon != std::string::npos) {
                                size_t q1 = objText.find('"', colon);
                                if (q1 != std::string::npos) {
                                    size_t q2 = objText.find('"', q1 + 1);
                                    if (q2 != std::string::npos) {
                                        rec.hash = objText.substr(q1 + 1, q2 - q1 - 1);
                                    }
                                }
                            }
                        }

                        if (!rec.path.empty()) {
                            inputs.push_back(rec);
                        }
                    }
                    pPos += 6; // move past "path"
                }
            }
        }
    }

    // Check peaks warning
    size_t peaksPos = json.find("\"peaks\"");
    if (peaksPos != std::string::npos) {
        size_t braceStart = json.find('{', peaksPos);
        if (braceStart != std::string::npos) {
            size_t braceEnd = json.find('}', braceStart);
            if (braceEnd != std::string::npos) {
                std::string peaksBlock = json.substr(braceStart, braceEnd - braceStart + 1);
                std::vector<double> peakValues;
                size_t colonPos = 0;
                while ((colonPos = peaksBlock.find(':', colonPos)) != std::string::npos) {
                    size_t dStart = peaksBlock.find_first_of("0123456789.-", colonPos);
                    if (dStart != std::string::npos) {
                        try {
                            double v = std::stod(peaksBlock.substr(dStart));
                            peakValues.push_back(v);
                        } catch (...) {}
                    }
                    colonPos++;
                }
                if (peakValues.size() >= 2 && peakValues[0] == peakValues[1]) {
                    std::printf("warning: record peak values are bit-identical (%.9f)\n", peakValues[0]);
                }
            }
        }
    }

    if (inputs.empty()) {
        std::fprintf(stderr, "error: record file '%s' contains no verifiable inputs\n", recordPath.c_str());
        return 2;
    }

    bool allOk = true;
    for (const auto& in : inputs) {
        // Test file existence first
        std::ifstream testFile(in.path, std::ios::binary);
        if (!testFile.is_open()) {
            std::printf("input '%s' (%s): MISSING\n", in.key.c_str(), in.path.c_str());
            allOk = false;
            continue;
        }
        testFile.close();

        size_t actBytes = 0;
        uint64_t actHashInt = computeFnv1a64(in.path, actBytes);
        std::string actHashStr = hexHash(actHashInt);

        std::string recHashLower = in.hash;
        for (char& c : recHashLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::string actHashLower = actHashStr;
        for (char& c : actHashLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (actBytes == in.bytes && actHashLower == recHashLower) {
            std::printf("input '%s' (%s): OK\n", in.key.c_str(), in.path.c_str());
        } else {
            std::printf("input '%s' (%s): HASH MISMATCH (recorded %s, actual %s)\n",
                        in.key.c_str(), in.path.c_str(), in.hash.c_str(), actHashStr.c_str());
            allOk = false;
        }
    }

    return allOk ? 0 : 1;
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

// Time-domain autocorrelation pitch detector. A spectral global-max (or even HPS)
// fails on bright M8 MacroSynth (Braids) tones: the note's full integer harmonic
// series is present but no single bin dominates as f0 (a captured C-4 had a
// complete 1..6x series on 261.6 Hz, yet the loudest bin was an *inharmonic*
// partial at 988 Hz, and secondary inharmonic partials at 331/657/988 even form a
// decoy series). Autocorrelation keys on the signal's *period* instead of the
// loudest partial, so the fundamental wins regardless of spectral tilt. Picking
// the smallest lag at near-peak correlation avoids latching onto a period multiple
// (a subharmonic / octave-too-low error).
static float findFundamentalAcf(const std::vector<float>& x, int sr) {
    const float loHz = 50.0f, hiHz = 2000.0f;
    const size_t minLag = std::max<size_t>(1, static_cast<size_t>(sr / hiHz));
    const size_t maxLag = static_cast<size_t>(sr / loHz);
    if (x.size() < maxLag + 64) return 0.0f;
    const size_t N = std::min(x.size() - maxLag, static_cast<size_t>(16384));

    // Sliding windowed energy E[off] = sum_{n<N} x[off+n]^2, for normalization.
    std::vector<double> E(maxLag + 1, 0.0);
    double e = 0.0;
    for (size_t n = 0; n < N; ++n) e += static_cast<double>(x[n]) * x[n];
    E[0] = e;
    for (size_t off = 1; off <= maxLag; ++off) {
        e += static_cast<double>(x[off + N - 1]) * x[off + N - 1]
           - static_cast<double>(x[off - 1]) * x[off - 1];
        E[off] = e;
    }
    if (E[0] <= 0.0) return 0.0f;

    std::vector<float> r(maxLag + 1, 0.0f);
    for (size_t lag = minLag; lag <= maxLag; ++lag) {
        double dot = 0.0;
        for (size_t n = 0; n < N; ++n) dot += static_cast<double>(x[n]) * x[n + lag];
        const double denom = std::sqrt(E[0] * E[lag]);
        r[lag] = denom > 0.0 ? static_cast<float>(dot / denom) : 0.0f;
    }

    size_t bestLag = minLag;
    for (size_t lag = minLag; lag <= maxLag; ++lag)
        if (r[lag] > r[bestLag]) bestLag = lag;
    const float maxR = r[bestLag];
    if (maxR < 0.2f) return 0.0f; // not periodic enough to call a pitch

    // Prefer the smallest lag that is a local max at >= 85% of peak correlation:
    // the true period, not one of its multiples.
    const float thresh = 0.85f * maxR;
    for (size_t lag = minLag + 1; lag + 1 <= maxLag; ++lag) {
        if (r[lag] >= thresh && r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1]) {
            bestLag = lag;
            break;
        }
    }

    // Parabolic interpolation on the correlation peak for sub-sample lag.
    float lagF = static_cast<float>(bestLag);
    if (bestLag > minLag && bestLag < maxLag) {
        const float a = r[bestLag - 1], b = r[bestLag], c = r[bestLag + 1];
        const float d = 2.0f * (2.0f * b - a - c);
        if (std::fabs(d) > 1e-12f) lagF += (a - c) / d;
    }
    return lagF > 0.0f ? static_cast<float>(sr) / lagF : 0.0f;
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



// ---------------------------------------------------------------- main

int main(int argc, char** argv) {
    std::string verifyRecordPath;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--verify-record" && i + 1 < argc) {
            verifyRecordPath = argv[i + 1];
        }
    }
    if (!verifyRecordPath.empty()) {
        return runVerifyRecord(verifyRecordPath);
    }

    std::string refPath, testPath, jsonPath, recordPath;
    bool align = true;

    std::string fullArgv;
    for (int i = 0; i < argc; ++i) {
        if (i > 0) fullArgv += " ";
        fullArgv += argv[i];
    }

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };

        if      (a == "--ref")      refPath = next();
        else if (a == "--test")     testPath = next();
        else if (a == "--json")     jsonPath = next();
        else if (a == "--record")   recordPath = next();
        else if (a == "--no-align") align = false;
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); printUsage(); return 2; }
    }

    if (refPath.empty() || testPath.empty()) {
        printUsage();
        return 2;
    }

    size_t bytesRef = 0, bytesTest = 0;
    uint64_t hashRef = computeFnv1a64(refPath, bytesRef);
    uint64_t hashTest = computeFnv1a64(testPath, bytesTest);

    if (bytesRef > 0 && bytesTest > 0 && hashRef == hashTest && bytesRef == bytesTest) {
        std::fprintf(stderr, "error: identical input files detected (ref '%s' and test '%s' have identical fnv1a64 hash %s) — refusing comparison\n",
                     refPath.c_str(), testPath.c_str(), hexHash(hashRef).c_str());
        writeRecordFile(recordPath, fullArgv, refPath, bytesRef, hashRef, testPath, bytesTest, hashTest,
                        0.0, 0.0, 0.995, false, false, 0.0, 0.0, 0.0, "REFUSED_IDENTICAL_INPUTS");
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

    float peakRef = 0.0f;
    for (float s : monoRef) peakRef = std::max(peakRef, std::fabs(s));
    float peakTest = 0.0f;
    for (float s : monoTest) peakTest = std::max(peakTest, std::fabs(s));

    const float kMinPeakThresh = 0.005f;
    static constexpr float kSaturationThresh = 0.995f;

    bool refSat = (peakRef >= kSaturationThresh);
    bool testSat = (peakTest >= kSaturationThresh);

    if (peakRef < kMinPeakThresh || peakTest < kMinPeakThresh || refSat || testSat) {
        std::string status = "FAIL";
        if (refSat || testSat) status = "REFUSED_SATURATED";
        else status = "REFUSED_SILENT";
        writeRecordFile(recordPath, fullArgv, refPath, bytesRef, hashRef, testPath, bytesTest, hashTest,
                        peakRef, peakTest, kSaturationThresh, refSat, testSat, 0.0, 0.0,
                        (peakRef > 0 ? (peakTest / peakRef) : 0.0), status);
        if (peakRef < kMinPeakThresh) std::fprintf(stderr, "error: ref peak (%.5f) is below threshold (%.5f) — recording is silent\n", peakRef, kMinPeakThresh);
        else if (peakTest < kMinPeakThresh) std::fprintf(stderr, "error: test peak (%.5f) is below threshold (%.5f) — recording is silent\n", peakTest, kMinPeakThresh);
        else if (refSat) std::fprintf(stderr, "error: ref peak (%.6f) >= saturation threshold (%.3f) — signal is saturated/clipped\n", peakRef, kSaturationThresh);
        else if (testSat) std::fprintf(stderr, "error: test peak (%.6f) >= saturation threshold (%.3f) — signal is saturated/clipped\n", peakTest, kSaturationThresh);
        return 2;
    }

    const float normGainRef = (peakRef > 0.0f) ? (1.0f / peakRef) : 1.0f;
    const float normGainTest = (peakTest > 0.0f) ? (1.0f / peakTest) : 1.0f;
    const float normGainRefDb = 20.0f * std::log10(normGainRef);
    const float normGainTestDb = 20.0f * std::log10(normGainTest);

    double ratio = (peakRef > 0.0f) ? (static_cast<double>(peakTest) / peakRef) : 0.0;
    writeRecordFile(recordPath, fullArgv, refPath, bytesRef, hashRef, testPath, bytesTest, hashTest,
                    peakRef, peakTest, kSaturationThresh, false, false, normGainRefDb, normGainTestDb,
                    ratio, "PASS");

    for (float& s : monoRef) s *= normGainRef;
    for (float& s : monoTest) s *= normGainTest;

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

    const float fundRef  = findFundamentalAcf(refWin,  sr);
    const float fundTest = findFundamentalAcf(testWin, sr);
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
    std::printf("ref:   %s  (%u ch, %u Hz, onset %.1f ms, peak %.5f, norm %+0.2f dB)\n",
                refPath.c_str(), chRef, srRef, 1000.0 * onsetRef / sr, peakRef, normGainRefDb);
    std::printf("test:  %s  (%u ch, %u Hz, onset %.1f ms, peak %.5f, norm %+0.2f dB)\n",
                testPath.c_str(), chTest, srTest, 1000.0 * onsetTest / sr, peakTest, normGainTestDb);
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
        std::fprintf(f, "  \"peak_ref_pre_norm\": %.5f,\n", peakRef);
        std::fprintf(f, "  \"peak_test_pre_norm\": %.5f,\n", peakTest);
        std::fprintf(f, "  \"norm_gain_ref_db\": %.4f,\n", normGainRefDb);
        std::fprintf(f, "  \"norm_gain_test_db\": %.4f,\n", normGainTestDb);
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
