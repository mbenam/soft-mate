#include <catch2/catch_test_macros.hpp>
#include "io/SongIO.h"
#include "engine/Engine.h"
#include "engine/CommandRing.h"
#include "song.hpp"
#include "synths.hpp"
#include "instruments.hpp"
#include "reader.hpp"
#include "writer.hpp"
#include <cstring>
#include <fstream>
#include <cmath>
#include <memory>
#include <filesystem>

using namespace m8::io;
using namespace m8::engine;

static std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    auto sz = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

static const char* songDir() {
    static std::string dir;
    if (dir.empty()) {
        // Resolve relative to the executable
        dir = std::string(THIRD_PARTY_DIR) + "/m8-files-cxx/examples/songs/";
    }
    return dir.c_str();
}

static std::string songPath(const char* name) {
    return std::string(songDir()) + name;
}

TEST_CASE("L4 V4EMPTY round-trip is byte-identical", "[io]") {
    auto path = songPath("V4EMPTY.m8s");
    auto result = loadSong(path, "");
    REQUIRE(result.ok);
    REQUIRE(result.writable);

    std::string err;
    bool saved = saveSong("V4EMPTY_rt.m8s", result, result.sequencer, result.state, err);
    REQUIRE(saved);
    REQUIRE(err.empty());

    // Compare file sizes
    auto orig = readFile(path);
    auto rt   = readFile("V4EMPTY_rt.m8s");
    REQUIRE(orig.size() == rt.size());

    // Byte-identical
    size_t diffs = 0;
    for (size_t i = 0; i < orig.size(); ++i)
        if (orig[i] != rt[i]) ++diffs;
    REQUIRE(diffs == 0);

    std::remove("V4EMPTY_rt.m8s");
}

TEST_CASE("L4 V4-1EMPTY round-trip is byte-identical", "[io]") {
    auto path = songPath("V4-1EMPTY.m8s");
    auto result = loadSong(path, "");
    REQUIRE(result.ok);
    REQUIRE(result.writable);

    std::string err;
    bool saved = saveSong("V4-1EMPTY_rt.m8s", result, result.sequencer, result.state, err);
    REQUIRE(saved);
    REQUIRE(err.empty());

    auto orig = readFile(path);
    auto rt   = readFile("V4-1EMPTY_rt.m8s");
    REQUIRE(orig.size() == rt.size());

    size_t diffs = 0;
    for (size_t i = 0; i < orig.size(); ++i)
        if (orig[i] != rt[i]) ++diffs;
    REQUIRE(diffs == 0);

    std::remove("V4-1EMPTY_rt.m8s");
}

TEST_CASE("L1 V4EMPTY loads without throw", "[io]") {
    auto path = songPath("V4EMPTY.m8s");
    auto result = loadSong(path, "");
    REQUIRE(result.ok);
    REQUIRE(result.error.empty());

    // Phrases should be empty (all NOTE_EMPTY)
    for (int p = 0; p < 255; ++p)
        for (int r = 0; r < 16; ++r)
            REQUIRE(result.sequencer.phrases[p][r].note == 0xFF);

    // Name should match
    REQUIRE(std::strncmp(result.state.project.name, "V4EMPTY", 7) == 0);
}

TEST_CASE("L2 DEFAULT loads as non-writable", "[io]") {
    auto path = songPath("DEFAULT.m8s");
    auto result = loadSong(path, "");
    REQUIRE(result.ok);
    REQUIRE_FALSE(result.writable);
}

TEST_CASE("L3 TEST-FILE loads with content", "[io]") {
    auto path = songPath("TEST-FILE.m8s");
    auto result = loadSong(path, "");
    REQUIRE(result.ok);

    // At least one phrase has a non-empty step
    bool hasNote = false;
    for (int p = 0; p < 255 && !hasNote; ++p)
        for (int r = 0; r < 16 && !hasNote; ++r)
            if (result.sequencer.phrases[p][r].note != 0xFF) hasNote = true;
    REQUIRE(hasNote);

    // At least one chain is non-empty
    bool hasChain = false;
    for (int c = 0; c < 255 && !hasChain; ++c)
        for (int r = 0; r < 16 && !hasChain; ++r)
            if (result.sequencer.chains[c][r].phrase != 0xFF) hasChain = true;
    REQUIRE(hasChain);
}

TEST_CASE("L10 save refuses pre-4.0", "[io]") {
    auto path = songPath("DEFAULT.m8s");
    auto result = loadSong(path, "");
    REQUIRE(result.ok);
    REQUIRE_FALSE(result.writable);

    std::string err;
    bool saved = saveSong("should_not_exist.m8s", result, result.sequencer, result.state, err);
    REQUIRE_FALSE(saved);
    REQUIRE_FALSE(err.empty());
}

TEST_CASE("L5 Engine round-trip via LOAD_SONG", "[io]") {
    CommandRing<EngineCommand, 1024> ring;
    auto enginePtr = std::make_unique<Engine>(ring);
    auto& engine = *enginePtr;
    engine.getSequencerForInit().clear();

    auto path = songPath("V4EMPTY.m8s");
    auto result = loadSong(path, "");
    REQUIRE(result.ok);

    // Use LoadedSongData, the same type the app pushes (main.cpp). All five
    // LOAD_SONG sites in this file used to hand-roll the payload as
    // `new uint8_t[sizeof(Sequencer) + sizeof(EngineState)]` and then ASSIGN
    // through a reinterpret_cast. That assigns into never-constructed memory,
    // and EngineState owns a std::vector<Instrument>, so operator= read the
    // destination's garbage pointers and freed them -- undefined behaviour that
    // happened to be survivable until the struct's layout moved, at which point
    // the whole [io] tag died with STATUS_HEAP_CORRUPTION (0xC0000374) and no
    // output at all. Do not go back to raw buffers here.
    auto* buf = new LoadedSongData{ result.sequencer, result.state };

    EngineCommand cmd{};
    cmd.type = CommandType::LOAD_SONG;
    cmd.u.song.data = buf;
    ring.push(cmd);

    std::vector<float> tmp(512 * 2);
    engine.render(tmp.data(), 512);

    const auto& engSeq = engine.getSequencer();
    REQUIRE(std::memcmp(&engSeq, &result.sequencer, sizeof(Sequencer)) == 0);
}

TEST_CASE("L8 TEST-FILE renders 30s without crash", "[io]") {
    CommandRing<EngineCommand, 1024> ring;
    auto enginePtr = std::make_unique<Engine>(ring);
    auto& engine = *enginePtr;
    engine.getSequencerForInit().clear();

    auto path = songPath("TEST-FILE.m8s");
    auto result = loadSong(path, "");
    REQUIRE(result.ok);

    auto* buf = new LoadedSongData{ result.sequencer, result.state };

    EngineCommand cmd{};
    cmd.type = CommandType::LOAD_SONG;
    cmd.u.song.data = buf;
    ring.push(cmd);

    EngineCommand play{};
    play.type = CommandType::PLAY_START;
    play.value = 3;
    play.targetId = 0;
    ring.push(play);

    constexpr int kTotal = 30 * 48000;
    std::vector<float> kBuf(512 * 2);
    int done = 0;
    bool bad = false;
    while (done < kTotal) {
        int n = std::min(512, kTotal - done);
        engine.render(kBuf.data(), n);
        for (int i = 0; i < n * 2; ++i) {
            if (!std::isfinite(kBuf[i]) || std::abs(kBuf[i]) > 1.0f) { bad = true; break; }
        }
        if (bad) break;
        done += n;
    }
    REQUIRE_FALSE(bad);
}

TEST_CASE("L6 missing sample loads silently", "[io]") {
    // Load a song, then tamper the sample path to a non-existent file
    auto path = songPath("V4EMPTY.m8s");
    auto result = loadSong(path, "");
    REQUIRE(result.ok);

    // V4EMPTY has no samples — inject a fake path on instrument 0
    auto& inst = result.state.instruments[0];
    inst.type = InstType::INST_SAMPLER;
    std::strncpy(inst.sampler.samplePath, "nonexistent/fake.wav", sizeof(inst.sampler.samplePath) - 1);
    inst.sampler.samplePath[sizeof(inst.sampler.samplePath) - 1] = '\0';

    // loadSong already ran — missing paths come from the library's sample_path fields.
    // Verify that an instrument with a valid sample_path that doesn't exist on disk
    // would be reported. For this test, we just verify the mechanism doesn't crash
    // and the song still loads ok with an empty sample root.
    REQUIRE(result.ok);
    REQUIRE(result.missing.empty()); // V4EMPTY has no sampler instruments
}

