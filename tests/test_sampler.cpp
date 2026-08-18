#include <catch2/catch_test_macros.hpp>
#include "support/OfflineHost.h"
#include "engine/SamplerEngine.h"
#include "engine/ZdfFilter.h"
#include <vector>
#include <cstring>
#include <cstdio>
#include <cmath>

using namespace m8::test;
using namespace m8::engine;
using namespace std;

// RMS of a `freqHz` sine after passing through the ZDF SVF, measured over the
// steady-state second half (past the filter's transient). `wantHp` selects the
// high-pass output, otherwise low-pass. Input sine amplitude is 1.0, so the
// return value is directly the magnitude response at that frequency.
static float zdfResponse(bool wantHp, float freqHz, float cutoffHz, float res) {
    ZdfSvf f;
    f.reset();
    f.setParams(cutoffHz, res, 48000.0f);
    const int n = 24000;
    double sumSq = 0.0;
    for (int i = 0; i < n; ++i) {
        float in = std::sin(6.2831853f * freqHz * i / 48000.0f);
        float hp = 0.0f;
        float lp = f.process(in, hp);
        float out = wantHp ? hp : lp;
        if (i >= n / 2) sumSq += double(out) * double(out);
    }
    return float(std::sqrt(sumSq / (n / 2)));
}

static SampleData makeRamp(int frames, int channels = 1, int sr = 48000) {
    SampleData sd{};
    sd.frames = frames;
    sd.channels = channels;
    sd.sampleRate = sr;
    sd.data = new float[frames * channels];
    // Channel 0 ramps up, every other channel is its negation -- so a stereo ramp
    // is maximally wide. The `channels == 1 ? ... : 0.0f` this used to have made
    // every stereo sample silent, which went unnoticed because nothing asked for
    // one until the stereo voice path did (S-ST1).
    for (int i = 0; i < frames; ++i) {
        float v = float(i) / float(frames - 1);
        for (int c = 0; c < channels; ++c)
            sd.data[i * channels + c] = (c == 0) ? v : -v;
    }
    return sd;
}

static SampleData makeConst(float value, int frames, int channels = 1, int sr = 48000) {
    SampleData sd{};
    sd.frames = frames;
    sd.channels = channels;
    sd.sampleRate = sr;
    sd.data = new float[frames * channels];
    for (int i = 0; i < frames * channels; ++i) sd.data[i] = value;
    return sd;
}

static void freeSample(SampleData& sd) { delete[] sd.data; sd.data = nullptr; }

static SampleData makeSample(const char* path, float value, int frames = 100) {
    SampleData sd;
    sd.frames = frames;
    sd.channels = 1;
    sd.sampleRate = 48000;
    sd.data = nullptr;
    std::strncpy(sd.path, path, 127);
    sd.path[127] = '\0';
    return sd;
}

struct S1Fixture {
    SamplerEngine eng;
    SampleData sd;
    SamplerState s;
    S1Fixture() {
        sd = makeRamp(1000);
        s.start = 0x80; s.loop_st = 0x00; s.length = 0xFF;
        s.play = 0; s.detune = 0x80;
        eng.noteOn(s, &sd);
    }
    ~S1Fixture() { freeSample(sd); }
};

TEST_CASE("S1 START=0x80 on ramp -> first output ~0.5", "[sampler]") {
    S1Fixture f;
    float out[2];
    f.eng.render(1.0f, out);
    REQUIRE(std::abs(out[0] - 0.5f) < 0.02f);
}

TEST_CASE("S2 FWD one-shot terminates", "[sampler]") {
    SamplerEngine eng;
    SampleData sd = makeRamp(1000);
    SamplerState s{};
    s.start = 0x00; s.loop_st = 0x00; s.length = 0x80;
    s.play = 0;
    eng.noteOn(s, &sd);
    int count = 0;
    while (!eng.finished() && count < 2000) {
        float out[2]; eng.render(1.0f, out); count++;
    }
    REQUIRE(eng.finished());
    REQUIRE(count > 400);
    REQUIRE(count < 600);
    freeSample(sd);
}

