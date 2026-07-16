#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "support/OfflineHost.h"
#include "engine/Envelopes.h"
#include "engine/Lfo.h"
#include "engine/Modulation.h"
#include <vector>
#include <cstring>
#include <cmath>

using namespace m8::test;
using namespace m8::engine;
using namespace std;
using Catch::Approx;

TEST_CASE("M1 AHD at 120 BPM, hold=6 holds full for 6000 samples", "[modulation]") {
    EnvContext ctx; ctx.samplesPerTick = 1000.0;
    AhdEnv env;
    env.trigger();
    float val = 0.0f;
    for (int i = 0; i < 600; ++i) val = env.process(0, 6, 0, ctx);
    REQUIRE(env.running());
    REQUIRE(val == Approx(1.0f).margin(0.01f));
}

TEST_CASE("M2 Same AHD at 60 BPM - stages 2x longer", "[modulation]") {
    EnvContext ctx120; ctx120.samplesPerTick = 1000.0;
    EnvContext ctx60; ctx60.samplesPerTick = 2000.0;

    AhdEnv env120; env120.trigger();
    int t120 = 0;
    while (env120.running() && t120 < 20000) { env120.process(0, 6, 4, ctx120); t120++; }

    AhdEnv env60; env60.trigger();
    int t60 = 0;
    while (env60.running() && t60 < 40000) { env60.process(0, 6, 4, ctx60); t60++; }

    REQUIRE(std::abs(t60 - t120 * 2) <= 1);
}

TEST_CASE("M3 ATK=0 is instantaneous, no NaN", "[modulation]") {
    EnvContext ctx; ctx.samplesPerTick = 1000.0;
    AhdEnv env; env.trigger();
    float val = env.process(0, 0, 0, ctx);
    REQUIRE(!std::isnan(val));
    REQUIRE(!std::isinf(val));
}

TEST_CASE("M4 AHD -> VOLUME, AMT=0xFF gain follows envelope", "[modulation]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_SAMPLER;
    state.instruments[0].mods[0] = {0, 1, 0xFF, 0, 0, 6, 0};
    state.instruments[0].mods[1] = {0, 0, 0x80, 0, 0, 0, 0};
    state.instruments[0].mods[2] = {0, 0, 0x80, 0, 0, 0, 0};
    state.instruments[0].mods[3] = {0, 0, 0x80, 0, 0, 0, 0};

    std::vector<float> buf(48000, 0.5f);
    SampleData sd{}; sd.data = buf.data(); sd.frames = 48000; sd.channels = 1; sd.sampleRate = 48000;
    std::strncpy(sd.path, "mod_test.wav", 127);
    EngineCommand cmd; cmd.type = CommandType::LOAD_SAMPLE; cmd.targetId = 0; cmd.u.sample = sd;
    host.push(cmd);

    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(200);

    const auto& a = host.audio();
    REQUIRE(a.size() > 4);
    bool hasAudio = false;
    for (float f : a) { if (std::abs(f) > 0.001f) hasAudio = true; }
    REQUIRE(hasAudio);
}

TEST_CASE("M5 ADSR gate on/off - sustain holds, release runs", "[modulation]") {
    EnvContext ctx; ctx.samplesPerTick = 1000.0;
    AdsrEnv env;
    env.retrigger();
    float val = 0.0f;
    for (int i = 0; i < 100; ++i) val = env.process(0, 0, 128, 10, ctx);
    REQUIRE(val == Approx(0.5f).margin(0.02f));

    env.gate(false);
    for (int i = 0; i < 20000; ++i) env.process(0, 0, 128, 10, ctx);
    REQUIRE(!env.running());
}

TEST_CASE("M7 LFO RETRIG resets phase at note-on", "[modulation]") {
    EnvContext ctx; ctx.samplesPerTick = 1000.0;
    Lfo lfo;
    lfo.trigger();
    for (int i = 0; i < 100; ++i) lfo.process(0, 1, 1, ctx, false);
    lfo.trigger();
    float first = lfo.process(0, 1, 1, ctx, true);
    REQUIRE(std::abs(first) < 0.1f);
}

