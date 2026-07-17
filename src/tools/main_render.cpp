// ===========================================================================
// src/tools/main_render.cpp
//
// Offline renderer. Drives the engine with no audio device and writes WAVs +
// an event log, so the output can be analysed rather than guessed at.
//
//   m8_render                                    -> renders everything (see --batch)
//   m8_render --song --seconds 40                -> the whole demo song
//   m8_render --phrase 0x0C --track 3            -> one phrase, looped, on one track
//   m8_render --solo 3                           -> the song with only track 3 audible
//   m8_render --batch                            -> song + 8 solo tracks + every phrase
//   m8_render --load SONG.m8s --seconds 60       -> load a real .m8s song
//   m8_render --load SONG.m8s --sample-root /sd  -> resolve samples from SD card
//   m8_render --load SONG.m8s --batch            -> full batch render of a real song
//   m8_render --load probe.m8s --note C-4 --instrument 0 --seconds 2.5 --out mine
//                -> render one instrument in isolation (probe's song row 0)
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
#include "engine/SamplePool.h"
#include "io/SongIO.h"

#define DR_WAV_IMPLEMENTATION
#include "engine/dr_wav.h"

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

// ---------------------------------------------------------------- decoded sample for pre-loading

struct DecodedSample {
    std::string path;         // relative path from the song
    std::string resolved;     // absolute path on disk
    SampleData data{};        // decoded WAV (owned)
    int instrumentIndex = -1; // which instrument uses this
    bool loaded = false;
};

// ---------------------------------------------------------------- pre-load song + samples

struct LoadedSong {
    m8::io::LoadResult loadResult;
    std::vector<DecodedSample> samples;  // deduplicated
    bool ok = false;
};

static LoadedSong loadSongForRender(const std::string& path, const std::string& sampleRoot) {
    LoadedSong ls;
    ls.loadResult = m8::io::loadSong(path, sampleRoot);
    if (!ls.loadResult.ok) return ls;

    // Build deduplicated sample list with resolved paths
    std::vector<std::pair<std::string,int>> relPaths; // (path, instIndex)
    const auto& insts = ls.loadResult.state.instruments;
    for (int i = 0; i < 128; ++i) {
        if (insts[i].type == InstType::INST_SAMPLER) {
            const char* p = insts[i].sampler.samplePath;
            if (p[0] != '\0') relPaths.push_back({p, i});
        }
    }
    // Deduplicate by path
    std::sort(relPaths.begin(), relPaths.end());
    relPaths.erase(std::unique(relPaths.begin(), relPaths.end(),
                   [](const auto& a, const auto& b){ return a.first == b.first; }),
                   relPaths.end());

    for (auto& [rel, idx] : relPaths) {
        DecodedSample ds;
        ds.path = rel;
        ds.resolved = sampleRoot.empty() ? rel : sampleRoot + "/" + rel;
        ds.instrumentIndex = idx;

        // Try to load the WAV
        unsigned int ch = 0, sr = 0;
        drwav_uint64 frames = 0;
        float* pcm = drwav_open_file_and_read_pcm_frames_f32(
            ds.resolved.c_str(), &ch, &sr, &frames, NULL);
        if (pcm) {
            ds.data.data = pcm;
            ds.data.frames = static_cast<uint32_t>(frames);
            ds.data.channels = ch;
            ds.data.sampleRate = sr;
            std::strncpy(ds.data.path, ds.path.c_str(), sizeof(ds.data.path) - 1);
            ds.loaded = true;
        } else {
            // Try CWD as fallback
            pcm = drwav_open_file_and_read_pcm_frames_f32(
                rel.c_str(), &ch, &sr, &frames, NULL);
            if (pcm) {
                ds.resolved = rel;
                ds.data.data = pcm;
                ds.data.frames = static_cast<uint32_t>(frames);
                ds.data.channels = ch;
                ds.data.sampleRate = sr;
                std::strncpy(ds.data.path, ds.path.c_str(), sizeof(ds.data.path) - 1);
                ds.loaded = true;
            }
        }
        ls.samples.push_back(std::move(ds));
    }

    ls.ok = true;
    return ls;
}

// Load a song (demo or from file) into a fresh engine
static void setupEngine(Engine& engine, CommandRing<EngineCommand, 1024>& ring,
                        const LoadedSong* loadedSong) {
    if (loadedSong && loadedSong->ok) {
        // Push combined payload via LOAD_SONG
        const auto& lr = loadedSong->loadResult;
        auto* buf = new uint8_t[sizeof(Sequencer) + sizeof(EngineState)];
        *reinterpret_cast<Sequencer*>(buf) = lr.sequencer;
        *reinterpret_cast<EngineState*>(buf + sizeof(Sequencer)) = lr.state;
        EngineCommand cmd{};
        cmd.type = CommandType::LOAD_SONG;
        cmd.u.song.data = buf;
        ring.push(cmd);

        // Push LOAD_SAMPLE for each decoded sample
        for (const auto& ds : loadedSong->samples) {
            if (!ds.loaded) continue;
            EngineCommand sampCmd{};
            sampCmd.type = CommandType::LOAD_SAMPLE;
            sampCmd.targetId = ds.instrumentIndex;
            sampCmd.u.sample = ds.data;
            ring.push(sampCmd);
        }
        // Commands are processed on the first sample of the main render.
        // No warmup needed — PLAY_START resets m_tickPhase which would
        // cause a spurious tick at the warmup chunk boundary.
    } else {
        engine.loadDemoSong();
    }
}