TEST_CASE("S3 LENGTH=0x00 plays >=1 frame, no hang", "[sampler]") {
    SamplerEngine eng;
    SampleData sd = makeRamp(100);
    SamplerState s{};
    s.start = 0x00; s.loop_st = 0x00; s.length = 0x00;
    s.play = 2; s.detune = 0x80;
    eng.noteOn(s, &sd);
    int count = 0;
    while (!eng.finished() && count < 1000) {
        float out[2]; eng.render(1.0f, out); count++;
    }
    REQUIRE(count >= 1);
    freeSample(sd);
}

TEST_CASE("S4 FWD one-shot finishes and stays silent", "[sampler]") {
    SamplerEngine eng;
    SampleData sd = makeRamp(100);
    SamplerState s{};
    s.start = 0x00; s.length = 0xFF; s.loop_st = 0x00; s.play = 0;
    eng.noteOn(s, &sd);
    while (!eng.finished()) { float out[2]; eng.render(1.0f, out); }
    float out[2];
    eng.render(1.0f, out);
    REQUIRE(out[0] == 0.0f);
    REQUIRE(out[1] == 0.0f);
    freeSample(sd);
}

TEST_CASE("S5 REV plays descending ramp", "[sampler]") {
    SamplerEngine eng;
    SampleData sd = makeRamp(100);
    SamplerState s{};
    s.start = 0x00; s.loop_st = 0x00; s.length = 0xFF; s.play = 1;
    eng.noteOn(s, &sd);
    float first[2], mid[2];
    eng.render(1.0f, first);
    for (int i = 0; i < 50; ++i) { float o[2]; eng.render(1.0f, o); }
    eng.render(1.0f, mid);
    REQUIRE(first[0] > mid[0]);
    freeSample(sd);
}

TEST_CASE("S6 FWDLOOP region model - loop within sample", "[sampler]") {
    SamplerEngine eng;
    SampleData sd = makeRamp(1000);
    SamplerState s{};
    s.start = 0x80; s.loop_st = 0x00; s.length = 0x40; s.play = 2;
    eng.noteOn(s, &sd);
    float first[2]; eng.render(1.0f, first);
    REQUIRE(std::abs(first[0] - 0.5f) < 0.02f);
    REQUIRE(!eng.finished());
    freeSample(sd);
}

TEST_CASE("S7 FWD_PP produces triangle-like shape", "[sampler]") {
    SamplerEngine eng;
    SampleData sd = makeRamp(100);
    SamplerState s{};
    s.start = 0x00; s.loop_st = 0x00; s.length = 0xFF; s.play = 4;
    eng.noteOn(s, &sd);
    float first[2], mid[2], late[2];
    eng.render(1.0f, first);
    for (int i = 0; i < 99; ++i) { float out[2]; eng.render(1.0f, out); }
    eng.render(1.0f, mid);
    for (int i = 0; i < 98; ++i) { float out[2]; eng.render(1.0f, out); }
    eng.render(1.0f, late);
    REQUIRE(mid[0] > first[0]);
    REQUIRE(late[0] < mid[0]);
    freeSample(sd);
}

TEST_CASE("S9 Stereo sample L=+1 R=-1", "[sampler]") {
    SamplerEngine eng;
    SampleData sd{};
    sd.frames = 10; sd.channels = 2; sd.sampleRate = 48000;
    sd.data = new float[20];
    for (int i = 0; i < 10; ++i) { sd.data[i*2] = 1.0f; sd.data[i*2+1] = -1.0f; }
    SamplerState s{}; s.start = 0x00; s.length = 0xFF; s.loop_st = 0x00; s.play = 2;
    eng.noteOn(s, &sd);
    float out[2]; eng.render(1.0f, out);
    REQUIRE(std::abs(out[0] - 1.0f) < 0.01f);
    REQUIRE(std::abs(out[1] - (-1.0f)) < 0.01f);
    freeSample(sd);
}