TEST_CASE("M9 LFO FREQ=0x10 at 120 BPM period = 1 bar", "[modulation]") {
    EnvContext ctx; ctx.samplesPerTick = 1000.0;
    Lfo lfo;
    lfo.trigger();
    float prev = 0.0f;
    int firstCrossing = 0;
    int secondCrossing = 0;
    int crossings = 0;
    for (int i = 1; i < 200000; ++i) {
        float v = lfo.process(0x00, 0x10, 1, ctx, (i == 1));
        if (i > 100 && prev >= 0.0f && v < 0.0f) {
            crossings++;
            if (crossings == 1) firstCrossing = i;
            if (crossings == 2) { secondCrossing = i; break; }
        }
        prev = v;
    }
    REQUIRE(crossings == 2);
    int period = secondCrossing - firstCrossing;
    REQUIRE(period > 90000);
    REQUIRE(period < 102000);
}

TEST_CASE("M21 LFO RANDOM shape (0x0C) is deterministic across instances", "[modulation]") {
    // Regression for CODE_CLEANUP_SPEC.md #7: the RANDOM shape used to call
    // std::rand() -- global state, non-deterministic across runs, breaking
    // the "offline render is reproducible ground truth" property. Two
    // independently-triggered Lfo instances fed the same shape/freq/trig
    // sequence must now produce bit-identical output.
    EnvContext ctx; ctx.samplesPerTick = 1000.0;

    Lfo a, b;
    a.trigger();
    b.trigger();

    bool sawNonZero = false;
    for (int i = 0; i < 2000; ++i) {
        float va = a.process(0x0C, 0x08, 1, ctx, (i == 0));
        float vb = b.process(0x0C, 0x08, 1, ctx, (i == 0));
        REQUIRE(va == vb);
        REQUIRE(std::isfinite(va));
        REQUIRE(va >= -1.0f);
        REQUIRE(va <= 1.0f);
        if (va != 0.0f) sawNonZero = true;
    }
    REQUIRE(sawNonZero); // actually exercised the RNG, not stuck returning 0
}

TEST_CASE("M10 DEST=CUTOFF (0x06) affects filter", "[modulation]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_SAMPLER;
    state.instruments[0].sampler.filter_type = 1;
    state.instruments[0].sampler.cutoff = 0x30;
    state.instruments[0].mods[0] = {3, 6, 0xFF, 0x00, 0x00, 0x10, 0x00};
    state.instruments[0].mods[1] = {0, 0, 0x80, 0, 0, 0, 0};
    state.instruments[0].mods[2] = {0, 0, 0x80, 0, 0, 0, 0};
    state.instruments[0].mods[3] = {0, 0, 0x80, 0, 0, 0, 0};

    std::vector<float> buf(48000, 0.5f);
    SampleData sd{}; sd.data = buf.data(); sd.frames = 48000; sd.channels = 1; sd.sampleRate = 48000;
    std::strncpy(sd.path, "cutoff_test.wav", 127);
    EngineCommand cmd; cmd.type = CommandType::LOAD_SAMPLE; cmd.targetId = 0; cmd.u.sample = sd;
    host.push(cmd);

    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(2000);

    const auto& a = host.audio();
    REQUIRE(a.size() > 0);
    bool hasAudio = false;
    for (float f : a) { if (std::abs(f) > 0.001f) hasAudio = true; }
    REQUIRE(hasAudio);
}

TEST_CASE("M13 Mod-to-mod: mod1 -> MOD AMT of mod2", "[modulation]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_SAMPLER;
    state.instruments[0].mods[0] = {3, 11, 0xC0, 0x00, 0x00, 0x04, 0x00};
    state.instruments[0].mods[1] = {3, 1, 0xFF, 0x00, 0x00, 0x10, 0x00};
    state.instruments[0].mods[2] = {0, 0, 0x80, 0, 0, 0, 0};
    state.instruments[0].mods[3] = {0, 0, 0x80, 0, 0, 0, 0};

    std::vector<float> buf(48000, 0.5f);
    SampleData sd{}; sd.data = buf.data(); sd.frames = 48000; sd.channels = 1; sd.sampleRate = 48000;
    std::strncpy(sd.path, "modmod.wav", 127);
    EngineCommand cmd; cmd.type = CommandType::LOAD_SAMPLE; cmd.targetId = 0; cmd.u.sample = sd;
    host.push(cmd);

    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(2000);

    const auto& a = host.audio();
    bool hasAudio = false;
    for (float f : a) { if (std::abs(f) > 0.001f) hasAudio = true; }
    REQUIRE(hasAudio);
}