TEST_CASE("L7 unimplemented type preserved on save", "[io]") {
    // Load V4EMPTY, replace instrument 0 with FMSynth, save, reload, verify
    auto path = songPath("V4EMPTY.m8s");
    auto data = readFile(path);
    REQUIRE(!data.empty());

    m8::BinaryReader r(data);
    auto song = m8::Song::from_reader(r);

    // Replace instrument 0 with FMSynth
    m8::FMSynth fm;
    fm.number = 0;
    fm.name = "FMTEST";
    fm.transpose = true;
    fm.table_tick = 0xFF;
    fm.algo = m8::FmAlgo::Algo3;
    fm.mod1 = fm.mod2 = fm.mod3 = fm.mod4 = 0;
    fm.synth_params = {};
    fm.synth_params.volume = 0x80;
    fm.synth_params.filter_cutoff = 0xA0;
    fm.synth_params.mods[0] = m8::Mod();
    song.instruments[0] = fm;

    // Save via write_over
    auto out = song.write_over(data);
    REQUIRE(out.size() == data.size());

    // Reload
    m8::BinaryReader r2(out);
    auto loaded = m8::Song::from_reader(r2);

    // FMSynth on instrument 0 should be preserved
    REQUIRE(loaded.instruments[0].index() == 5); // FMSynth is index 5
    auto& fm2 = std::get<m8::FMSynth>(loaded.instruments[0]);
    REQUIRE(fm2.name == "FMTEST");
    REQUIRE(fm2.algo == m8::FmAlgo::Algo3);
    REQUIRE(fm2.synth_params.volume == 0x80);

    // Round-trip should be byte-identical
    m8::BinaryWriter w(std::vector<uint8_t>{});
    loaded.write(w);
    auto bytes2 = w.finish();
    // write_over output should round-trip through write_over again
    auto out2 = loaded.write_over(out);
    size_t diffs = 0;
    for (size_t i = 0; i < out.size(); ++i)
        if (out[i] != out2[i]) ++diffs;
    REQUIRE(diffs == 0);
}

TEST_CASE("L9 bulk load is one command, ring never fills", "[io]") {
    CommandRing<EngineCommand, 1024> ring;
    auto enginePtr = std::make_unique<Engine>(ring);
    auto& engine = *enginePtr;

    auto path = songPath("V4EMPTY.m8s");
    auto result = loadSong(path, "");
    REQUIRE(result.ok);

    auto* buf = new LoadedSongData{ result.sequencer, result.state };

    EngineCommand cmd{};
    cmd.type = CommandType::LOAD_SONG;
    cmd.u.song.data = buf;
    bool pushed = ring.push(cmd);
    REQUIRE(pushed);

    std::vector<float> tmp(512 * 2);
    engine.render(tmp.data(), 512);

    void* gcPtr = nullptr;
    bool found = engine.getSongGcRing().pop(gcPtr);
    REQUIRE(found);
    REQUIRE(gcPtr != nullptr);

    delete[] static_cast<uint8_t*>(gcPtr);

    REQUIRE(std::memcmp(&engine.getSequencer(), &result.sequencer, sizeof(Sequencer)) == 0);
}

TEST_CASE("L9 LOAD_SONG resets effects buffers — in-app render matches fresh engine", "[io]") {
    // Regression test for the effects-buffer contamination bug:
    // LOAD_SONG copied Sequencer/EngineState but left chorus/delay/reverb buffers
    // and DC blockers untouched, causing the in-app render to diverge from m8_render's
    // fresh-engine output.  Fix: reset these on LOAD_SONG (Engine.cpp).

    constexpr int kFrames = 48000;  // 1 second

    // --- Engine A: load demo song first, then load TEST-FILE, render 1s ---
    auto ringA = std::make_unique<CommandRing<EngineCommand, 1024>>();
    auto engineA = std::make_unique<Engine>(*ringA);
    engineA->loadDemoSong();
    {
        std::vector<float> warmup(512 * 2);
        engineA->render(warmup.data(), 512);  // let demo song run briefly
    }

    auto resultA = loadSong(songPath("TEST-FILE.m8s"), "");
    REQUIRE(resultA.ok);
    {
        auto* buf = new LoadedSongData{ resultA.sequencer, resultA.state };
        EngineCommand cmd{};
        cmd.type = CommandType::LOAD_SONG;
        cmd.u.song.data = buf;
        ringA->push(cmd);
    }
    {
        EngineCommand play{};
        play.type = CommandType::PLAY_START;
        play.value = 3;  // SONG
        ringA->push(play);
    }
    std::vector<float> audioA;
    audioA.reserve(kFrames * 2);
    {
        std::vector<float> buf(512 * 2);
        int done = 0;
        while (done < kFrames) {
            int n = std::min(512, kFrames - done);
            engineA->render(buf.data(), n);
            audioA.insert(audioA.end(), buf.begin(), buf.begin() + n * 2);
            done += n;
        }
    }

    // --- Engine B: fresh engine, load TEST-FILE only, render 1s ---
    auto ringB = std::make_unique<CommandRing<EngineCommand, 1024>>();
    auto engineB = std::make_unique<Engine>(*ringB);
    engineB->getSequencerForInit().clear();

    auto resultB = loadSong(songPath("TEST-FILE.m8s"), "");
    REQUIRE(resultB.ok);
    {
        auto* buf = new LoadedSongData{ resultB.sequencer, resultB.state };
        EngineCommand cmd{};
        cmd.type = CommandType::LOAD_SONG;
        cmd.u.song.data = buf;
        ringB->push(cmd);
    }
    {
        EngineCommand play{};
        play.type = CommandType::PLAY_START;
        play.value = 3;  // SONG
        ringB->push(play);
    }
    std::vector<float> audioB;
    audioB.reserve(kFrames * 2);
    {
        std::vector<float> buf(512 * 2);
        int done = 0;
        while (done < kFrames) {
            int n = std::min(512, kFrames - done);
            engineB->render(buf.data(), n);
            audioB.insert(audioB.end(), buf.begin(), buf.begin() + n * 2);
            done += n;
        }
    }

    // --- Compare: should be identical if LOAD_SONG resets effects state ---
    REQUIRE(audioA.size() == audioB.size());
    float maxDiff = 0.0f;
    for (size_t i = 0; i < audioA.size(); ++i) {
        float d = std::fabs(audioA[i] - audioB[i]);
        if (d > maxDiff) maxDiff = d;
    }
    REQUIRE(maxDiff == 0.0f);
}

TEST_CASE("L10 loadSong populates missing for unresolved sample paths", "[io]") {
    // Regression test: loadSong() previously accepted sampleRoot but never checked
    // whether sample paths actually resolved, so LoadResult::missing was always empty.
    // TEST-FILE.m8s references /Samples/Drums/Hits/TR505/bass drum 505.wav which
    // does not exist in the repo's Samples/ directory.

    auto result = loadSong(songPath("TEST-FILE.m8s"), "");
    REQUIRE(result.ok);

    // samplePaths should be populated (the path exists in the file)
    REQUIRE(!result.samplePaths.empty());

    // missing should be populated (the file doesn't exist on disk)
    REQUIRE(!result.missing.empty());

    // The missing path should match the sample path
    REQUIRE(result.missing[0] == result.samplePaths[0]);
}

TEST_CASE("L11 loadSong reports no missing for V4EMPTY (no samples referenced)", "[io]") {
    auto result = loadSong(songPath("V4EMPTY.m8s"), "");
    REQUIRE(result.ok);
    REQUIRE(result.missing.empty());
}

#ifdef _WIN32
#include <windows.h>
static std::string getTestExeDir() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::filesystem::path p(path);
    return p.parent_path().string();
}
#else
static std::string getTestExeDir() {
    return "build/Release";
}
#endif

TEST_CASE("T0 makeprobe round-trip in CI", "[io]") {
    std::string exeDir = getTestExeDir();
    std::string makeprobePath = exeDir + "/m8_makeprobe.exe";
    std::string outPath = "T0_probe.m8s";
    
    // Clean up any stale probe file
    std::filesystem::remove(outPath);
    
    std::string cmd = "\"" + makeprobePath + "\" --type macrosynth --shape 0x00 --timbre 0x40 --color 0x80 --note C-4 --out " + outPath;
    int rc = std::system(cmd.c_str());
    REQUIRE(rc == 0);
    REQUIRE(std::filesystem::exists(outPath));
    
    // Load and verify
    auto result = loadSong(outPath, "");
    REQUIRE(result.ok);
    REQUIRE(result.state.instruments[0].type == InstType::INST_MACROSYN);
    REQUIRE(result.state.instruments[0].macrosyn.shape == 0x00);
    REQUIRE(result.state.instruments[0].macrosyn.timbre == 0x40);
    REQUIRE(result.state.instruments[0].macrosyn.color == 0x80);
    
    // Verify sequence
    REQUIRE(result.sequencer.phrases[0][0].note == 60); // C-4
    REQUIRE(result.sequencer.phrases[0][0].instr == 0);
    
    // Clean up
    std::filesystem::remove(outPath);
}