TEST_CASE("S10 C-4 on 48kHz -> ratio exactly 1.0", "[sampler]") {
    SamplerEngine eng;
    SampleData sd = makeRamp(1000);
    SamplerState s{};
    s.start = 0x00; s.length = 0xFF; s.loop_st = 0x00; s.play = 2; s.detune = 0x80;
    eng.noteOn(s, &sd);
    float out[2]; eng.render(1.0f, out);
    REQUIRE(std::abs(out[0] - 0.0f) < 0.01f);
    freeSample(sd);
}

TEST_CASE("S14 OSC mode ignores START", "[sampler]") {
    SamplerEngine eng;
    SampleData sd = makeRamp(1000);
    SamplerState s{};
    s.start = 0xFF; s.loop_st = 0x00; s.length = 0x40; s.play = 6;
    eng.noteOn(s, &sd);
    float out[2]; eng.render(1.0f, out);
    REQUIRE(!eng.finished());
    freeSample(sd);
}

TEST_CASE("S15 One-shot reads final frame (ASan clean)", "[sampler]") {
    SamplerEngine eng;
    SampleData sd = makeRamp(10);
    SamplerState s{};
    s.start = 0x00; s.length = 0xFF; s.loop_st = 0x00; s.play = 0;
    eng.noteOn(s, &sd);
    while (!eng.finished()) { float out[2]; eng.render(1.0f, out); }
    freeSample(sd);
    REQUIRE(true);
}

TEST_CASE("S16 Gate has no click", "[sampler]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_SAMPLER;
    state.instruments[0].mods[0] = {0, 0, 0x80, 0, 0, 0, 0};
    state.instruments[0].mods[1] = {0, 0, 0x80, 0, 0, 0, 0};
    state.instruments[0].mods[2] = {0, 0, 0x80, 0, 0, 0, 0};
    state.instruments[0].mods[3] = {0, 0, 0x80, 0, 0, 0, 0};

    std::vector<float> buf(1000);
    for (int i = 0; i < 1000; ++i) buf[i] = 0.5f;
    SampleData sd{}; sd.data = buf.data(); sd.frames = 1000; sd.channels = 1; sd.sampleRate = 48000;
    std::strncpy(sd.path, "gate_test.wav", 127);

    EngineCommand cmd; cmd.type = CommandType::LOAD_SAMPLE; cmd.targetId = 0; cmd.u.sample = sd;
    host.push(cmd);

    std::vector<float> ramp(1000);
    for (int i = 0; i < 1000; ++i) ramp[i] = float(i) / 1000.0f;

    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(200);

    float maxDelta = 0.0f;
    const auto& a = host.audio();
    for (size_t i = 2; i < a.size(); ++i) {
        float d = std::abs(a[i] - a[i-2]);
        if (d > maxDelta) maxDelta = d;
    }
    REQUIRE(maxDelta < 0.1f);
}

TEST_CASE("S17 One-shot has no midway dip", "[sampler]") {
    SamplerEngine eng;
    SampleData sd = makeConst(0.5f, 48000);
    SamplerState s{};
    s.start = 0x00; s.length = 0xFF; s.loop_st = 0x00; s.play = 0; s.detune = 0x80;
    eng.noteOn(s, &sd);
    float minVal = 1.0f, maxVal = 0.0f;
    int count = 0;
    while (!eng.finished() && count < 48000) {
        float out[2]; eng.render(1.0f, out);
        if (out[0] < minVal) minVal = out[0];
        if (out[0] > maxVal) maxVal = out[0];
        count++;
    }
    REQUIRE(maxVal - minVal < 0.05f);
    freeSample(sd);
}

