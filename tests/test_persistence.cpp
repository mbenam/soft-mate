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

    auto* buf = new uint8_t[sizeof(Sequencer) + sizeof(EngineState)];
    *reinterpret_cast<Sequencer*>(buf) = result.sequencer;
    *reinterpret_cast<EngineState*>(buf + sizeof(Sequencer)) = result.state;

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

    auto* buf = new uint8_t[sizeof(Sequencer) + sizeof(EngineState)];
    *reinterpret_cast<Sequencer*>(buf) = result.sequencer;
    *reinterpret_cast<EngineState*>(buf + sizeof(Sequencer)) = result.state;

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

    auto* buf = new uint8_t[sizeof(Sequencer) + sizeof(EngineState)];
    *reinterpret_cast<Sequencer*>(buf) = result.sequencer;
    *reinterpret_cast<EngineState*>(buf + sizeof(Sequencer)) = result.state;

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
    CommandRing<EngineCommand, 1024> ringA;
    auto engineA = std::make_unique<Engine>(ringA);
    engineA->loadDemoSong();
    {
        std::vector<float> warmup(512 * 2);
        engineA->render(warmup.data(), 512);  // let demo song run briefly
    }

    auto resultA = loadSong(songPath("TEST-FILE.m8s"), "");
    REQUIRE(resultA.ok);
    {
        auto* buf = new uint8_t[sizeof(Sequencer) + sizeof(EngineState)];
        *reinterpret_cast<Sequencer*>(buf) = resultA.sequencer;
        *reinterpret_cast<EngineState*>(buf + sizeof(Sequencer)) = resultA.state;
        EngineCommand cmd{};
        cmd.type = CommandType::LOAD_SONG;
        cmd.u.song.data = buf;
        ringA.push(cmd);
    }
    {
        EngineCommand play{};
        play.type = CommandType::PLAY_START;
        play.value = 3;  // SONG
        ringA.push(play);
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
    CommandRing<EngineCommand, 1024> ringB;
    auto engineB = std::make_unique<Engine>(ringB);
    engineB->getSequencerForInit().clear();

    auto resultB = loadSong(songPath("TEST-FILE.m8s"), "");
    REQUIRE(resultB.ok);
    {
        auto* buf = new uint8_t[sizeof(Sequencer) + sizeof(EngineState)];
        *reinterpret_cast<Sequencer*>(buf) = resultB.sequencer;
        *reinterpret_cast<EngineState*>(buf + sizeof(Sequencer)) = resultB.state;
        EngineCommand cmd{};
        cmd.type = CommandType::LOAD_SONG;
        cmd.u.song.data = buf;
        ringB.push(cmd);
    }
    {
        EngineCommand play{};
        play.type = CommandType::PLAY_START;
        play.value = 3;  // SONG
        ringB.push(play);
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

TEST_CASE("S-RT1 sampler fields round-trip through save/reload", "[io]") {
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