// Print per-track instrument info. Must be called AFTER the engine has
// processed any queued LOAD_SONG command (see the warm-up render at the call
// site) — reading state before that shows 128 default SAMPLER slots instead
// of what the loaded song actually contains.
static void printTrackInfo(Engine& engine) {
    const auto& state = engine.getStateForInit();

    std::printf("  track  instrument  type\n");
    for (int i = 0; i < 128; ++i) {
        const auto& inst = state.instruments[i];

        char name[14];
        std::memcpy(name, inst.name, 13);
        name[13] = '\0';
        // trim trailing spaces
        for (int c = 12; c >= 0; --c) { if (name[c] == ' ') name[c] = '\0'; else break; }

        // A genuinely-unused slot is INST_NONE with the untouched default
        // name. Unimplemented types loaded from a song file (FM/Hyper/Wav/
        // MIDIOut/External) are ALSO InstType::INST_NONE, but SongIO gives
        // them the file's real name — that's how we tell "empty" apart from
        // "present but silent", instead of hiding the latter entirely.
        const bool isDefaultName = (std::strncmp(inst.name, "------------", 12) == 0);
        if (inst.type == InstType::INST_NONE && isDefaultName) continue;

        const char* typeName = "UNKNOWN";
        if (inst.type == InstType::INST_SAMPLER) typeName = "SAMPLER";
        else if (inst.type == InstType::INST_MACROSYN) typeName = "MACROSYN";
        else if (inst.type == InstType::INST_HYPERSYN) typeName = "HYPERSYN";
        else if (inst.type == InstType::INST_MIDI) typeName = "MIDI";
        else if (inst.type == InstType::INST_NONE) typeName = "NONE (unimplemented — silent)";

        if (inst.type == InstType::INST_SAMPLER) {
            std::printf("  inst %02X  %-12s  %s  sample: %s\n", i, name, typeName,
                        inst.sampler.samplePath[0] ? inst.sampler.samplePath : "(none)");
        } else {
            std::printf("  inst %02X  %-12s  %s\n", i, name, typeName);
        }
    }
}

// ---------------------------------------------------------------- a render

struct Render {
    std::vector<float>       audio;    // interleaved stereo
    std::vector<EngineEvent> events;
};