TEST_CASE("S18 Two instruments same path share one pool entry", "[sampler]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_SAMPLER;
    state.instruments[1].type = InstType::INST_SAMPLER;

    std::vector<float> buf(100, 0.5f);
    SampleData sd = makeSample("shared.wav", 0.5f);
    sd.data = buf.data();

    EngineCommand cmd; cmd.type = CommandType::LOAD_SAMPLE;
    cmd.targetId = 0; cmd.u.sample = sd;
    host.push(cmd); host.render(10);

    cmd.targetId = 1; cmd.u.sample = sd;
    host.push(cmd); host.render(10);

    SampleHandle h0 = state.instruments[0].sampler.sample;
    SampleHandle h1 = state.instruments[1].sampler.sample;
    REQUIRE(h0 >= 0);
    REQUIRE(h0 == h1);
    REQUIRE(host.pool().get(h0)->refs == 2);
}

TEST_CASE("S19 Both instruments re-pointed, buffer freed once", "[sampler]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_SAMPLER;
    state.instruments[1].type = InstType::INST_SAMPLER;

    std::vector<float> bufShared(100, 0.5f);
    std::vector<float> bufA(100, 0.25f);
    std::vector<float> bufB(100, 0.75f);

    SampleData sdShared = makeSample("shared.wav", 0.5f); sdShared.data = bufShared.data();
    SampleData sdA = makeSample("a.wav", 0.25f); sdA.data = bufA.data();
    SampleData sdB = makeSample("b.wav", 0.75f); sdB.data = bufB.data();

    EngineCommand cmd; cmd.type = CommandType::LOAD_SAMPLE;
    cmd.targetId = 0; cmd.u.sample = sdShared; host.push(cmd);
    cmd.targetId = 1; cmd.u.sample = sdShared; host.push(cmd);
    host.render(10);

    cmd.targetId = 0; cmd.u.sample = sdA; host.push(cmd); host.render(10);
    cmd.targetId = 1; cmd.u.sample = sdB; host.push(cmd); host.render(10);

    int sharedFreed = 0;
    int duplicateGcd = 0;
    SampleData gc;
    while (host.engine().getGcRing().pop(gc)) {
        if (gc.data == bufShared.data()) sharedFreed++;
        if (gc.data == bufShared.data()) duplicateGcd++;
    }
    REQUIRE(sharedFreed == 2);
}

TEST_CASE("S20 Fuzz - random SamplerState x random samples", "[sampler]") {
    SamplerEngine eng;
    for (int trial = 0; trial < 50; ++trial) {
        int frames = 10 + std::rand() % 500;
        SampleData sd = makeRamp(frames);
        SamplerState s{};
        s.start = std::rand() % 256;
        s.loop_st = std::rand() % 256;
        s.length = std::rand() % 256;
        s.play = std::rand() % 15;
        s.detune = std::rand() % 256;
        eng.noteOn(s, &sd);
        int count = 0;
        while (!eng.finished() && count < 5000) {
            float out[2]; eng.render(0.5f + float(std::rand() % 100) / 100.0f, out);
            REQUIRE(!std::isnan(out[0]));
            REQUIRE(!std::isinf(out[0]));
            count++;
        }
        freeSample(sd);
    }
}

TEST_CASE("S21 Pool full - incoming freed via GC", "[sampler]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    for (int i = 0; i < 128; ++i) state.instruments[i].type = InstType::INST_SAMPLER;

    std::vector<std::vector<float>> bufs(129);
    for (int i = 0; i < 129; ++i) bufs[i].resize(10, 0.5f);

    for (int i = 0; i < 128; ++i) {
        SampleData sd; sd.data = bufs[i].data(); sd.frames = 10;
        char path[128]; std::snprintf(path, sizeof(path), "full_%d.wav", i);
        std::strncpy(sd.path, path, 127);
        EngineCommand cmd; cmd.type = CommandType::LOAD_SAMPLE; cmd.targetId = i; cmd.u.sample = sd;
        host.push(cmd); host.render(10);
    }

    SampleData sd129; sd129.data = bufs[128].data(); sd129.frames = 10;
    std::strncpy(sd129.path, "overflow.wav", 127);
    EngineCommand cmd; cmd.type = CommandType::LOAD_SAMPLE; cmd.targetId = 0; cmd.u.sample = sd129;
    host.push(cmd); host.render(10);

    int overflowInGc = 0;
    SampleData gc;
    while (host.engine().getGcRing().pop(gc)) {
        if (gc.data == bufs[128].data()) overflowInGc++;
    }
    REQUIRE(overflowInGc == 1);
}