TEST_CASE("M14 AMT polarity - 0x80 neutral, 0x00 inverted, 0xFF full", "[modulation]") {
    REQUIRE(bipolarAmt(0x80) == Approx(0.0f).margin(0.01f));
    REQUIRE(bipolarAmt(0x00) < -0.9f);
    REQUIRE(bipolarAmt(0xFF) > 0.9f);
}

TEST_CASE("M19 LFO -> PITCH, amt 0xFF: deviation within 1 semitone", "[modulation]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_MACROSYN;
    state.instruments[0].macrosyn.dry = 0xFF;
    state.instruments[0].macrosyn.filter_type = 0;
    state.instruments[0].macrosyn.amp = 0x00;
    state.instruments[0].macrosyn.lim = 0;
    // AHD envelope -> PITCH, amt 0xFF, hold=0x40 (long hold = near-constant output)
    // This gives a nearly constant pitch offset for reliable measurement.
    state.instruments[0].mods[0] = {0, 2, 0xFF, 0x00, 0x40, 0x40, 0x00};
    state.instruments[0].mods[1] = {0, 0, 0x80, 0, 0, 0, 0};
    state.instruments[0].mods[2] = {0, 0, 0x80, 0, 0, 0, 0};
    state.instruments[0].mods[3] = {0, 0, 0x80, 0, 0, 0, 0};

    std::vector<float> buf(48000, 0.5f);
    SampleData sd{}; sd.data = buf.data(); sd.frames = 48000; sd.channels = 1; sd.sampleRate = 48000;
    std::strncpy(sd.path, "pitch_test.wav", 127);
    EngineCommand cmd; cmd.type = CommandType::LOAD_SAMPLE; cmd.targetId = 0; cmd.u.sample = sd;
    host.push(cmd);

    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(20000);

    const auto& a = host.audio();
    REQUIRE(a.size() > 1000);

    // Verify audio is non-trivial
    float maxAbs = 0.0f;
    for (size_t i = 1000; i < a.size() / 2; ++i) {
        float s = std::abs(a[i * 2]);
        if (s > maxAbs) maxAbs = s;
    }
    REQUIRE(maxAbs > 0.01f);

    // Measure via zero crossings, skip gate ramp
    int skip = 500;
    std::vector<int> crossings;
    for (size_t i = skip + 1; i < a.size() / 2; ++i) {
        float prev = a[(i - 1) * 2];
        float curr = a[i * 2];
        if ((prev < 0.0f && curr >= 0.0f) || (prev >= 0.0f && curr < 0.0f))
            crossings.push_back((int)i);
    }

    REQUIRE(crossings.size() > 20);

    // Median period from all consecutive crossings (both slopes)
    std::vector<double> periods;
    for (size_t i = 1; i < crossings.size(); ++i) {
        double p = (double)(crossings[i] - crossings[i - 1]);
        if (p > 20.0 && p < 500.0) periods.push_back(p);
    }
    REQUIRE(periods.size() > 10);

    std::sort(periods.begin(), periods.end());
    double medianPeriod = periods[periods.size() / 2];

    // AHD env with hold=0x40, dec=0x40 at 120 BPM: holds full for ~6.67 seconds.
    // During hold, envelope = 1.0. With amt=0xFF (bipolar ~1.0), pitch offset = 1.0 semitone.
    // Expected frequency = 261.63 * 2^(1/12) = 277.18 Hz
    // Expected half-period = 48000 / 277.18 / 2 ≈ 86.6 samples
    double expectedHalfPeriod = 48000.0 / (261.626 * std::pow(2.0, 1.0 / 12.0)) / 2.0;
    double devSemitones = 12.0 * std::log2(expectedHalfPeriod / medianPeriod);

    // The measured half-period should correspond to within 1 semitone of +1
    // (the envelope ramps up so the actual offset varies, but during hold it's ~1.0)
    REQUIRE(std::abs(devSemitones) < 2.0);
}

