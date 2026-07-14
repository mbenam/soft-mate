// ===========================================================================
// src/tools/main_render.cpp
//
// Offline renderer. Drives the engine with no audio device and writes WAVs +
// an event log, so the output can be analysed rather than guessed at.
//
//   m8_render                          -> renders everything (see --batch)
//   m8_render --song --seconds 40      -> the whole demo song
//   m8_render --phrase 0x0C --track 3  -> one phrase, looped, on one track
//   m8_render --solo 3                 -> the song with only track 3 audible
//   m8_render --batch                  -> song + 8 solo tracks + every phrase
//
// Every render also writes a CSV of the EngineEvent ring: every note-on,
// note-off and tick with its absolute sample time. That is the ground truth to
// check the audio against.
//
// Links m8_engine only. No SDL.
// ===========================================================================

#include "engine/Engine.h"
#include "engine/EngineEvents.h"
#include "engine/CommandRing.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>

using namespace m8::engine;

// ---------------------------------------------------------------- WAV output

static void writeWav(const std::string& path, const std::vector<float>& interleaved,
                     int channels, int sampleRate) {
    const uint32_t nFrames  = static_cast<uint32_t>(interleaved.size() / channels);
    const uint32_t dataSize = nFrames * channels * 2;          // 16-bit
    const uint32_t riffSize = 36 + dataSize;

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::printf("  ! cannot open %s\n", path.c_str()); return; }

    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };

    std::fwrite("RIFF", 1, 4, f);  u32(riffSize);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);  u32(16);
    u16(1);                                        // PCM
    u16(static_cast<uint16_t>(channels));
    u32(static_cast<uint32_t>(sampleRate));
    u32(static_cast<uint32_t>(sampleRate * channels * 2));   // byte rate
    u16(static_cast<uint16_t>(channels * 2));                // block align
    u16(16);                                       // bits
    std::fwrite("data", 1, 4, f);  u32(dataSize);

    for (float s : interleaved) {
        s = std::max(-1.0f, std::min(1.0f, s));
        int16_t v = static_cast<int16_t>(s * 32767.0f);
        std::fwrite(&v, 2, 1, f);
    }
    std::fclose(f);

    std::printf("  wrote %-28s  %6.2f s\n", path.c_str(),
                static_cast<double>(nFrames) / sampleRate);
}

// ---------------------------------------------------------------- a render

struct Render {
    std::vector<float>       audio;    // interleaved stereo
    std::vector<EngineEvent> events;
};

// mode: 1 = PHRASE, 2 = CHAIN, 3 = SONG
static Render renderOnce(double seconds, int mode, int targetId, int track, int startRow,
                          const int* soloTrack = nullptr, int chunk = 512) {
    CommandRing<EngineCommand, 1024> ring;
    auto enginePtr = std::make_unique<Engine>(ring);
    Engine& engine = *enginePtr;
    engine.loadDemoSong();

    if (soloTrack) {
        auto& mix = engine.getStateForInit().mixer;
        for (int t = 0; t < 8; ++t)
            if (t != *soloTrack) mix.track_vol[t] = 0;
    }

    EngineCommand cmd{};
    cmd.type     = CommandType::PLAY_START;
    cmd.value    = mode;
    cmd.targetId = static_cast<int16_t>(targetId);
    cmd.col      = static_cast<int8_t>(track);
    cmd.row      = static_cast<int8_t>(startRow);
    ring.push(cmd);

    const int total = static_cast<int>(seconds * kSampleRate);
    Render r;
    r.audio.reserve(static_cast<size_t>(total) * 2);

    std::vector<float> buf(static_cast<size_t>(chunk) * 2);
    int done = 0;
    while (done < total) {
        const int n = std::min(chunk, total - done);
        engine.render(buf.data(), n);
        r.audio.insert(r.audio.end(), buf.begin(), buf.begin() + n * 2);

        EngineEvent ev;
        while (engine.getEventRing().pop(ev)) r.events.push_back(ev);

        done += n;
    }
    return r;
}

static void writeEvents(const std::string& path, const Render& r) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return;
    std::fprintf(f, "sample_time,seconds,type,track,song_row,chain_row,phrase_row,"
                    "instrument,frequency,volume\n");
    for (const auto& e : r.events) {
        const char* t = (e.type == EventType::NOTE_ON)  ? "NOTE_ON"
                      : (e.type == EventType::NOTE_OFF) ? "NOTE_OFF"
                      : (e.type == EventType::TICK)     ? "TICK" : "ROW";
        std::fprintf(f, "%llu,%.6f,%s,%u,%u,%u,%u,%u,%.3f,%.4f\n",
                     static_cast<unsigned long long>(e.sampleTime),
                     static_cast<double>(e.sampleTime) / kSampleRate,
                     t, e.track, e.songRow, e.chainRow, e.phraseRow,
                     e.instrument, e.frequency, e.volume);
    }
    std::fclose(f);
    std::printf("  wrote %-28s  %zu events\n", path.c_str(), r.events.size());
}

