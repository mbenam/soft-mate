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
    for (int i = 0; i < frames; ++i) {
        float v = (channels == 1) ? float(i) / float(frames - 1) : 0.0f;
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