static void ensureSamplerProbeExists() {
    if (!std::filesystem::exists("hwtest_out/probes/probe_sampler.m8s")) {
        std::filesystem::create_directories("hwtest_out/probes");
        std::string exeDir = getTestExeDir();
        std::string makeprobePath = exeDir + "/m8_makeprobe.exe";
        std::string cmd = "\"" + makeprobePath + "\" --type sampler --sample-path samples/test.wav --out hwtest_out/probes/probe_sampler.m8s";
        std::system(cmd.c_str());
    }
}

TEST_CASE("S-RT1 sampler fields round-trip through save/reload", "[io]") {
    ensureSamplerProbeExists();
    auto a = loadSong("hwtest_out/probes/probe_sampler.m8s", "hwtest_out");
    REQUIRE(a.ok);
    REQUIRE(a.writable);

    auto& s = a.state.instruments[0].sampler;
    a.state.instruments[0].type = InstType::INST_SAMPLER;
    s.play = 2;
    s.start = 0x11;
    s.loop_st = 0x22;
    s.length = 0x33;
    s.slice = 0x44;
    s.degrade = 0x55;
    s.amp = 0x66;
    s.filter_type = 1;
    s.cutoff = 0x77;
    s.res = 0x18;
    s.lim = 1;
    s.pan = 0x40;
    s.dry = 0x50;
    s.cho = 0x10;
    s.del = 0x20;
    s.rev = 0x30;
    s.detune = 0x90;
    s.transp = 0;
    s.tbl_tic = 0x0F;

    std::string err;
    bool saved = saveSong("temp_rt_sampler.m8s", a, a.sequencer, a.state, err);
    REQUIRE(saved);
    REQUIRE(err.empty());

    auto b = loadSong("temp_rt_sampler.m8s", "hwtest_out");
    REQUIRE(b.ok);
    REQUIRE(b.state.instruments[0].type == InstType::INST_SAMPLER);
    const auto& s2 = b.state.instruments[0].sampler;
    REQUIRE(s2.play == 2);
    REQUIRE(s2.start == 0x11);
    REQUIRE(s2.loop_st == 0x22);
    REQUIRE(s2.length == 0x33);
    REQUIRE(s2.slice == 0x44);
    REQUIRE(s2.degrade == 0x55);
    REQUIRE(s2.amp == 0x66);
    REQUIRE(s2.filter_type == 1);
    REQUIRE(s2.cutoff == 0x77);
    REQUIRE(s2.res == 0x18);
    REQUIRE(s2.lim == 1);
    REQUIRE(s2.pan == 0x40);
    REQUIRE(s2.dry == 0x50);
    REQUIRE(s2.cho == 0x10);
    REQUIRE(s2.del == 0x20);
    REQUIRE(s2.rev == 0x30);
    REQUIRE(s2.detune == 0x90);
    REQUIRE(s2.transp == 0);
    REQUIRE(s2.tbl_tic == 0x0F);

    std::filesystem::remove("temp_rt_sampler.m8s");
}

TEST_CASE("S-DET2 detune loads/saves signed fine_pitch correctly", "[io]") {
    ensureSamplerProbeExists();
    auto a = loadSong("hwtest_out/probes/probe_sampler.m8s", "hwtest_out");
    REQUIRE(a.ok);

    // Test case 1: detune = 0x90 (+16) -> fine_pitch = 0x10 (+16)
    a.state.instruments[0].sampler.detune = 0x90;
    std::string err;
    REQUIRE(saveSong("temp_det1.m8s", a, a.sequencer, a.state, err));
    
    auto b = loadSong("temp_det1.m8s", "hwtest_out");
    REQUIRE(b.ok);
    REQUIRE(b.state.instruments[0].sampler.detune == 0x90);

    // Verify file bytes directly
    auto data1 = readFile("temp_det1.m8s");
    m8::BinaryReader r1(data1);
    m8::Song song1 = m8::Song::from_reader(r1);
    REQUIRE(std::get<m8::Sampler>(song1.instruments[0]).synth_params.fine_pitch == 0x10);
    
    std::filesystem::remove("temp_det1.m8s");

    // Test case 2: fine_pitch = 0xF0 (-16) -> detune = 0x70
    std::get<m8::Sampler>(song1.instruments[0]).synth_params.fine_pitch = 0xF0;
    auto outData = song1.write_over(data1);
    std::ofstream out("temp_det2.m8s", std::ios::binary);
    out.write(reinterpret_cast<char*>(outData.data()), outData.size());
    out.close();

    auto c = loadSong("temp_det2.m8s", "hwtest_out");
    REQUIRE(c.ok);
    REQUIRE(c.state.instruments[0].sampler.detune == 0x70);

    std::filesystem::remove("temp_det2.m8s");
}

// Helper: write a library Song to a temp .m8s file over a template's bytes.
static void writeSongFile(const std::string& path, m8::Song& song,
                          const std::vector<uint8_t>& templateBytes) {
    auto out = song.write_over(templateBytes);
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(out.data()), out.size());
}

// L12 — modeled FX commands TBL/GRV/TIC (lib 0x06/0x07/0x08) survive load and
// round-trip on save. Regression: libFxToEngine used to drop everything >= 0x06 to
// NONE, so table/groove/tic assignments were lost on load (and clobbered on save).
TEST_CASE("L12 TBL/GRV/TIC round-trip through file", "[io]") {
    auto data = readFile(songPath("V4EMPTY.m8s"));
    REQUIRE(!data.empty());

    m8::BinaryReader r(data);
    auto song = m8::Song::from_reader(r);
    // Inject TBL/GRV/TIC into phrase 0, row 0's three FX slots.
    song.phrases[0].steps[0].fx1.command = 0x06; song.phrases[0].steps[0].fx1.value = 0x11; // TBL
    song.phrases[0].steps[0].fx2.command = 0x07; song.phrases[0].steps[0].fx2.value = 0x22; // GRV
    song.phrases[0].steps[0].fx3.command = 0x08; song.phrases[0].steps[0].fx3.value = 0x33; // TIC
    writeSongFile("temp_fxrt1.m8s", song, data);

    // Load: the engine must decode them to TBL/GRV/TIC (not NONE).
    auto loaded = loadSong("temp_fxrt1.m8s", "");
    REQUIRE(loaded.ok);
    const auto& step = loaded.sequencer.phrases[0][0];
    REQUIRE(step.fx[0].cmd == FxCmd::TBL); REQUIRE(step.fx[0].val == 0x11);
    REQUIRE(step.fx[1].cmd == FxCmd::GRV); REQUIRE(step.fx[1].val == 0x22);
    REQUIRE(step.fx[2].cmd == FxCmd::TIC); REQUIRE(step.fx[2].val == 0x33);

    // Save and reparse: the file bytes must come back identical.
    std::string err;
    REQUIRE(saveSong("temp_fxrt1_out.m8s", loaded, loaded.sequencer, loaded.state, err));
    auto outBytes = readFile("temp_fxrt1_out.m8s");
    m8::BinaryReader r2(outBytes);
    auto reparsed = m8::Song::from_reader(r2);
    REQUIRE(reparsed.phrases[0].steps[0].fx1.command == 0x06);
    REQUIRE(reparsed.phrases[0].steps[0].fx1.value   == 0x11);
    REQUIRE(reparsed.phrases[0].steps[0].fx2.command == 0x07);
    REQUIRE(reparsed.phrases[0].steps[0].fx3.command == 0x08);

    std::filesystem::remove("temp_fxrt1.m8s");
    std::filesystem::remove("temp_fxrt1_out.m8s");
}

// L13 — unmodeled FX commands (lib byte >= 0x09, e.g. ARP) are preserved byte-for-byte
// across load -> save. Regression: convertEngineToSong unconditionally rewrote every
// phrase FX via engineFxToLib(NONE)=0xFF, silently destroying any command the engine
// did not model. Now they decode to FxCmd::UNKNOWN and the save loop leaves the original
// bytes intact.
TEST_CASE("L13 unmodeled FX commands preserved on save", "[io]") {
    auto data = readFile(songPath("V4EMPTY.m8s"));
    REQUIRE(!data.empty());

    m8::BinaryReader r(data);
    auto song = m8::Song::from_reader(r);
    // Inject two commands past TIC into phrase 1, row 2.
    song.phrases[1].steps[2].fx1.command = 0x09; song.phrases[1].steps[2].fx1.value = 0x55; // ARP-range
    song.phrases[1].steps[2].fx2.command = 0x14; song.phrases[1].steps[2].fx2.value = 0x66; // higher cmd
    writeSongFile("temp_fxrt2.m8s", song, data);

    // Load: unmodeled commands decode to UNKNOWN (inert, but present).
    auto loaded = loadSong("temp_fxrt2.m8s", "");
    REQUIRE(loaded.ok);
    const auto& step = loaded.sequencer.phrases[1][2];
    REQUIRE(step.fx[0].cmd == FxCmd::UNKNOWN);
    REQUIRE(step.fx[1].cmd == FxCmd::UNKNOWN);

    // Save and reparse: the original bytes (command AND value) must survive.
    std::string err;
    REQUIRE(saveSong("temp_fxrt2_out.m8s", loaded, loaded.sequencer, loaded.state, err));
    auto outBytes = readFile("temp_fxrt2_out.m8s");
    m8::BinaryReader r2(outBytes);
    auto reparsed = m8::Song::from_reader(r2);
    REQUIRE(reparsed.phrases[1].steps[2].fx1.command == 0x09);
    REQUIRE(reparsed.phrases[1].steps[2].fx1.value   == 0x55);
    REQUIRE(reparsed.phrases[1].steps[2].fx2.command == 0x14);
    REQUIRE(reparsed.phrases[1].steps[2].fx2.value   == 0x66);

    std::filesystem::remove("temp_fxrt2.m8s");
    std::filesystem::remove("temp_fxrt2_out.m8s");
}