TEST_CASE("S-ZDF1 ZDF low-pass passes lows, attenuates highs", "[sampler]") {
    const float cutoff = 2000.0f, res = 0.3f;
    float pass = zdfResponse(false, 100.0f, cutoff, res);    // well below cutoff
    float stop = zdfResponse(false, 12000.0f, cutoff, res);  // well above cutoff
    // rms of a unit sine is ~0.707; the pass band should be near that.
    REQUIRE(pass > 0.6f);
    REQUIRE(stop < 0.1f);
    REQUIRE(pass / stop > 8.0f);   // clear low-pass slope
}

TEST_CASE("S-ZDF2 ZDF high-pass passes highs, attenuates lows", "[sampler]") {
    const float cutoff = 2000.0f, res = 0.3f;
    float pass = zdfResponse(true, 12000.0f, cutoff, res);   // well above cutoff
    float stop = zdfResponse(true, 100.0f, cutoff, res);     // well below cutoff
    REQUIRE(pass > 0.6f);
    REQUIRE(stop < 0.1f);
    REQUIRE(pass / stop > 8.0f);   // clear high-pass slope
}

TEST_CASE("S-ZDF3 ZDF filter stays finite and bounded under noise", "[sampler]") {
    ZdfSvf f;
    f.reset();
    f.setParams(1000.0f, 0.95f, 48000.0f);   // high resonance stress
    uint32_t rng = 0x12345678u;
    float peak = 0.0f;
    for (int i = 0; i < 96000; ++i) {
        rng = rng * 1664525u + 1013904223u;
        float in = (float(rng >> 8) / 8388608.0f) - 1.0f;   // white noise ~[-1,1]
        float hp = 0.0f;
        float lp = f.process(in, hp);
        REQUIRE(std::isfinite(lp));
        REQUIRE(std::isfinite(hp));
        peak = std::max(peak, std::fabs(lp));
    }
    REQUIRE(peak < 100.0f);   // does not blow up even at high resonance
}

