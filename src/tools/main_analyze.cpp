// ===========================================================================
// src/tools/main_analyze.cpp
//
// Audio analysis tool. Reads a rendered WAV, prints objective measurements,
// and exits non-zero if any hard check fails.
//
//   m8_analyze <file.wav>
//   m8_analyze <file.wav> --events <file_events.csv>   (per-note pitch/centroid/attack)
//   m8_analyze <file.wav> --json <report.json>         (machine-readable report)
//   m8_analyze --diff <a.wav> <b.wav>                  (sample-by-sample comparison)
//
// Links m8_engine only. No SDL.
// ===========================================================================

#include "analysis/AudioMetrics.h"

#define DR_WAV_IMPLEMENTATION
#include "engine/dr_wav.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <limits>

static void printUsage() {
    std::fprintf(stderr,
        "usage: m8_analyze <file.wav> [--events <events.csv>] [--json <report.json>]\n"
        "       m8_analyze --diff <a.wav> <b.wav>\n");
}

// Hard checks from the spec:
//   peak < 1.0
//   clipped == 0
//   non-finite == 0
//   |DC| < 0.005 overall AND < 0.01 per 1-second window
//   crest > 6 dB
static bool checkHard(const m8::analysis::Metrics& m, const char* path) {
    bool ok = true;

    if (m.peak >= 1.0f) {
        std::fprintf(stderr, "FAIL peak %.6f >= 1.0 (%s)\n", m.peak, path);
        ok = false;
    }
    if (m.clipped != 0) {
        std::fprintf(stderr, "FAIL clipped %d != 0 (%s)\n", m.clipped, path);
        ok = false;
    }
    if (m.nonFinite != 0) {
        std::fprintf(stderr, "FAIL non-finite %d != 0 (%s)\n", m.nonFinite, path);
        ok = false;
    }
    if (std::fabs(m.dcL) >= 0.005f) {
        std::fprintf(stderr, "FAIL |DC| %.6f >= 0.005 L (%s)\n", std::fabs(m.dcL), path);
        ok = false;
    }
    if (std::fabs(m.dcR) >= 0.005f) {
        std::fprintf(stderr, "FAIL |DC| %.6f >= 0.005 R (%s)\n", std::fabs(m.dcR), path);
        ok = false;
    }
    if (m.dcWorstWindow >= 0.01f) {
        std::fprintf(stderr, "FAIL DC worst-window %.6f >= 0.01 (%s)\n", m.dcWorstWindow, path);
        ok = false;
    }
    if (m.crestDb <= 6.0f) {
        std::fprintf(stderr, "FAIL crest %.2f dB <= 6 dB (%s)\n", m.crestDb, path);
        ok = false;
    }

    return ok;
}

// ─── --events mode: per-note pitch / centroid / attack analysis ─────────────
//
// Reads the events CSV written by m8_render (writeEvents in main_render.cpp):
//   sample_time,seconds,type,track,song_row,chain_row,phrase_row,instrument,frequency,volume
// For each NOTE_ON, the analysis window is [this note's sample_time, the next
// NOTE_ON's sample_time on the SAME track), matching the spec exactly. Since
// the WAV is the full mix (not a per-track stem), this is only meaningful
// when the note's track is the dominant/only source in that window — e.g. a
// solo render or a probe song, same assumption the [audio] A3/A4 tests make.

struct NoteEvent {
    uint64_t sampleTime = 0;
    int      track = -1;
    float    frequency = 0.0f;
};

static std::vector<NoteEvent> parseNoteOns(const std::string& path) {
    std::vector<NoteEvent> events;
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return events;

    char line[512];
    bool header = true;
    while (std::fgets(line, sizeof(line), f)) {
        if (header) { header = false; continue; }

        char* fields[10] = {};
        int n = 0;
        char* tok = std::strtok(line, ",\r\n");
        while (tok && n < 10) { fields[n++] = tok; tok = std::strtok(nullptr, ",\r\n"); }
        if (n < 9) continue;                          // malformed row
        if (std::strcmp(fields[2], "NOTE_ON") != 0) continue;

        NoteEvent e;
        e.sampleTime = std::strtoull(fields[0], nullptr, 10);
        e.track      = std::atoi(fields[3]);
        e.frequency  = static_cast<float>(std::atof(fields[8]));
        if (e.frequency > 0.0f) events.push_back(e);
    }
    std::fclose(f);
    return events;
}

struct NoteMetrics {
    uint64_t sampleTime = 0;
    int      track = -1;
    float    expectedHz = 0.0f;
    uint64_t windowSamples = 0;