// ---------------------------------------------------------------- reporting

static void summarise(const Render& r) {
    float peak = 0.0f;
    double sumSq = 0.0;
    int clipped = 0, bad = 0;
    for (float s : r.audio) {
        if (!std::isfinite(s)) { ++bad; continue; }
        const float a = std::fabs(s);
        peak = std::max(peak, a);
        sumSq += static_cast<double>(s) * s;
        if (a >= 0.999f) ++clipped;
    }
    const double rms = std::sqrt(sumSq / std::max<size_t>(1, r.audio.size()));

    int noteOns[8] = {0};
    for (const auto& e : r.events)
        if (e.type == EventType::NOTE_ON && e.track < 8) ++noteOns[e.track];

    std::printf("  peak %.3f  rms %.4f  clipped %d  non-finite %d\n", peak, rms, clipped, bad);
    std::printf("  note-ons per track: ");
    for (int t = 0; t < 8; ++t) std::printf("%d:%-4d ", t, noteOns[t]);
    std::printf("\n");
}

// ---------------------------------------------------------------- main

int main(int argc, char** argv) {
    double seconds = 40.0;
    std::string out = "render";
    int mode = 3, targetId = 0, track = 0, startRow = 0;
    int solo = -1;
    bool batch = (argc == 1);

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        auto num  = [&]() -> int { return static_cast<int>(std::strtol(next().c_str(), nullptr, 0)); };

        if      (a == "--seconds") seconds = std::atof(next().c_str());
        else if (a == "--out")     out = next();
        else if (a == "--song")  { mode = 3; targetId = 0; }
        else if (a == "--chain") { mode = 2; targetId = num(); }
        else if (a == "--phrase"){ mode = 1; targetId = num(); }
        else if (a == "--track")   track = num();
        else if (a == "--row")     startRow = num();
        else if (a == "--solo")  { solo = num(); mode = 3; }
        else if (a == "--batch")   batch = true;
        else { std::printf("unknown arg: %s\n", a.c_str()); return 1; }
    }

    if (!batch) {
        std::printf("rendering...\n");
        Render r = renderOnce(seconds, mode, targetId, track, startRow,
                              solo >= 0 ? &solo : nullptr);
        writeWav(out + ".wav", r.audio, 2, static_cast<int>(kSampleRate));
        writeEvents(out + "_events.csv", r);
        summarise(r);
        return 0;
    }

    // ---------------- batch ----------------
    std::printf("BATCH RENDER\n\n");

    std::printf("[song] full demo, %.0f s\n", seconds);
    {
        Render r = renderOnce(seconds, 3, 0, 0, 0);
        writeWav("song.wav", r.audio, 2, static_cast<int>(kSampleRate));
        writeEvents("song_events.csv", r);
        summarise(r);
    }
    std::printf("\n");

    static const char* names[8] = { "kick", "snare", "hat", "bass",
                                    "pad",  "arp",   "lead", "perc" };
    for (int t = 0; t < 8; ++t) {
        std::printf("[solo %d] %s\n", t, names[t]);
        Render r = renderOnce(seconds, 3, 0, 0, 0, &t);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "solo_%d_%s.wav", t, names[t]);
        writeWav(buf, r.audio, 2, static_cast<int>(kSampleRate));
        summarise(r);
    }
    std::printf("\n");

    // Every non-empty phrase, looped for 8 seconds on its own track.
    {
        CommandRing<EngineCommand, 1024> ring;
        auto probePtr = std::make_unique<Engine>(ring);
        Engine& probe = *probePtr;
        probe.loadDemoSong();
        const Sequencer& seq = probe.getSequencer();

        for (int p = 0; p < 64; ++p) {
            int firstInst = -1;
            for (int r = 0; r < 16; ++r)
                if (seq.phrases[p][r].note != NOTE_EMPTY) {
                    firstInst = seq.phrases[p][r].instr;
                    break;
                }
            if (firstInst < 0) continue;

            const int t = std::min(firstInst, 7);   // route it to its own track
            std::printf("[phrase %02X] inst %02X -> track %d\n", p, firstInst, t);
            Render r = renderOnce(8.0, 1, p, t, 0);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "phrase_%02X.wav", p);
            writeWav(buf, r.audio, 2, static_cast<int>(kSampleRate));
        }
    }

    std::printf("\ndone.\n");
    return 0;
}