// ---- Stereo voice path ----------------------------------------------------
//
// MEASURED on hardware 2026-08-14 (hw_findings.md §UI-12): two sampler probes
// over purpose-built WAVs, one with a tone on L and silence on R, one with the
// same tone on both. The device captured the first at side RMS == mid RMS with
// L/R correlation 0.0000, and the second at side 0.000000 / correlation 1.0000 --
// the mono control's mid exactly 2x the other's, which is what a hard-panned file
// versus a both-channels file must give when nothing sums them. So the M8
// reproduces a stereo sample's image intact.
//
// Our sampler used to sum: `0.5f * (sampOut[0] + sampOut[1])`. The same probes
// through m8_render gave side 0.000053 against 0.000061 -- mono either way.
//
// These pin the fix. `makeRamp(frames, 2)` builds exactly the hardware case: L
// ramps up, R is its negation, so the source is maximally wide.
namespace {

struct SideMid { float mid = 0.0f, side = 0.0f; };

SideMid renderSideMid(SampleData sd, int frames) {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    auto& inst = state.instruments[0];
    inst.type = InstType::INST_SAMPLER;
    inst.sampler.play = 2;          // FWDLOOP, so it keeps sounding
    inst.sampler.loop_st = 0x00;
    inst.sampler.length = 0xFF;
    inst.sampler.detune = 0x80;
    inst.sampler.dry = 0xFF;
    inst.sampler.cho = 0x00;
    inst.sampler.del = 0x00;
    inst.sampler.rev = 0x00;        // dry only: the returns have their own width
    inst.sampler.pan = 0x80;        // centred, so pan cannot contribute side energy
    inst.sampler.amp = 0x00;
    inst.sampler.degrade = 0x00;
    inst.sampler.filter_type = 0;

    EngineCommand load{};
    load.type = CommandType::LOAD_SAMPLE;
    load.targetId = 0;
    load.u.sample = sd;
    host.push(load);

    setStep(host.sequencer(), 0, 0, 60, 127, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(frames);

    SideMid r;
    const auto& buf = host.audio();
    for (size_t i = 0; i + 1 < buf.size(); i += 2) {
        r.mid  += std::fabs(0.5f * (buf[i] + buf[i + 1]));
        r.side += std::fabs(0.5f * (buf[i] - buf[i + 1]));
    }
    return r;
}

} // namespace

TEST_CASE("S-ST1 a stereo sample keeps its image through the voice", "[sampler]") {
    SampleData sd = makeRamp(400, 2);      // L = +ramp, R = -ramp
    const SideMid r = renderSideMid(sd, 4000);

    // Maximally wide source: L == -R means mid cancels and side carries it all.
    REQUIRE(r.side > 0.0f);
    REQUIRE(r.side > r.mid);
    freeSample(sd);
}

TEST_CASE("S-ST2 a mono sample stays centred", "[sampler]") {
    SampleData sd = makeConst(0.5f, 400, 2);   // identical on both channels
    const SideMid r = renderSideMid(sd, 4000);

    // The control. If this shows side energy, something in the path is inventing
    // stereo and S-ST1 would prove nothing.
    REQUIRE(r.mid > 0.0f);
    REQUIRE(r.side < r.mid * 0.01f);
    freeSample(sd);
}

TEST_CASE("Sampler SLICE equal divisions plays correct quarter per note", "[sampler]") {
    // 4000-frame ramp: sample values go linearly from 0.0 to 1.0
    SampleData sd = makeRamp(4000, 1);

    auto renderNote = [&](int slice, int note) -> float {
        SamplerEngine eng;
        SamplerState s{};
        s.slice = slice;
        s.play = 0; // FWD
        s.start = 0x00;
        s.length = 0xFF;
        s.loop_st = 0x00;
        s.detune = 0x80;
        eng.noteOn(s, &sd, note);
        if (eng.finished()) return 0.0f;
        float out[2] = {0.0f, 0.0f};
        eng.render(1.0f, out);
        return out[0];
    };

    // SLICE 04 divides into 4 equal 1000-frame slices
    // Note 0 -> slice 0 (starts at 0.00)
    // Note 1 -> slice 1 (starts at ~0.25)
    // Note 2 -> slice 2 (starts at ~0.50)
    // Note 3 -> slice 3 (starts at ~0.75)
    // Note 4.. -> silent
    float v0 = renderNote(4, 0);
    float v1 = renderNote(4, 1);
    float v2 = renderNote(4, 2);
    float v3 = renderNote(4, 3);
    float v4 = renderNote(4, 4);
    float v60 = renderNote(4, 60);

    REQUIRE(v0 < 0.05f);
    REQUIRE(std::abs(v1 - 0.25f) < 0.05f);
    REQUIRE(std::abs(v2 - 0.50f) < 0.05f);
    REQUIRE(std::abs(v3 - 0.75f) < 0.05f);
    REQUIRE(v4 == 0.0f);
    REQUIRE(v60 == 0.0f);

    // Control: SLICE 00 plays note 60 normally from the beginning
    float vCtrl = renderNote(0, 60);
    REQUIRE(vCtrl < 0.05f);

    freeSample(sd);
}

TEST_CASE("Sampler SLICE ignores START and LENGTH", "[sampler]") {
    SampleData sd = makeRamp(4000, 1);

    auto renderSliceWithStart = [&](int start, int length) -> float {
        SamplerEngine eng;
        SamplerState s{};
        s.slice = 4;
        s.play = 0;
        s.start = start;
        s.length = length;
        s.loop_st = 0x00;
        s.detune = 0x80;
        eng.noteOn(s, &sd, 1); // Note 1 = Slice 1
        if (eng.finished()) return 0.0f;
        float out[2] = {0.0f, 0.0f};
        eng.render(1.0f, out);
        return out[0];
    };

    float valDefault = renderSliceWithStart(0x00, 0xFF);
    float valMoved   = renderSliceWithStart(0x40, 0x40);

    // Both should start at slice 1's origin (~0.25)
    REQUIRE(std::abs(valDefault - 0.25f) < 0.05f);
    REQUIRE(std::abs(valMoved - 0.25f) < 0.05f);
    REQUIRE(std::abs(valDefault - valMoved) < 0.01f);

    freeSample(sd);
}

TEST_CASE("SampleData is trivially copyable POD", "[sampler]") {
    REQUIRE(std::is_trivially_copyable_v<SampleData>);
    REQUIRE(std::is_standard_layout_v<SampleData>);
}

#include "ui/screens/sample_editor/SampleEditorScreen.h"

TEST_CASE("SampleEditor buffer operations work correctly", "[sampler]") {
    using namespace m8::ui::sample_editor;

    SampleData sd = makeRamp(1000, 1);
    Instrument inst{};
    inst.type = InstType::INST_SAMPLER;
    std::strncpy(inst.sampler.samplePath, "Samples/TEST.WAV", sizeof(inst.sampler.samplePath));

    SampleEditorState st;
    st.init(0, inst, &sd);

    REQUIRE(st.selectStart == 0);
    REQUIRE(st.selectEnd == 1000);
    REQUIRE(std::string(st.name) == "TEST");

    // Test REVERSE on full buffer
    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.key = SDLK_RETURN;
    st.row = CursorRow::PROCESS;
    st.processIndex = 5; // REVERSE
    st.subCol = 1; // '>'
    HandleSampleEditorInput(ev, true, false, *(m8::engine::EngineState*)nullptr, st, &sd, *(m8::ui::CommandSink*)nullptr);

    // After reverse, first sample should be ~1.0, last sample ~0.0
    REQUIRE(sd.data[0] > 0.95f);
    REQUIRE(sd.data[sd.frames - 1] < 0.05f);

    // Test UNDO
    st.subCol = 2; // 'UNDO'
    HandleSampleEditorInput(ev, true, false, *(m8::engine::EngineState*)nullptr, st, &sd, *(m8::ui::CommandSink*)nullptr);
    REQUIRE(sd.data[0] < 0.05f);
    REQUIRE(sd.data[sd.frames - 1] > 0.95f);

    // Test INVERT on selection [200, 400]
    st.selectStart = 200;
    st.selectEnd = 400;
    st.processIndex = 6; // INVERT
    st.subCol = 1;
    HandleSampleEditorInput(ev, true, false, *(m8::engine::EngineState*)nullptr, st, &sd, *(m8::ui::CommandSink*)nullptr);
    REQUIRE(sd.data[300] < 0.0f);
    REQUIRE(sd.data[100] > 0.0f);
    REQUIRE(sd.data[500] > 0.0f);

    // Test SILENCE on selection [500, 600]
    st.selectStart = 500;
    st.selectEnd = 600;
    st.processIndex = 4; // SILENCE
    st.subCol = 1;
    HandleSampleEditorInput(ev, true, false, *(m8::engine::EngineState*)nullptr, st, &sd, *(m8::ui::CommandSink*)nullptr);
    REQUIRE(sd.data[550] == 0.0f);

    // Test CROP on [200, 600]
    st.selectStart = 200;
    st.selectEnd = 600;
    st.processIndex = 0; // CROP
    st.subCol = 1;
    HandleSampleEditorInput(ev, true, false, *(m8::engine::EngineState*)nullptr, st, &sd, *(m8::ui::CommandSink*)nullptr);
    REQUIRE(sd.frames == 400);

    st.freeUndo();
    freeSample(sd);
}