    bool  pitchValid = false;
    float measuredHz = 0.0f;
    float pitchCents = 0.0f;

    bool  centroidValid = false;
    float centroidStartHz = 0.0f;
    float centroidEndHz = 0.0f;

    bool     attackValid = false;
    uint64_t attackSamples = 0;
};

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

static std::vector<NoteMetrics> analyzeNotes(const std::vector<NoteEvent>& notes,
                                              const std::vector<float>& mono, int sr) {
    // Group by track (order within a track is already chronological — the
    // CSV is written in sample_time order), so "next NOTE_ON on the same
    // track" is just "the next entry in that track's group".
    std::vector<std::vector<size_t>> byTrack(8);
    for (size_t i = 0; i < notes.size(); ++i) {
        int t = notes[i].track;
        if (t >= 0 && t < 8) byTrack[t].push_back(i);
    }

    std::vector<NoteMetrics> out;
    for (int t = 0; t < 8; ++t) {
        const auto& idxs = byTrack[t];
        for (size_t k = 0; k < idxs.size(); ++k) {
            const auto& ev = notes[idxs[k]];
            uint64_t windowStart = ev.sampleTime;
            uint64_t windowEnd = (k + 1 < idxs.size()) ? notes[idxs[k + 1]].sampleTime
                                                        : static_cast<uint64_t>(mono.size());
            windowEnd = std::min<uint64_t>(windowEnd, mono.size());
            if (windowEnd <= windowStart) continue;

            NoteMetrics nm{};
            nm.sampleTime = windowStart;
            nm.track = t;
            nm.expectedHz = ev.frequency;
            nm.windowSamples = windowEnd - windowStart;

            const uint64_t winLen = nm.windowSamples;
            const float* winPtr = mono.data() + windowStart;

            // Pitch: skip the attack transient, measure over the sustain
            // (capped at 1s so a very long final-note window doesn't force
            // a huge FFT for no extra accuracy).
            const size_t pitchSkip = static_cast<size_t>(std::min<uint64_t>(500, winLen / 4));
            const size_t pitchLen = (winLen > pitchSkip)
                ? static_cast<size_t>(std::min<uint64_t>(winLen - pitchSkip, 48000)) : 0;
            if (pitchLen >= 256) {
                nm.measuredHz = m8::analysis::pitchHz(winPtr + pitchSkip, pitchLen, sr, ev.frequency);
                if (nm.measuredHz > 0.0f) {
                    nm.pitchCents = 1200.0f * std::log2(nm.measuredHz / ev.frequency);
                    nm.pitchValid = true;
                }
            }

            // Centroid at note start vs note end (does the filter/brightness move).
            const size_t centSkip = static_cast<size_t>(std::min<uint64_t>(200, winLen / 4));
            const size_t centLen = (winLen > centSkip)
                ? static_cast<size_t>(std::min<uint64_t>(4096, winLen - centSkip)) : 0;
            if (centLen >= 64) {
                nm.centroidStartHz = m8::analysis::spectralCentroidHz(winPtr + centSkip, centLen, sr);
                size_t endLen = static_cast<size_t>(std::min<uint64_t>(4096, winLen));
                nm.centroidEndHz = m8::analysis::spectralCentroidHz(winPtr + winLen - endLen, endLen, sr);
                nm.centroidValid = true;
            }

            // Attack time: index of the peak short-window RMS envelope,
            // searched over the window (capped at 2s — an M8 note's attack
            // is always well inside that even for a very long hold).
            const size_t hop = std::max<size_t>(1, static_cast<size_t>(sr) / 1500); // ~0.67 ms
            const uint64_t searchLen = std::min<uint64_t>(winLen, static_cast<uint64_t>(sr) * 2);
            if (searchLen >= hop) {
                float peakEnv = -1.0f;
                size_t peakHop = 0;
                const size_t nHops = static_cast<size_t>(searchLen / hop);
                for (size_t h = 0; h < nHops; ++h) {
                    double sum = 0.0;
                    for (size_t i = 0; i < hop; ++i) {
                        float s = winPtr[h * hop + i];
                        sum += static_cast<double>(s) * s;
                    }
                    float env = static_cast<float>(std::sqrt(sum / hop));
                    if (env > peakEnv) { peakEnv = env; peakHop = h; }
                }
                nm.attackSamples = static_cast<uint64_t>(peakHop) * hop;
                nm.attackValid = true;
            }

            out.push_back(nm);
        }
    }

    // Report chronologically rather than grouped by track.
    std::sort(out.begin(), out.end(),
              [](const NoteMetrics& a, const NoteMetrics& b) { return a.sampleTime < b.sampleTime; });
    return out;
}