// mode: 1 = PHRASE, 2 = CHAIN, 3 = SONG
static Render renderOnce(double seconds, int mode, int targetId, int track, int startRow,
                          const int* soloTrack = nullptr, const LoadedSong* loadedSong = nullptr,
                          int chunk = 512) {
    CommandRing<EngineCommand, 1024> ring;
    auto enginePtr = std::make_unique<Engine>(ring);
    Engine& engine = *enginePtr;

    setupEngine(engine, ring, loadedSong);

    // Mute non-solo tracks via a queued command rather than mutating engine
    // state directly. setupEngine() may have already queued a LOAD_SONG
    // command (when loadedSong is set); LOAD_SONG overwrites the entire
    // mixer when processed, so a direct mixer edit here would be silently
    // clobbered before the first render() call. Queuing MIX_TRK_VOL commands
    // guarantees they run after LOAD_SONG since the ring is FIFO.
    if (soloTrack) {
        for (int t = 0; t < 8; ++t) {
            if (t == *soloTrack) continue;
            EngineCommand mute{};
            mute.type = CommandType::UPDATE_PARAM;
            mute.paramId = ParamID::MIX_TRK_VOL;
            mute.row = t;
            mute.value = 0;
            ring.push(mute);
        }
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
    std::string loadPath;
    std::string sampleRoot;
    std::string noteStr;
    int instrumentIsolate = -1;
    int mode = 3, targetId = 0, track = 0, startRow = 0;
    int solo = -1;
    bool batch = (argc == 1);

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        auto num  = [&]() -> int { return static_cast<int>(std::strtol(next().c_str(), nullptr, 0)); };

        if      (a == "--seconds") seconds = std::atof(next().c_str());
        else if (a == "--out")     out = next();
        else if (a == "--load")  { loadPath = next(); batch = false; }
        else if (a == "--sample-root") sampleRoot = next();
        else if (a == "--song")  { mode = 3; targetId = 0; }
        else if (a == "--chain") { mode = 2; targetId = num(); }
        else if (a == "--phrase"){ mode = 1; targetId = num(); }
        else if (a == "--track")   track = num();
        else if (a == "--row")     startRow = num();
        else if (a == "--solo")  { solo = num(); mode = 3; }
        else if (a == "--note")      noteStr = next();
        else if (a == "--instrument") instrumentIsolate = num();
        else if (a == "--batch")   batch = true;
        else { std::printf("unknown arg: %s\n", a.c_str()); return 1; }
    }

    // Load song if --load specified
    LoadedSong loadedSong;
    if (!loadPath.empty()) {
        std::printf("loading %s ...\n", loadPath.c_str());
        loadedSong = loadSongForRender(loadPath, sampleRoot);
        if (!loadedSong.ok) {
            std::fprintf(stderr, "error: %s\n", loadedSong.loadResult.error.c_str());
            return 1;
        }

        // Report missing samples
        if (!loadedSong.loadResult.missing.empty()) {
            std::fprintf(stderr, "missing samples:\n");
            for (const auto& s : loadedSong.loadResult.missing)
                std::fprintf(stderr, "  %s\n", s.c_str());
        }

        // Print loaded sample info
        int loaded = 0, failed = 0;
        for (const auto& ds : loadedSong.samples) {
            if (ds.loaded) ++loaded; else ++failed;
        }
        std::printf("  samples: %d loaded, %d missing\n", loaded, failed);

        // Print track/instrument info
        {
            CommandRing<EngineCommand, 1024> ring;
            auto enginePtr = std::make_unique<Engine>(ring);
            Engine& engine = *enginePtr;
            setupEngine(engine, ring, &loadedSong);
            // Drain the queued LOAD_SONG/LOAD_SAMPLE commands before reading
            // state back out. playMode is still NONE at this point (LOAD_SONG
            // doesn't change it and PLAY_START hasn't been pushed), so this
            // is a pure command-processing pass with no playback side effects.
            float warmup[16] = {};
            engine.render(warmup, 8);
            printTrackInfo(engine);
        }
        std::printf("\n");
    }

    const LoadedSong* lsPtr = loadedSong.ok ? &loadedSong : nullptr;

    // --note/--instrument isolation: play song row 0 (probe layout), solo the instrument's track
    if (!noteStr.empty() && instrumentIsolate >= 0) {
        // The probe puts the note on track 0. Solo track 0 and play song mode from row 0.
        solo = 0;
        mode = 3; targetId = 0; track = 0; startRow = 0;
        std::printf("isolation: instrument %d, note %s -> solo track 0, song row 0\n",
                   instrumentIsolate, noteStr.c_str());
    }

    if (!batch) {
        std::printf("rendering...\n");
        Render r = renderOnce(seconds, mode, targetId, track, startRow,
                              solo >= 0 ? &solo : nullptr, lsPtr);
        writeWav(out + ".wav", r.audio, 2, static_cast<int>(kSampleRate));
        writeEvents(out + "_events.csv", r);
        summarise(r);
        return 0;
    }

    // ---------------- batch ----------------
    std::printf("BATCH RENDER\n\n");

    std::printf("[song] full song, %.0f s\n", seconds);
    {
        Render r = renderOnce(seconds, 3, 0, 0, 0, nullptr, lsPtr);
        writeWav("song.wav", r.audio, 2, static_cast<int>(kSampleRate));
        writeEvents("song_events.csv", r);
        summarise(r);
    }
    std::printf("\n");

    for (int t = 0; t < 8; ++t) {
        std::printf("[solo %d]\n", t);
        Render r = renderOnce(seconds, 3, 0, 0, 0, &t, lsPtr);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "solo_%d.wav", t);
        writeWav(buf, r.audio, 2, static_cast<int>(kSampleRate));
        summarise(r);
    }
    std::printf("\n");

    // Every non-empty phrase, looped for 8 seconds on its own track.
    {
        CommandRing<EngineCommand, 1024> ring;
        auto probePtr = std::make_unique<Engine>(ring);
        Engine& probe = *probePtr;
        setupEngine(probe, ring, lsPtr);
        const Sequencer& seq = probe.getSequencer();

        for (int p = 0; p < 256; ++p) {
            int firstInst = -1;
            for (int r = 0; r < 16; ++r)
                if (seq.phrases[p][r].note != NOTE_EMPTY) {
                    firstInst = seq.phrases[p][r].instr;
                    break;
                }
            if (firstInst < 0) continue;

            const int t = std::min(firstInst, 7);
            std::printf("[phrase %02X] inst %02X -> track %d\n", p, firstInst, t);
            Render r = renderOnce(8.0, 1, p, t, 0, nullptr, lsPtr);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "phrase_%02X.wav", p);
            writeWav(buf, r.audio, 2, static_cast<int>(kSampleRate));
        }
    }

    std::printf("\ndone.\n");
    return 0;
}