// Helper: patch mixer_settings bytes into a .m8s file at the known offset.
// MixerSettings sits at byte offset 0xCE in V4/V4.1 songs (right before grooves
// at 0xEE). Song::write() does not persist mixer_settings, so write_over() cannot
// be used to test them — we must patch the raw bytes.
static constexpr size_t MIXER_OFFSET = 0xCE;

static void patchMonoAnalog(std::vector<uint8_t>& data,
                            uint8_t vol, uint8_t cho, uint8_t del, uint8_t rev) {
    data[MIXER_OFFSET + 0x0D] = vol;  // a_vol0 (left/mono volume)
    data[MIXER_OFFSET + 0x0E] = 0xFF; // a_vol1 = 0xFF sentinel (mono)
    data[MIXER_OFFSET + 0x10] = cho;  // a_cho0
    data[MIXER_OFFSET + 0x12] = del;  // a_del0
    data[MIXER_OFFSET + 0x14] = rev;  // a_rev0
}

static void patchStereoAnalog(std::vector<uint8_t>& data,
                               uint8_t lVol, uint8_t rVol,
                               uint8_t lCho, uint8_t rCho,
                               uint8_t lDel, uint8_t rDel,
                               uint8_t lRev, uint8_t rRev) {
    data[MIXER_OFFSET + 0x0D] = lVol;  // a_vol0 (left)
    data[MIXER_OFFSET + 0x0E] = rVol;  // a_vol1 (right — NOT 0xFF, so stereo)
    data[MIXER_OFFSET + 0x10] = lCho;  // a_cho0 (left)
    data[MIXER_OFFSET + 0x11] = rCho;  // right chorus
    data[MIXER_OFFSET + 0x12] = lDel;  // a_del0 (left)
    data[MIXER_OFFSET + 0x13] = rDel;  // right delay
    data[MIXER_OFFSET + 0x14] = lRev;  // a_rev0 (left)
    data[MIXER_OFFSET + 0x15] = rRev;  // right reverb
}

static void patchUsbInput(std::vector<uint8_t>& data,
                           uint8_t vol, uint8_t cho, uint8_t del, uint8_t rev) {
    data[MIXER_OFFSET + 0x0F] = vol;  // usb_volume
    data[MIXER_OFFSET + 0x16] = cho;  // usb_cho
    data[MIXER_OFFSET + 0x17] = del;  // usb_del
    data[MIXER_OFFSET + 0x18] = rev;  // usb_rev
}