static std::string fmtOrNA(bool valid, float v, const char* fmt) {
    if (!valid) return "n/a";
    char buf[32];
    std::snprintf(buf, sizeof(buf), fmt, v);
    return buf;
}

static void printNoteReport(const std::vector<NoteMetrics>& notes, int sr) {
    std::printf("\n--- per-note (%zu notes) ---\n", notes.size());
    std::printf("   time(s)  trk   freq(Hz)  pitch(cents)  centroid_start(Hz)  centroid_end(Hz)  attack(ms)\n");
    for (const auto& n : notes) {
        std::printf("  %8.3f  %3d  %9.1f  %12s  %18s  %16s  %10s\n",
                    static_cast<double>(n.sampleTime) / sr, n.track, n.expectedHz,
                    fmtOrNA(n.pitchValid, n.pitchCents, "%+.1f").c_str(),
                    fmtOrNA(n.centroidValid, n.centroidStartHz, "%.0f").c_str(),
                    fmtOrNA(n.centroidValid, n.centroidEndHz, "%.0f").c_str(),
                    fmtOrNA(n.attackValid, static_cast<float>(n.attackSamples) * 1000.0f / sr, "%.2f").c_str());
    }
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

static void writeJsonReport(const std::string& jsonPath, const std::string& wavPath,
                             unsigned channels, unsigned sr, drwav_uint64 totalFrames,
                             const m8::analysis::Metrics& m, bool ok,
                             const std::string& eventsPath, const std::vector<NoteMetrics>& notes) {
    FILE* f = std::fopen(jsonPath.c_str(), "w");
    if (!f) { std::fprintf(stderr, "error: cannot write %s\n", jsonPath.c_str()); return; }

    std::fprintf(f, "{\n");
    std::fprintf(f, "  \"file\": \"%s\",\n", jsonEscape(wavPath).c_str());
    std::fprintf(f, "  \"channels\": %u,\n", channels);
    std::fprintf(f, "  \"sample_rate\": %u,\n", sr);
    std::fprintf(f, "  \"frames\": %llu,\n", static_cast<unsigned long long>(totalFrames));
    std::fprintf(f, "  \"duration_sec\": %.4f,\n", static_cast<double>(totalFrames) / sr);
    std::fprintf(f, "  \"metrics\": {\n");
    std::fprintf(f, "    \"peak\": %.6f,\n", m.peak);
    std::fprintf(f, "    \"rms\": %.6f,\n", m.rms);
    std::fprintf(f, "    \"crest_db\": %.4f,\n", m.crestDb);
    std::fprintf(f, "    \"clipped\": %d,\n", m.clipped);
    std::fprintf(f, "    \"non_finite\": %d,\n", m.nonFinite);
    std::fprintf(f, "    \"dc_l\": %.6f,\n", m.dcL);
    std::fprintf(f, "    \"dc_r\": %.6f,\n", m.dcR);
    std::fprintf(f, "    \"dc_worst_window\": %.6f,\n", m.dcWorstWindow);
    std::fprintf(f, "    \"mid_rms\": %.6f,\n", m.midRms);
    std::fprintf(f, "    \"side_rms\": %.6f,\n", m.sideRms);
    std::fprintf(f, "    \"correlation\": %.6f,\n", m.correlation);
    std::fprintf(f, "    \"longest_silence_sec\": %.4f\n", m.longestSilenceSec);
    std::fprintf(f, "  },\n");
    std::fprintf(f, "  \"hard_checks\": {\n");
    std::fprintf(f, "    \"peak_ok\": %s,\n", (m.peak < 1.0f) ? "true" : "false");
    std::fprintf(f, "    \"clipped_ok\": %s,\n", (m.clipped == 0) ? "true" : "false");
    std::fprintf(f, "    \"non_finite_ok\": %s,\n", (m.nonFinite == 0) ? "true" : "false");
    std::fprintf(f, "    \"dc_ok\": %s,\n",
                 (std::fabs(m.dcL) < 0.005f && std::fabs(m.dcR) < 0.005f) ? "true" : "false");
    std::fprintf(f, "    \"dc_worst_window_ok\": %s,\n", (m.dcWorstWindow < 0.01f) ? "true" : "false");
    std::fprintf(f, "    \"crest_ok\": %s,\n", (m.crestDb > 6.0f) ? "true" : "false");
    std::fprintf(f, "    \"overall_pass\": %s\n", ok ? "true" : "false");
    std::fprintf(f, "  }");

    if (!eventsPath.empty()) {
        std::fprintf(f, ",\n  \"events_file\": \"%s\",\n", jsonEscape(eventsPath).c_str());
        std::fprintf(f, "  \"notes\": [\n");
        for (size_t i = 0; i < notes.size(); ++i) {
            const auto& n = notes[i];
            std::fprintf(f, "    {\"sample_time\": %llu, \"track\": %d, \"expected_hz\": %.3f",
                         static_cast<unsigned long long>(n.sampleTime), n.track, n.expectedHz);
            if (n.pitchValid)
                std::fprintf(f, ", \"measured_hz\": %.3f, \"pitch_cents\": %.2f", n.measuredHz, n.pitchCents);
            if (n.centroidValid)
                std::fprintf(f, ", \"centroid_start_hz\": %.1f, \"centroid_end_hz\": %.1f",
                             n.centroidStartHz, n.centroidEndHz);
            if (n.attackValid)
                std::fprintf(f, ", \"attack_ms\": %.3f", n.attackSamples * 1000.0 / sr);
            std::fprintf(f, "}%s\n", (i + 1 < notes.size()) ? "," : "");
        }
        std::fprintf(f, "  ]\n");
    } else {
        std::fprintf(f, "\n");
    }
    std::fprintf(f, "}\n");
    std::fclose(f);
    std::printf("\nwrote %s\n", jsonPath.c_str());
}

// ─── --diff mode: sample-by-sample WAV comparison ────────────────────────────

static int runDiff(const std::string& pathA, const std::string& pathB) {
    unsigned int chA = 0, srA = 0;
    drwav_uint64 framesA = 0;
    float* pcmA = drwav_open_file_and_read_pcm_frames_f32(
        pathA.c_str(), &chA, &srA, &framesA, nullptr);
    if (!pcmA) {
        std::fprintf(stderr, "error: cannot read %s\n", pathA.c_str());
        return 2;
    }

    unsigned int chB = 0, srB = 0;
    drwav_uint64 framesB = 0;
    float* pcmB = drwav_open_file_and_read_pcm_frames_f32(
        pathB.c_str(), &chB, &srB, &framesB, nullptr);
    if (!pcmB) {
        std::fprintf(stderr, "error: cannot read %s\n", pathB.c_str());
        drwav_free(pcmA, nullptr);
        return 2;
    }

    std::printf("file A: %s  (%u ch, %u Hz, %llu frames, %.2f s)\n",
                pathA.c_str(), chA, srA,
                static_cast<unsigned long long>(framesA),
                static_cast<double>(framesA) / srA);
    std::printf("file B: %s  (%u ch, %u Hz, %llu frames, %.2f s)\n",
                pathB.c_str(), chB, srB,
                static_cast<unsigned long long>(framesB),
                static_cast<double>(framesB) / srB);

    if (chA != chB) {
        std::fprintf(stderr, "FAIL channel count mismatch: %u vs %u\n", chA, chB);
        drwav_free(pcmA, nullptr);
        drwav_free(pcmB, nullptr);
        return 1;
    }
    if (srA != srB) {
        std::fprintf(stderr, "FAIL sample rate mismatch: %u vs %u\n", srA, srB);
        drwav_free(pcmA, nullptr);
        drwav_free(pcmB, nullptr);
        return 1;
    }

    const drwav_uint64 minFrames = std::min(framesA, framesB);
    const drwav_uint64 maxFrames = std::max(framesA, framesB);
    const size_t totalSamples = static_cast<size_t>(minFrames) * chA;

    float maxAbsDiff = 0.0f;
    size_t firstDiffIdx = totalSamples;  // = no diff found
    for (size_t i = 0; i < totalSamples; ++i) {
        float d = std::fabs(pcmA[i] - pcmB[i]);
        if (d > maxAbsDiff) maxAbsDiff = d;
        if (d > 0.0f && firstDiffIdx == totalSamples) firstDiffIdx = i;
    }

    std::printf("\n--- diff ---\n");
    std::printf("  channels:       %u\n", chA);
    std::printf("  sample rate:    %u\n", srA);
    std::printf("  frames A:       %llu\n", static_cast<unsigned long long>(framesA));
    std::printf("  frames B:       %llu\n", static_cast<unsigned long long>(framesB));
    std::printf("  compared:       %llu frames (%zu samples)\n",
                static_cast<unsigned long long>(minFrames), totalSamples);
    std::printf("  max |A-B|:      %.9f\n", maxAbsDiff);
    if (firstDiffIdx < totalSamples) {
        drwav_uint64 frame = firstDiffIdx / chA;
        unsigned ch = static_cast<unsigned>(firstDiffIdx % chA);
        std::printf("  first diff at:  sample %zu (frame %llu, channel %u)\n",
                    firstDiffIdx, static_cast<unsigned long long>(frame), ch);
        std::printf("  A value:        %.9f\n", pcmA[firstDiffIdx]);
        std::printf("  B value:        %.9f\n", pcmB[firstDiffIdx]);
    } else {
        std::printf("  first diff at:  none (identical)\n");
    }
    if (framesA != framesB) {
        std::printf("  NOTE: files have different frame counts (%llu vs %llu); "
                    "only the first %llu frames were compared.\n",
                    static_cast<unsigned long long>(framesA),
                    static_cast<unsigned long long>(framesB),
                    static_cast<unsigned long long>(minFrames));
    }

    bool match = (maxAbsDiff == 0.0f && framesA == framesB);
    std::printf("\n%s\n", match ? "PASS" : "FAIL");

    drwav_free(pcmA, nullptr);
    drwav_free(pcmB, nullptr);
    return match ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 2;
    }

    // --diff mode
    if (std::string(argv[1]) == "--diff") {
        if (argc < 4) {
            printUsage();
            return 2;
        }
        return runDiff(argv[2], argv[3]);
    }

    std::string path, eventsPath, jsonPath;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };

        if      (a == "--events") eventsPath = next();
        else if (a == "--json")   jsonPath = next();
        else if (path.empty())    path = a;
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); printUsage(); return 2; }
    }
    if (path.empty()) { printUsage(); return 2; }

    // Read WAV
    unsigned int channels = 0, sr = 0;
    drwav_uint64 totalFrames = 0;
    float* pcm = drwav_open_file_and_read_pcm_frames_f32(
        path.c_str(), &channels, &sr, &totalFrames, nullptr);

    if (!pcm) {
        std::fprintf(stderr, "error: cannot read %s\n", path.c_str());
        return 2;
    }

    if (channels != 2) {
        std::fprintf(stderr, "error: expected 2 channels, got %u\n", channels);
        drwav_free(pcm, nullptr);
        return 2;
    }

    std::printf("file: %s\n", path.c_str());
    std::printf("  channels: %u  sample rate: %u  frames: %llu  duration: %.2f s\n",
                channels, sr,
                static_cast<unsigned long long>(totalFrames),
                static_cast<double>(totalFrames) / sr);

    // Analyse
    m8::analysis::Metrics m = m8::analysis::analyze(pcm, static_cast<size_t>(totalFrames), static_cast<int>(sr));

    // Print results
    std::printf("\n--- metrics ---\n");
    std::printf("  peak:          %.6f\n", m.peak);
    std::printf("  rms:           %.6f\n", m.rms);
    std::printf("  crest:         %.2f dB\n", m.crestDb);
    std::printf("  clipped:       %d\n", m.clipped);
    std::printf("  non-finite:    %d\n", m.nonFinite);
    std::printf("  DC L:          %.6f\n", m.dcL);
    std::printf("  DC R:          %.6f\n", m.dcR);
    std::printf("  DC worst-1s:   %.6f\n", m.dcWorstWindow);
    std::printf("  mid RMS:       %.6f\n", m.midRms);
    std::printf("  side RMS:      %.6f\n", m.sideRms);
    std::printf("  L/R corr:      %.4f\n", m.correlation);
    std::printf("  longest sil:   %.3f s\n", m.longestSilenceSec);

    std::vector<NoteMetrics> notes;
    if (!eventsPath.empty()) {
        auto noteOns = parseNoteOns(eventsPath);
        if (noteOns.empty()) {
            std::fprintf(stderr, "warning: no NOTE_ON rows found in %s\n", eventsPath.c_str());
        } else {
            std::vector<float> mono = downmix(pcm, static_cast<size_t>(totalFrames), channels);
            notes = analyzeNotes(noteOns, mono, static_cast<int>(sr));
            printNoteReport(notes, static_cast<int>(sr));
        }
    }

    bool ok = checkHard(m, path.c_str());

    if (!jsonPath.empty()) {
        writeJsonReport(jsonPath, path, channels, sr, totalFrames, m, ok, eventsPath, notes);
    }

    if (pcm) drwav_free(pcm, nullptr);

    std::printf("\n%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