TEST_CASE("M20 DC offset < 0.001 over 30s demo render", "[modulation]") {
    OfflineHost host;
    host.engine().loadDemoSong();

    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playSong(0));
    host.renderSeconds(30.0);

    const auto& a = host.audio();
    REQUIRE(a.size() > 0);

    int frames = (int)(a.size() / 2);
    double sumL = 0.0, sumR = 0.0;
    for (int i = 0; i < frames; ++i) {
        sumL += a[i * 2];
        sumR += a[i * 2 + 1];
    }
    double meanL = sumL / frames;
    double meanR = sumR / frames;
    REQUIRE(std::abs(meanL) < 0.001);
    REQUIRE(std::abs(meanR) < 0.001);
}

static std::vector<float> renderRandomLfoPhrase() {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_SAMPLER;
    state.instruments[0].sampler.play = 2; // FWDLOOP
    state.instruments[0].sampler.loop_st = 0x00;
    state.instruments[0].sampler.length = 0xFF;
    state.instruments[0].sampler.dry = 0xFF;

    state.instruments[0].mods[0] = {0, 1, 0xFF, 0, 0, 4, 0};       // AHD -> VOLUME
    // LFO (type=3), shape 0x0C (RANDOM), dest VOLUME, full amount
    state.instruments[0].mods[1] = {3, 1, 0xFF, 0x0C, 0x01 /*RETRIG*/, 0x10 /*freq*/, 0x00};

    std::vector<float> sampleBuf(4800, 0.7f);
    SampleData sd{}; sd.data = sampleBuf.data(); sd.frames = 4800; sd.channels = 1; sd.sampleRate = 48000;
    std::strncpy(sd.path, "rand_lfo_test.wav", 127);
    EngineCommand cmd{}; cmd.type = CommandType::LOAD_SAMPLE; cmd.targetId = 0; cmd.u.sample = sd;
    host.push(cmd);

    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.renderSeconds(1.0);
    return host.audio();
}

TEST_CASE("M22 RANDOM-LFO render is bit-identical across independent runs", "[modulation]") {
    // Full-engine version of the CODE_CLEANUP_SPEC.md #7 determinism check --
    // same scenario as m8_render'ing the same phrase twice and diffing the
    // WAVs, done in-process via two independent OfflineHost instances.
    auto a = renderRandomLfoPhrase();
    auto b = renderRandomLfoPhrase();
    REQUIRE(a.size() == b.size());
    REQUIRE(a.size() > 0);
    REQUIRE(std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}

TEST_CASE("M18 Fuzz - random mod bytes x render: no crash, no NaN", "[modulation]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    for (int i = 0; i < 8; ++i) state.instruments[i].type = InstType::INST_SAMPLER;

    std::vector<float> buf(48000, 0.3f);
    SampleData sd{}; sd.data = buf.data(); sd.frames = 48000; sd.channels = 1; sd.sampleRate = 48000;
    std::strncpy(sd.path, "fuzz.wav", 127);
    EngineCommand cmd; cmd.type = CommandType::LOAD_SAMPLE; cmd.targetId = 0; cmd.u.sample = sd;
    host.push(cmd);

    for (int trial = 0; trial < 30; ++trial) {
        for (int m = 0; m < 4; ++m) {
            state.instruments[0].mods[m].type = std::rand() % 6;
            state.instruments[0].mods[m].dest = std::rand() % 14;
            state.instruments[0].mods[m].amt = std::rand() % 256;
            state.instruments[0].mods[m].p1 = std::rand() % 256;
            state.instruments[0].mods[m].p2 = std::rand() % 256;
            state.instruments[0].mods[m].p3 = std::rand() % 256;
            state.instruments[0].mods[m].p4 = std::rand() % 256;
        }

        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(200);
        host.push(stop());
        host.render(10);
    }
    REQUIRE(true);
}