// IO-1 — analog_input mono variant loads into engine mixer fields.
TEST_CASE("IO-1 mono analog_input maps to in_vol/in_cho/in_del/in_rev", "[io]") {
    auto data = readFile(songPath("V4EMPTY.m8s"));
    REQUIRE(!data.empty());

    patchMonoAnalog(data, 0x11, 0x22, 0x33, 0x44);
    patchUsbInput(data, 0x55, 0x66, 0x77, 0x88);

    std::ofstream f("temp_io1.m8s", std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
    f.close();

    auto loaded = loadSong("temp_io1.m8s", "");
    REQUIRE(loaded.ok);
    REQUIRE(loaded.state.mixer.in_vol == 0x11);
    REQUIRE(loaded.state.mixer.in_cho == 0x22);
    REQUIRE(loaded.state.mixer.in_del == 0x33);
    REQUIRE(loaded.state.mixer.in_rev == 0x44);
    REQUIRE(loaded.state.mixer.usb_vol == 0x55);
    REQUIRE(loaded.state.mixer.usb_cho == 0x66);
    REQUIRE(loaded.state.mixer.usb_del == 0x77);
    REQUIRE(loaded.state.mixer.usb_rev == 0x88);

    std::filesystem::remove("temp_io1.m8s");
}

// IO-2 — analog_input stereo variant loads left channel into engine mixer fields.
// The library's read path intentionally discards the right-channel bytes and
// constructs the right channel from left-channel values (types.cpp:198). So
// the stereo test verifies that left-channel values arrive in the engine fields,
// and that the right-channel bytes do not interfere.
TEST_CASE("IO-2 stereo analog_input maps left channel to in_vol/in_cho/in_del/in_rev", "[io]") {
    auto data = readFile(songPath("V4EMPTY.m8s"));
    REQUIRE(!data.empty());

    // Left channel: 0xAA/0xBB/0xCC/0xDD. Right channel: 0x11/0x22/0x33/0x44.
    patchStereoAnalog(data, 0xAA, 0x11, 0xBB, 0x22, 0xCC, 0x33, 0xDD, 0x44);
    patchUsbInput(data, 0x55, 0x66, 0x77, 0x88);

    std::ofstream f("temp_io2.m8s", std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
    f.close();

    auto loaded = loadSong("temp_io2.m8s", "");
    REQUIRE(loaded.ok);
    // Must match left channel — the library discards right-channel bytes
    REQUIRE(loaded.state.mixer.in_vol == 0xAA);
    REQUIRE(loaded.state.mixer.in_cho == 0xBB);
    REQUIRE(loaded.state.mixer.in_del == 0xCC);
    REQUIRE(loaded.state.mixer.in_rev == 0xDD);
    REQUIRE(loaded.state.mixer.usb_vol == 0x55);
    REQUIRE(loaded.state.mixer.usb_cho == 0x66);
    REQUIRE(loaded.state.mixer.usb_del == 0x77);
    REQUIRE(loaded.state.mixer.usb_rev == 0x88);

    std::filesystem::remove("temp_io2.m8s");
}

// IO-3 — stereo analog_input round-trips through library read.
// The library intentionally mirrors the Rust reference: right channel is
// constructed from left-channel values (types.cpp:198), so both sides of the
// pair carry identical values regardless of what the file contains.
TEST_CASE("IO-3 stereo analog_input round-trips through library read", "[io]") {
    auto data = readFile(songPath("V4EMPTY.m8s"));
    REQUIRE(!data.empty());

    patchStereoAnalog(data, 0xAA, 0x11, 0xBB, 0x22, 0xCC, 0x33, 0xDD, 0x44);

    m8::BinaryReader r(data);
    auto song = m8::Song::from_reader(r);

    // Must be stereo variant
    REQUIRE(std::holds_alternative<
        std::pair<m8::InputMixerSettings, m8::InputMixerSettings>>(
        song.mixer_settings.analog_input));

    auto& pair = std::get<std::pair<m8::InputMixerSettings, m8::InputMixerSettings>>(
        song.mixer_settings.analog_input);
    // Left channel carries the written values
    REQUIRE(pair.first.volume == 0xAA);
    REQUIRE(pair.first.chorus == 0xBB);
    REQUIRE(pair.first.delay == 0xCC);
    REQUIRE(pair.first.reverb == 0xDD);
    // Right channel is constructed from left (library discards right-channel bytes)
    REQUIRE(pair.second.volume == 0xAA);
    REQUIRE(pair.second.chorus == 0xBB);
    REQUIRE(pair.second.delay == 0xCC);
    REQUIRE(pair.second.reverb == 0xDD);
}

// IO-4 — mono analog_input round-trips through library read.
TEST_CASE("IO-4 mono analog_input round-trips through library read", "[io]") {
    auto data = readFile(songPath("V4EMPTY.m8s"));
    REQUIRE(!data.empty());

    patchMonoAnalog(data, 0x11, 0x22, 0x33, 0x44);

    m8::BinaryReader r(data);
    auto song = m8::Song::from_reader(r);

    // Must be mono variant
    REQUIRE(std::holds_alternative<m8::InputMixerSettings>(
        song.mixer_settings.analog_input));

    auto& mono = std::get<m8::InputMixerSettings>(song.mixer_settings.analog_input);
    REQUIRE(mono.volume == 0x11);
    REQUIRE(mono.chorus == 0x22);
    REQUIRE(mono.delay == 0x33);
    REQUIRE(mono.reverb == 0x44);
}

// ---------------------------------------------------------------------------
// Mixer model corrections (MIXER_SPEC.md §3). The file's single master gain is
// the mixer's MIX control; the screen's SPEAKER VOL is ours and is never
// persisted. `dj_peak` is OTT, not a filter resonance.
// ---------------------------------------------------------------------------

TEST_CASE("L14 master_volume loads into MIX, not SPEAKER VOL", "[io]") {
    auto path = songPath("V4EMPTY.m8s");
    auto result = loadSong(path, "");
    REQUIRE(result.ok);

    // Whatever the file says, it must land on mix_vol. Loading must not touch
    // out_vol at all -- it is application state, so it stays at its default.
    REQUIRE(result.state.mixer.out_vol == 0xFF);

    // And it must survive the trip back out: save writes master_volume from
    // mix_vol, so a reload sees the same value.
    std::string err;
    REQUIRE(saveSong("mixer_rt.m8s", result, result.sequencer, result.state, err));
    auto again = loadSong("mixer_rt.m8s", "");
    REQUIRE(again.ok);
    REQUIRE(again.state.mixer.mix_vol == result.state.mixer.mix_vol);
    REQUIRE(again.state.mixer.ott == result.state.mixer.ott);
    std::remove("mixer_rt.m8s");
}

TEST_CASE("L15 SPEAKER VOL is never written to the song", "[io]") {
    auto path = songPath("V4EMPTY.m8s");
    auto result = loadSong(path, "");
    REQUIRE(result.ok);

    // Move SPEAKER VOL somewhere distinctive and save. The file must be
    // byte-identical to a save that left it alone -- it has no slot in the
    // format and must not displace the song's own master volume.
    std::string err;
    REQUIRE(saveSong("spk_a.m8s", result, result.sequencer, result.state, err));

    auto altered = result.state;
    altered.mixer.out_vol = 0x10;
    REQUIRE(saveSong("spk_b.m8s", result, result.sequencer, altered, err));

    auto a = readFile("spk_a.m8s");
    auto b = readFile("spk_b.m8s");
    REQUIRE(a.size() == b.size());
    size_t diffs = 0;
    for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) ++diffs;
    REQUIRE(diffs == 0);

    std::remove("spk_a.m8s");
    std::remove("spk_b.m8s");
}

// ---------------------------------------------------------------------------
// EQ banks (EQ_SPEC.md step 2). The encoding is confirmed in EQ_SPEC.md §4;
// these pin that we decode it the same way and give the bytes back unchanged.
// ---------------------------------------------------------------------------

TEST_CASE("L16 EQ banks load at their factory defaults", "[io]") {
    auto result = loadSong(songPath("V4EMPTY.m8s"), "");
    REQUIRE(result.ok);

    // Every song file in the tree carries the M8's factory EQ: a low shelf at
    // 100 Hz, a bell at 1 kHz, a high shelf at 5 kHz, all flat, Q 50, stereo.
    const auto& bank = result.state.eqs[0];
    REQUIRE(bank.low.type  == 1);      // LOWSHELF
    REQUIRE(bank.mid.type  == 2);      // BELL
    REQUIRE(bank.high.type == 4);      // HI.SHELF
    REQUIRE(bank.low.freq  == 100);
    REQUIRE(bank.mid.freq  == 1000);
    REQUIRE(bank.high.freq == 5000);
    REQUIRE(bank.low.gain  == 0);
    REQUIRE(bank.mid.q     == 50);
    REQUIRE(bank.high.mode == 0);      // STEREO
}

TEST_CASE("L17 EQ bank count follows the song version", "[io]") {
    // V4 carries 32 banks, V4.1+ carries 128. Banks past the file's count stay
    // at their defaults rather than reading garbage.
    auto v4 = loadSong(songPath("V4EMPTY.m8s"), "");
    auto v41 = loadSong(songPath("V4-1EMPTY.m8s"), "");
    REQUIRE(v4.ok);
    REQUIRE(v41.ok);

    // Bank 100 exists only in the larger file; in both cases it must decode to
    // something sane rather than to noise.
    REQUIRE(v4.state.eqs[100].low.freq == 100);
    REQUIRE(v41.state.eqs[100].low.freq == 100);
    REQUIRE(v4.state.eqs[100].mid.type == 2);
    REQUIRE(v41.state.eqs[100].mid.type == 2);
}

TEST_CASE("L18 edited EQ survives save and reload", "[io]") {
    auto result = loadSong(songPath("V4-1EMPTY.m8s"), "");
    REQUIRE(result.ok);

    // Values chosen to exercise the whole encoding: a negative gain (two's
    // complement across both bytes), a frequency needing both bytes, a
    // non-default type and a non-stereo mode.
    auto edited = result.state;
    edited.eqs[3].mid.type = 3;         // BANDPASS
    edited.eqs[3].mid.mode = 2;         // SIDE
    edited.eqs[3].mid.freq = 1547;
    edited.eqs[3].mid.gain = -500;      // -5.00 dB
    edited.eqs[3].mid.q    = 69;

    std::string err;
    REQUIRE(saveSong("eq_rt.m8s", result, result.sequencer, edited, err));
    auto again = loadSong("eq_rt.m8s", "");
    REQUIRE(again.ok);

    const auto& band = again.state.eqs[3].mid;
    REQUIRE(band.type == 3);
    REQUIRE(band.mode == 2);
    REQUIRE(band.freq == 1547);
    REQUIRE(band.gain == -500);
    REQUIRE(band.q    == 69);

    std::remove("eq_rt.m8s");
}

TEST_CASE("L19 untouched EQ banks round-trip byte-identically", "[io]") {
    // The whole point of decoding into ints and re-packing is that it must be
    // lossless. L4 covers the file as a whole; this narrows it to the EQ block
    // so a failure here names the culprit directly.
    auto path = songPath("V4-1EMPTY.m8s");
    auto result = loadSong(path, "");
    REQUIRE(result.ok);

    std::string err;
    REQUIRE(saveSong("eq_identity.m8s", result, result.sequencer, result.state, err));

    auto orig = readFile(path);
    auto rt   = readFile("eq_identity.m8s");
    REQUIRE(orig.size() == rt.size());

    // 0x1AD5E, 128 banks of 18 bytes -- see EQ_SPEC.md §4.
    constexpr size_t kEqOffset = 0x1AD5A + 4;
    constexpr size_t kEqBytes  = 128 * 18;
    REQUIRE(orig.size() >= kEqOffset + kEqBytes);

    size_t diffs = 0;
    for (size_t i = kEqOffset; i < kEqOffset + kEqBytes; ++i)
        if (orig[i] != rt[i]) ++diffs;
    REQUIRE(diffs == 0);

    std::remove("eq_identity.m8s");
}

// ---------------------------------------------------------------------------
// L20-L24 -- the blocks Song::write never emits.
//
// `Song::write` seeks straight to the song steps and only writes the data
// sections from there on, so everything in the header region (tempo, mixer,
// grooves) and the effects block further down survived save-by-overlay as
// whatever the original file said. `convertEngineToSong` had been filling all
// four in on the Song object for as long as it has existed, and `write_over`
// silently discarded every one of them: an edited tempo, mixer level, groove
// or effect saved successfully and came back unchanged.
//
// These tests are written against the observable contract (edit -> save ->
// reload -> value survived), not against the patch, so they stay meaningful if
// the library ever grows a real writer for these blocks.
// ---------------------------------------------------------------------------

TEST_CASE("L20 edited mixer settings survive save and reload", "[io]") {
    auto result = loadSong(songPath("V4-1EMPTY.m8s"), "");
    REQUIRE(result.ok);

    auto edited = result.state;
    edited.mixer.mix_vol      = 0x77;
    edited.mixer.lim_val      = 0x42;
    edited.mixer.djf_freq     = 0x30;
    edited.mixer.ott          = 0x64;
    edited.mixer.djf_typ      = 0x02;
    edited.mixer.cho_vol      = 0x11;
    edited.mixer.del_vol      = 0x22;
    edited.mixer.rev_vol      = 0x33;
    for (int i = 0; i < 8; ++i) edited.mixer.track_vol[i] = 0x10 + i;

    std::string err;
    REQUIRE(saveSong("mixer_rt.m8s", result, result.sequencer, edited, err));
    auto again = loadSong("mixer_rt.m8s", "");
    REQUIRE(again.ok);

    REQUIRE(again.state.mixer.mix_vol  == 0x77);
    REQUIRE(again.state.mixer.lim_val  == 0x42);
    REQUIRE(again.state.mixer.djf_freq == 0x30);
    REQUIRE(again.state.mixer.ott      == 0x64);
    REQUIRE(again.state.mixer.djf_typ  == 0x02);
    REQUIRE(again.state.mixer.cho_vol  == 0x11);
    REQUIRE(again.state.mixer.del_vol  == 0x22);
    REQUIRE(again.state.mixer.rev_vol  == 0x33);
    for (int i = 0; i < 8; ++i)
        REQUIRE(again.state.mixer.track_vol[i] == 0x10 + i);

    std::remove("mixer_rt.m8s");
}

TEST_CASE("L21 edited tempo survives save and reload", "[io]") {
    auto result = loadSong(songPath("V4-1EMPTY.m8s"), "");
    REQUIRE(result.ok);

    // 138.50 is exactly representable once reassembled from bpm + bpm_frac,
    // so this asserts the write happened, not the float's rounding behaviour.
    auto edited = result.state;
    edited.bpm      = 138;
    edited.bpm_frac = 50;

    std::string err;
    REQUIRE(saveSong("tempo_rt.m8s", result, result.sequencer, edited, err));
    auto again = loadSong("tempo_rt.m8s", "");
    REQUIRE(again.ok);

    REQUIRE(again.state.bpm      == 138);
    REQUIRE(again.state.bpm_frac == 50);

    std::remove("tempo_rt.m8s");
}

TEST_CASE("L22 edited grooves survive save and reload", "[io]") {
    auto result = loadSong(songPath("V4-1EMPTY.m8s"), "");
    REQUIRE(result.ok);

    auto seq = result.sequencer;
    // A 7/5 swing in groove 1, and a value in the last slot of groove 31 so the
    // whole 32 x 16 block is covered, not just the first row.
    seq.grooves[1].steps[0]  = 7;
    seq.grooves[1].steps[1]  = 5;
    seq.grooves[31].steps[15] = 9;

    std::string err;
    REQUIRE(saveSong("groove_rt.m8s", result, seq, result.state, err));
    auto again = loadSong("groove_rt.m8s", "");
    REQUIRE(again.ok);

    REQUIRE(again.sequencer.grooves[1].steps[0]   == 7);
    REQUIRE(again.sequencer.grooves[1].steps[1]   == 5);
    REQUIRE(again.sequencer.grooves[31].steps[15] == 9);

    std::remove("groove_rt.m8s");
}

// Re-enabled 2026-08-14 once the effects block was read and written at the
// measured offsets instead of the library's. Note this test alone would NOT
// have caught the offset bug -- load and save were wrong in the same direction
// and cancelled out. L25 below is the one that pins the direction.
TEST_CASE("L23 edited effects settings survive save and reload", "[io]") {
    auto result = loadSong(songPath("V4-1EMPTY.m8s"), "");
    REQUIRE(result.ok);

    auto edited = result.state;
    edited.effects.cho_mod_depth = 0x21;
    edited.effects.cho_mod_freq  = 0x22;
    edited.effects.cho_width     = 0x23;
    edited.effects.del_time_l    = 0x24;
    edited.effects.del_time_r    = 0x25;
    edited.effects.del_feedback  = 0x26;
    edited.effects.del_width     = 0x27;
    edited.effects.del_reverb    = 0x28;
    edited.effects.rev_size      = 0x29;
    edited.effects.rev_decay     = 0x2A;
    edited.effects.rev_mod_depth = 0x2B;
    edited.effects.rev_mod_freq  = 0x2C;
    edited.effects.rev_width     = 0x2D;

    std::string err;
    REQUIRE(saveSong("fx_rt.m8s", result, result.sequencer, edited, err));
    auto again = loadSong("fx_rt.m8s", "");
    REQUIRE(again.ok);

    const auto& fx = again.state.effects;
    REQUIRE(fx.cho_mod_depth == 0x21);
    REQUIRE(fx.cho_mod_freq  == 0x22);
    REQUIRE(fx.cho_width     == 0x23);
    REQUIRE(fx.del_time_l    == 0x24);
    REQUIRE(fx.del_time_r    == 0x25);
    REQUIRE(fx.del_feedback  == 0x26);
    REQUIRE(fx.del_width     == 0x27);
    REQUIRE(fx.del_reverb    == 0x28);
    REQUIRE(fx.rev_size      == 0x29);
    REQUIRE(fx.rev_decay     == 0x2A);
    REQUIRE(fx.rev_mod_depth == 0x2B);
    REQUIRE(fx.rev_mod_freq  == 0x2C);
    REQUIRE(fx.rev_width     == 0x2D);

    std::remove("fx_rt.m8s");
}

TEST_CASE("L24 patching the mixer leaves the bytes we do not model alone", "[io]") {
    // The mixer block has twelve bytes of analog/USB input and a four-byte tail
    // the file library discards on read and zeroes on write. Neither is ours to
    // touch: the inputs cannot represent a right channel in our engine
    // (hw_findings.md §UI-4e) and the tail is unidentified but demonstrably not
    // padding -- V4EMPTY and V4-1EMPTY both carry 40 70 12 32 there. This is why
    // saveUnwrittenBlocks patches field by field instead of calling the
    // library's MixerSettings::write, and this test is what stops someone
    // "simplifying" it into that call.
    auto path = songPath("V4-1EMPTY.m8s");
    auto result = loadSong(path, "");
    REQUIRE(result.ok);

    auto edited = result.state;
    edited.mixer.mix_vol = 0x55;   // force the patch to run

    std::string err;
    REQUIRE(saveSong("mixer_tail.m8s", result, result.sequencer, edited, err));

    auto orig = readFile(path);
    auto rt   = readFile("mixer_tail.m8s");
    REQUIRE(orig.size() == rt.size());

    constexpr size_t kMixerOffset = 0xCE;
    REQUIRE(orig.size() >= kMixerOffset + 32);

    // The whole tail (+28 ATK, +29 REL, +30 SOFT CLIP, +31 OTT) is modeled as
    // of §UI-9, so it is written rather than preserved -- L27 and L28 cover it.
    // What this case still guards is the analog/USB input block below.

    // Analog + USB input bytes, +13..+24, likewise untouched.
    for (size_t i = 13; i <= 24; ++i)
        REQUIRE(rt[kMixerOffset + i] == orig[kMixerOffset + i]);

    // And the edit we did ask for landed.
    REQUIRE(rt[kMixerOffset + 0] == 0x55);

    std::remove("mixer_tail.m8s");
}

// ---------------------------------------------------------------------------
// L25 -- the effects block loads the bytes the DEVICE says it should.
//
// This is the test the round-trip cases could never be. L4, L19 and L23 all
// check that what we write comes back, which passes just as happily when load
// and save are wrong in the same direction -- and that is exactly how the
// effects offsets stayed broken: the file library starts delay 3 bytes early
// and reverb 5 bytes early, we inherited that on both sides, and every
// round-trip closed on itself.
//
// The only thing that breaks the symmetry is an external statement of what the
// bytes mean. That is what this fixture is: scope_rel_10.m8s was saved by a
// real M8 (firmware 6.5.2) while its EFFECT SETTINGS screen was photographed,
// so these expected values are read off the device's own display, not off our
// parser (hw_findings.md UI-8).
//
//     MODFX   MOD DEPTH:FRQ 40:80   STEREO WIDTH FF   REVERB SEND 00
//     DELAY   TIME L:R      30:30   FEEDBACK     80   STEREO WIDTH FF   REVERB SEND 00
//     REVERB  ROOM SIZE     FF      DECAY        C0   MOD DEPTH:FRQ 10:FF  STEREO WIDTH FF
// ---------------------------------------------------------------------------
TEST_CASE("L25 effects load matches the device screen", "[io]") {
    auto result = loadSong("tests/fixtures/device_golden/scope_rel_10.m8s", "");
    REQUIRE(result.ok);
    const auto& fx = result.state.effects;

    // MODFX
    REQUIRE(fx.cho_mod_depth == 0x40);
    REQUIRE(fx.cho_mod_freq  == 0x80);
    REQUIRE(fx.cho_width     == 0xFF);
    REQUIRE(fx.cho_reverb    == 0x00);

    // DELAY -- these are the ones the library got wrong. Before the fix
    // feedback read 0x00 here while the device plainly showed 0x80.
    REQUIRE(fx.del_time_l   == 0x30);
    REQUIRE(fx.del_time_r   == 0x30);
    REQUIRE(fx.del_feedback == 0x80);
    REQUIRE(fx.del_width    == 0xFF);
    REQUIRE(fx.del_reverb   == 0x00);

    // REVERB -- likewise; decay read 0x00 instead of 0xC0.
    REQUIRE(fx.rev_size      == 0xFF);
    REQUIRE(fx.rev_decay     == 0xC0);
    REQUIRE(fx.rev_mod_depth == 0x10);
    REQUIRE(fx.rev_mod_freq  == 0xFF);
    REQUIRE(fx.rev_width     == 0xFF);
}

// L26 -- saving must not disturb the effects bytes we do not model.
// MOD TYPE (+4) and the unknown runs carry real device data; the block is
// patched field by field precisely so they survive, and this stops anyone
// "simplifying" it back into a whole-block write.
TEST_CASE("L26 unmodelled effects bytes survive a save", "[io]") {
    const char* path = "tests/fixtures/device_golden/scope_rel_10.m8s";
    auto result = loadSong(path, "");
    REQUIRE(result.ok);

    auto edited = result.state;
    edited.effects.del_feedback = 0x55;   // force the patch to run

    std::string err;
    REQUIRE(saveSong("fx_preserve.m8s", result, result.sequencer, edited, err));

    auto orig = readFile(path);
    auto rt   = readFile("fx_preserve.m8s");
    REQUIRE(orig.size() == rt.size());

    constexpr size_t kFxBase = 0x1A5C1;
    REQUIRE(orig.size() >= kFxBase + 22);

    // Untouched: MOD TYPE and the two unknown runs inside the block.
    const int untouched[] = {4, 5, 6, 7, 8, 14, 15, 16};
    for (int i : untouched)
        REQUIRE(rt[kFxBase + i] == orig[kFxBase + i]);

    // And the edit landed where the device says feedback lives (+11).
    REQUIRE(rt[kFxBase + 11] == 0x55);

    std::remove("fx_preserve.m8s");
}

// ---------------------------------------------------------------------------
// L27 -- OTT and the DJ filter's RES go to the bytes the device uses.
//
// These were swapped from 2026-08-12 until 2026-08-14: hw_findings.md §UI-4a
// concluded the file library's `dj_peak` was OTT and the field was renamed to
// match. A device probe then moved RES and watched `dj_peak` move with it while
// OTT sat still, and separately moved OTT and watched 0xED follow (§UI-9). So
// `dj_peak` is resonance -- the library's original name -- and OTT lives at
// 0xED, which the library does not model at all.
//
// Like L25 for the effects block, this is anchored to committed device files
// rather than to a round-trip, because a round-trip passes just as happily when
// both directions are wrong in the same way.
// ---------------------------------------------------------------------------
TEST_CASE("L27 OTT reads 0xED and RES reads dj_peak", "[io]") {
    constexpr size_t kMixerOffset = 0xCE;

    SECTION("values come from the right bytes") {
        // PROBEB was saved with RES = 0x30 on screen and OTT = 0x00.
        auto raw = readFile("tests/fixtures/device_golden/probe_res30.m8s");
        REQUIRE(raw.size() > kMixerOffset + 32);
        REQUIRE(raw[kMixerOffset + 26] == 0x30);   // dj_peak == RES
        REQUIRE(raw[kMixerOffset + 31] == 0x00);   // OTT

        auto r = loadSong("tests/fixtures/device_golden/probe_res30.m8s", "");
        REQUIRE(r.ok);
        REQUIRE(r.state.mixer.djf_res == 0x30);
        REQUIRE(r.state.mixer.ott     == 0x00);
    }

    SECTION("and the other way round") {
        // PROBEC was saved with OTT = 0xA0 on screen and RES back at 0x00.
        auto raw = readFile("tests/fixtures/device_golden/probe_ottA0.m8s");
        REQUIRE(raw.size() > kMixerOffset + 32);
        REQUIRE(raw[kMixerOffset + 26] == 0x00);
        REQUIRE(raw[kMixerOffset + 31] == 0xA0);

        auto r = loadSong("tests/fixtures/device_golden/probe_ottA0.m8s", "");
        REQUIRE(r.ok);
        REQUIRE(r.state.mixer.djf_res == 0x00);
        REQUIRE(r.state.mixer.ott     == 0xA0);
    }

    SECTION("both survive a save") {
        auto r = loadSong("tests/fixtures/device_golden/probe_ottA0.m8s", "");
        REQUIRE(r.ok);
        auto edited = r.state;
        edited.mixer.ott     = 0x5A;
        edited.mixer.djf_res = 0x2B;

        std::string err;
        REQUIRE(saveSong("ott_rt.m8s", r, r.sequencer, edited, err));

        auto rt = readFile("ott_rt.m8s");
        REQUIRE(rt[kMixerOffset + 31] == 0x5A);   // OTT
        REQUIRE(rt[kMixerOffset + 26] == 0x2B);   // RES

        auto again = loadSong("ott_rt.m8s", "");
        REQUIRE(again.ok);
        REQUIRE(again.state.mixer.ott     == 0x5A);
        REQUIRE(again.state.mixer.djf_res == 0x2B);

        std::remove("ott_rt.m8s");
    }
}

// L28 -- the scope parameters located in §UI-9 round-trip.
// ATK/REL/SOFT CLIP sit in the mixer tail, SHIMMER/TIME/COLOR/MOD TYPE in the
// effects block. None of them exist in the file library, so all eight are
// patched in by hand and this is what stops that drifting.
TEST_CASE("L28 scope-view parameters round-trip", "[io]") {
    auto r = loadSong("tests/fixtures/device_golden/probe_ottA0.m8s", "");
    REQUIRE(r.ok);

    auto edited = r.state;
    edited.mixer.lim_atk    = 0x11;
    edited.mixer.lim_rel    = 0x22;
    edited.mixer.soft_clip  = 0x01;
    edited.effects.rev_shimmer = 0x33;
    edited.effects.ott_time    = 0x44;
    edited.effects.ott_color   = 0x55;
    edited.effects.modfx_type  = 0x02;

    std::string err;
    REQUIRE(saveSong("scope_rt.m8s", r, r.sequencer, edited, err));
    auto again = loadSong("scope_rt.m8s", "");
    REQUIRE(again.ok);

    REQUIRE(again.state.mixer.lim_atk       == 0x11);
    REQUIRE(again.state.mixer.lim_rel       == 0x22);
    REQUIRE(again.state.mixer.soft_clip     == 0x01);
    REQUIRE(again.state.effects.rev_shimmer == 0x33);
    REQUIRE(again.state.effects.ott_time    == 0x44);
    REQUIRE(again.state.effects.ott_color   == 0x55);
    REQUIRE(again.state.effects.modfx_type  == 0x02);

    std::remove("scope_rt.m8s");
}

// L29 -- and they load from the bytes the device actually uses.
// probe_res30.m8s was photographed showing ATK 10 / SOFT CLIP ON / MOD TYPE
// Flanger / OTT TIME 40 / OTT COLOR 50, so these are the device's readings.
TEST_CASE("L29 scope parameters load from the device's bytes", "[io]") {
    auto r = loadSong("tests/fixtures/device_golden/probe_res30.m8s", "");
    REQUIRE(r.ok);
    REQUIRE(r.state.mixer.lim_atk      == 0x10);
    REQUIRE(r.state.mixer.soft_clip    == 0x01);
    REQUIRE(r.state.effects.ott_time   == 0x40);
    REQUIRE(r.state.effects.ott_color  == 0x50);
    REQUIRE(r.state.effects.modfx_type == 0x02);   // Flanger
}

// The enable mask and the name as the file holds them. The device pads names
// with 0xFF, so that terminates rather than being copied into the string.
static uint16_t scaleMask(const Scale& s) {
    uint16_t m = 0;
    for (int i = 0; i < 12; ++i)
        if (s.notes[i].enable) m |= static_cast<uint16_t>(1u << i);
    return m;
}
static std::string scaleName(const Scale& s) {
    std::string out;
    for (int i = 0; i < 16; ++i) {
        const unsigned char c = static_cast<unsigned char>(s.name[i]);
        if (c == 0x00 || c == 0xFF) break;
        out += static_cast<char>(c);
    }
    return out;
}

// L30 -- the 16 scale records round-trip, negative offsets included.
//
// Negative offsets are the case that forced this block to bypass the library:
// its Scale::from_reader takes the semitone byte UNSIGNED, so it cannot read
// the lower half of the M8's -24.00..+24.00 range at all. If that reader were
// used, ENC below would come back as +23.50 rather than -0.50.
TEST_CASE("L30 scales round-trip through a song file", "[io]") {
    auto r = loadSong("tests/fixtures/device_golden/probe_ottA0.m8s", "");
    REQUIRE(r.ok);

    // Stride first, because getting it wrong is silent: 2 + 24 + 16 = 42 looks
    // like the record size and is not -- it is 46, with four unmodelled bytes
    // trailing. At 42 the reads drift by 4 per record, so record 0 still looks
    // perfect and every later one is quietly shifted. These three masks are the
    // factory scales the file carries, and only the right stride decodes all
    // three (a 42 stride gives record 2 a 16-bit mask of 0xFFFF, which cannot
    // be a 12-interval scale at all).
    REQUIRE(scaleMask(r.state.scales[0]) == 0x0FFF);   // CHROMATIC
    REQUIRE(scaleMask(r.state.scales[1]) == 0x0AB5);   // MAJOR
    REQUIRE(scaleMask(r.state.scales[2]) == 0x05AD);   // NATURAL MINOR
    REQUIRE(scaleName(r.state.scales[0]) == "CHROMATIC");
    REQUIRE(scaleName(r.state.scales[1]) == "MAJOR");
    REQUIRE(scaleName(r.state.scales[2]) == "MINOR");

    auto edited = r.state;
    auto& s = edited.scales[3];
    for (int i = 0; i < 12; ++i) {
        s.notes[i].enable = (i % 2) == 0;      // whole tone from C
        s.notes[i].offset = 0.0f;
    }
    s.notes[0].offset  =  1.25f;
    s.notes[2].offset  = -0.50f;               // the sub-semitone signed case
    s.notes[4].offset  = -24.0f;               // the documented lower bound
    s.notes[6].offset  =  24.0f;               // and the upper
    std::memcpy(s.name, "WHOLE TONE------", 16);
    edited.project.scale = 0x07;               // global KEY, was never persisted

    std::string err;
    REQUIRE(saveSong("scale_rt.m8s", r, r.sequencer, edited, err));
    auto again = loadSong("scale_rt.m8s", "");
    REQUIRE(again.ok);

    const auto& got = again.state.scales[3];
    for (int i = 0; i < 12; ++i)
        REQUIRE(got.notes[i].enable == ((i % 2) == 0));
    REQUIRE(std::fabs(got.notes[0].offset -  1.25f) < 0.005f);
    REQUIRE(std::fabs(got.notes[2].offset - -0.50f) < 0.005f);
    REQUIRE(std::fabs(got.notes[4].offset - -24.0f) < 0.005f);
    REQUIRE(std::fabs(got.notes[6].offset -  24.0f) < 0.005f);
    REQUIRE(std::string(got.name, 16) == "WHOLE TONE------");
    REQUIRE(again.state.project.scale == 0x07);

    // The other 15 records must be untouched by the edit to one of them.
    for (int i = 0; i < 16; ++i) {
        if (i == 3) continue;
        for (int n = 0; n < 12; ++n)
            REQUIRE(again.state.scales[i].notes[n].enable == r.state.scales[i].notes[n].enable);
    }

    std::remove("scale_rt.m8s");
}

// L31 -- the OFFSET encoding, read off a device-authored file.
//
// scaleprobe.m8s was made on hardware (fw 6.5.2, 2026-08-14) for exactly this:
// scale 00 restricted to C and E, scale 01 restricted to C with its OFFSET set
// to -00.50, everything else left alone. Scale 01's first offset is `CE FF` in
// the file -- 0xFFCE little-endian, -50 -- so an offset is a SIGNED 16-BIT LE
// value in HUNDREDTHS of a semitone, the same scheme AGENTS.md §7 records for
// EQ gain.
//
// This case was [!shouldfail] for the few hours between implementing scales and
// getting the probe back, because we had read the pair as (signed whole
// semitone, unsigned hundredths) exactly as the vendored library does. That
// reading agrees on every all-zero offset, which is every offset in every song
// we held -- so nothing in the repo could distinguish the two -- and it cannot
// represent anything in (-1.00, 0.00) at all: -0.50 encodes as whole 0 / cents
// 50 and reads back as +0.50. The inability to express half the documented
// -24.00..+24.00 range was the tell, but a tell is not a measurement, so it
// took the file.
TEST_CASE("L31 scale offsets are signed 16-bit hundredths", "[io]") {
    auto r = loadSong("tests/fixtures/device_golden/scaleprobe.m8s", "");
    REQUIRE(r.ok);

    // What the device was set to, straight off its own save.
    REQUIRE(scaleMask(r.state.scales[0]) == 0x0011);   // C and E only
    REQUIRE(scaleMask(r.state.scales[1]) == 0x0001);   // C only
    REQUIRE(std::fabs(r.state.scales[1].notes[0].offset - -0.50f) < 0.005f);

    // Untouched records still decode, which is the 46-byte stride surviving a
    // real device save rather than only our own writer.
    REQUIRE(scaleMask(r.state.scales[2]) == 0x05AD);   // NATURAL MINOR
    REQUIRE(scaleName(r.state.scales[2]) == "MINOR");
    REQUIRE(scaleName(r.state.scales[3]) == "DORIAN");

    // And it survives our round-trip, negative sub-semitone included.
    auto edited = r.state;
    edited.scales[5].notes[1].enable = true;
    edited.scales[5].notes[1].offset = -0.50f;
    edited.scales[5].notes[2].offset =  23.99f;
    edited.scales[5].notes[3].offset = -23.99f;

    std::string err;
    REQUIRE(saveSong("scale_neg.m8s", r, r.sequencer, edited, err));
    auto again = loadSong("scale_neg.m8s", "");
    REQUIRE(again.ok);
    const auto& got = again.state.scales[5];
    std::remove("scale_neg.m8s");

    REQUIRE(std::fabs(got.notes[1].offset - -0.50f)  < 0.005f);
    REQUIRE(std::fabs(got.notes[2].offset -  23.99f) < 0.005f);
    REQUIRE(std::fabs(got.notes[3].offset - -23.99f) < 0.005f);
}

// L32 -- SCA is library byte 0x10 and SCG is 0x11, read off a device save.
//
// The same probe carries the answer: a phrase authored on hardware with SCG 10
// on row 0 and SCA 20 on row 1 stores its FX slots as `11 10` and `10 20`.
//
// FX_COMMANDS_SPEC.md Part K says 0x17 and 0x18, and this is what disproves it.
// That table derives the whole 0x09..0x23 run by walking the M8 manual's FX list
// in order, and the device's own enum is not in that order either -- stepping it
// gives ARP ARC CHA DEL GRV HOP RND RNL RET REP RTO NTH PSL PBN PVB PVX SCA SCG,
// with DEL/GRV/HOP where the spec puts RND/RNL/RET and RMX missing. Nothing else
// in that table should be trusted without its own probe.
TEST_CASE("L32 SCA/SCG decode from the bytes a device writes", "[io]") {
    auto r = loadSong("tests/fixtures/device_golden/scaleprobe.m8s", "");
    REQUIRE(r.ok);

    const auto& row0 = r.sequencer.phrases[0][0];
    const auto& row1 = r.sequencer.phrases[0][1];

    REQUIRE(row0.fx[0].cmd == FxCmd::SCG);
    REQUIRE(row0.fx[0].val == 0x10);
    REQUIRE(row1.fx[0].cmd == FxCmd::SCA);
    REQUIRE(row1.fx[0].val == 0x20);

    // And they survive a save, so a song using them is not silently rewritten.
    std::string err;
    REQUIRE(saveSong("sca_rt.m8s", r, r.sequencer, r.state, err));
    auto again = loadSong("sca_rt.m8s", "");
    REQUIRE(again.ok);
    std::remove("sca_rt.m8s");

    REQUIRE(again.sequencer.phrases[0][0].fx[0].cmd == FxCmd::SCG);
    REQUIRE(again.sequencer.phrases[0][0].fx[0].val == 0x10);
    REQUIRE(again.sequencer.phrases[0][1].fx[0].cmd == FxCmd::SCA);
    REQUIRE(again.sequencer.phrases[0][1].fx[0].val == 0x20);
}

// L33 -- AMP and LIM come from amp_type and amp_limit, NOT from volume.
//
// This is the offline form of a hardware experiment run on 2026-08-19 (fw 6.5.2,
// COM3). The fixture is the exact .m8s that was loaded on the device: every
// instrument parameter slot carries a distinct signature byte, so reading the
// INSTRUMENT screen said unambiguously which file byte feeds which UI field.
// What the device showed:
//
//     file volume    = 0x11  ->  appears NOWHERE on the INSTRUMENT screen
//     file amp_type  = 0x22  ->  AMP 22
//     file amp_limit = 0x03  ->  LIM 03
//     file mixer_pan = 0x44  ->  PAN 44
//     file mixer_dry = 0x55  ->  DRY 55
//     cho/del/rev  = 66/77/88 -> MFX 66  DEL 77  REV 88
//
// SongIO read `amp` from `volume` and `lim` from `amp_type` -- every field one
// across -- so every loaded song played with the wrong amp value AND the wrong
// limiter mode. It went unnoticed for months because nothing compared a loaded
// value against the device, and because the two wrong bytes are both plausible
// numbers. The expectations below are hardware-verified, not derived from the
// code they check. AGENTS.md 7 carries the byte map.
TEST_CASE("L33 AMP and LIM load from amp_type and amp_limit, not volume", "[io]") {
    auto r = loadSong("tests/fixtures/device_golden/instmap.m8s", "");
    REQUIRE(r.ok);
    REQUIRE(r.state.instruments[0].type == InstType::INST_WAVSYNTH);
    const auto& ws = r.state.instruments[0].wav;

    // The one that was wrong. 0x22 is amp_type; 0x11 is volume.
    CHECK(ws.amp == 0x22);
    CHECK(ws.amp != 0x11);

    // The other half of the same shift. 0x03 is amp_limit; 0x22 is amp_type.
    CHECK(ws.lim == 0x03);
    CHECK(ws.lim != 0x22);

    // Carried so a save cannot zero it, even though the voice does not apply it.
    CHECK(ws.volume == 0x11);

    // These were always right, and are here so a "fix" that shifts the whole
    // block the other way cannot pass.
    CHECK(ws.pan == 0x44);
    CHECK(ws.dry == 0x55);
    CHECK(ws.cho == 0x66);
    CHECK(ws.del == 0x77);
    CHECK(ws.rev == 0x88);
}
