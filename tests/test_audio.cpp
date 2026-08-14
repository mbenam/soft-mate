#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "support/OfflineHost.h"
#include "analysis/AudioMetrics.h"
#include "analysis/Fft.h"
#include "engine/Engine.h"
#include "engine/CommandRing.h"
#include "engine/SamplerEngine.h"
#include "engine/SamplePool.h"
#include <vector>
#include <cmath>
#include <cstring>
#include <iostream>
#include <functional>
#include <memory>

using namespace m8::test;
using namespace m8::engine;
using namespace m8::analysis;
using Catch::Approx;

TEST_CASE("A1 demo song 40s: DC worst < 0.01, crest > 9 dB, 0 clipped, 0 non-finite", "[audio]") {
    OfflineHost host;
    host.engine().loadDemoSong();
    host.push(playSong(0));
    host.renderSeconds(40.0);

    const auto& a = host.audio();
    REQUIRE(a.size() > 0);

    Metrics m = analyze(a.data(), a.size() / 2, 48000);
    REQUIRE(m.dcWorstWindow < 0.01f);
    REQUIRE(m.crestDb > 9.0f);
    REQUIRE(m.clipped == 0);
    REQUIRE(m.nonFinite == 0);
}

TEST_CASE("A2 demo song: longest silence < 0.5 s", "[audio]") {
    OfflineHost host;
    host.engine().loadDemoSong();
    host.push(playSong(0));
    host.renderSeconds(40.0);

    const auto& a = host.audio();
    REQUIRE(a.size() > 0);

    Metrics m = analyze(a.data(), a.size() / 2, 48000);
    REQUIRE(m.longestSilenceSec < 0.5f);
}

TEST_CASE("A3 single sampler note, LFO->PITCH amt 0xFF: pitch within 1 semitone", "[audio]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();

    // Set up instrument 0 as a sampler with a simple sine-like sample
    state.instruments[0].type = InstType::INST_SAMPLER;
    state.instruments[0].sampler.play = 2; // FWDLOOP
    state.instruments[0].sampler.loop_st = 0x00;
    state.instruments[0].sampler.length = 0xFF;
    state.instruments[0].sampler.detune = 0x80;
    state.instruments[0].sampler.dry = 0xFF;
    state.instruments[0].sampler.filter_type = 0;
    state.instruments[0].sampler.amp = 0x00;
    state.instruments[0].sampler.lim = 0;

    // Mod slot 0: AHD -> VOLUME (amp envelope)
    state.instruments[0].mods[0] = {0, 1, 0xFF, 0, 0, 4, 0};
    // Mod slot 1: AHD -> PITCH, amt 0xFF, hold=0x40 (long hold = near-constant pitch offset)
    // During hold, envelope = 1.0. With amt=0xFF (bipolar ~1.0), pitch offset = +1 semitone.
    state.instruments[0].mods[1] = {0, 2, 0xFF, 0x00, 0x40, 0x40, 0x00};
    state.instruments[0].mods[2] = {0, 0, 0x80, 0, 0, 0, 0};
    state.instruments[0].mods[3] = {0, 0, 0x80, 0, 0, 0, 0};

    // Create a constant sample (sine-ish at a known frequency)
    const int sampleFrames = 4800;
    std::vector<float> sampleBuf(sampleFrames);
    for (int i = 0; i < sampleFrames; ++i) {
        sampleBuf[i] = 0.8f * std::sin(6.2831853f * 261.626f * i / 48000.0f);
    }

    SampleData sd{};
    sd.data = sampleBuf.data();
    sd.frames = sampleFrames;
    sd.channels = 1;
    sd.sampleRate = 48000;
    std::strncpy(sd.path, "pitch_test.wav", 127);

    EngineCommand cmd{};
    cmd.type = CommandType::LOAD_SAMPLE;
    cmd.targetId = 0;
    cmd.u.sample = sd;
    host.push(cmd);

    setStep(host.sequencer(), 0, 0, 60, 100, 0); // C-4
    host.push(playPhrase(0, 0, 0));
    host.render(48000); // 1 second

    const auto& a = host.audio();
    REQUIRE(a.size() > 2400);

    // Extract mono from interleaved stereo
    std::vector<float> mono(a.size() / 2);
    for (size_t i = 0; i < mono.size(); ++i) {
        mono[i] = a[i * 2];
    }

    // Measure pitch in the sustain portion (skip first 500 samples for gate ramp)
    // During hold, envelope is constant at 1.0, so pitch offset is constant.
    size_t sustainStart = 500;
    size_t sustainLen = std::min(mono.size() - sustainStart, static_cast<size_t>(24000));
    float measured = pitchHz(mono.data() + sustainStart, sustainLen, 48000, 261.626f);

    // Expected: C-4 = 261.626 Hz. With AHD->PITCH at full amt during hold,
    // pitch offset should be +1 semitone. Expected freq = 261.626 * 2^(1/12) = 277.18 Hz.
    // Measure deviation from nominal (C-4), not from the modulated pitch.
    float ratio = measured / 261.626f;
    float semitoneDev = 12.0f * std::log2(ratio);

    // The measured pitch should be near +1 semitone (the AHD hold value).
    // Tolerance accounts for FFT bin resolution and mod curve shape.
    // The critical assertion: pitch is shifted up by roughly 1 semitone, not 45× (the old bug).
    REQUIRE(std::fabs(semitoneDev - 1.0f) < 2.0f);
}

TEST_CASE("A4 single note, AHD->CUTOFF: centroid at end < centroid at start", "[audio]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();

    // Sampler with noise-like sample (broad spectrum)
    state.instruments[0].type = InstType::INST_SAMPLER;
    state.instruments[0].sampler.play = 2; // FWDLOOP
    state.instruments[0].sampler.loop_st = 0x00;
    state.instruments[0].sampler.length = 0xFF;
    state.instruments[0].sampler.detune = 0x80;
    state.instruments[0].sampler.dry = 0xFF;
    state.instruments[0].sampler.filter_type = 1; // LP
    state.instruments[0].sampler.cutoff = 0x40;
    state.instruments[0].sampler.res = 0x00;
    state.instruments[0].sampler.amp = 0x00;
    state.instruments[0].sampler.lim = 0;

    // Mod slot 0: AHD -> VOLUME (amp envelope)
    state.instruments[0].mods[0] = {0, 1, 0xFF, 0, 0, 8, 0};
    // Mod slot 1: AHD -> CUTOFF, filter closes over time
    state.instruments[0].mods[1] = {0, 6, 0xC0, 0x00, 0x04, 0x08, 0x20};
    state.instruments[0].mods[2] = {0, 0, 0x80, 0, 0, 0, 0};
    state.instruments[0].mods[3] = {0, 0, 0x80, 0, 0, 0, 0};

    // White noise sample
    const int sampleFrames = 48000;
    std::vector<float> sampleBuf(sampleFrames);
    uint32_t s = 12345;
    for (int i = 0; i < sampleFrames; ++i) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        sampleBuf[i] = static_cast<float>(s) / 4294967296.0f;
    }

    SampleData sd{};
    sd.data = sampleBuf.data();
    sd.frames = sampleFrames;
    sd.channels = 1;
    sd.sampleRate = 48000;
    std::strncpy(sd.path, "noise_test.wav", 127);

    EngineCommand cmd{};
    cmd.type = CommandType::LOAD_SAMPLE;
    cmd.targetId = 0;
    cmd.u.sample = sd;
    host.push(cmd);

    setStep(host.sequencer(), 0, 0, 60, 100, 0); // C-4
    host.push(playPhrase(0, 0, 0));
    host.render(48000); // 1 second

    const auto& a = host.audio();
    REQUIRE(a.size() > 4800);

    // Extract mono
    std::vector<float> mono(a.size() / 2);
    for (size_t i = 0; i < mono.size(); ++i) {
        mono[i] = a[i * 2];
    }

    // Measure centroid at note start (skip first 100 samples for gate ramp)
    size_t startBegin = 200;
    size_t startLen = std::min(mono.size() - startBegin, static_cast<size_t>(4800));
    float centroidStart = spectralCentroidHz(mono.data() + startBegin, startLen, 48000);

    // Measure centroid at note end (last 4800 samples)
    size_t endLen = std::min(static_cast<size_t>(4800), mono.size());
    size_t endStart = mono.size() - endLen;
    float centroidEnd = spectralCentroidHz(mono.data() + endStart, endLen, 48000);

    REQUIRE(centroidStart > 0.0f);
    REQUIRE(centroidEnd > 0.0f);

    // Filter closes -> centroid at end should be lower than at start
    REQUIRE(centroidEnd < centroidStart);
}

TEST_CASE("A5 reverb+delay feedback at 0xFF, 30s loud input: no divergence", "[audio]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();

    // Instrument: sampler with noise at full volume, heavy reverb and delay
    state.instruments[0].type = InstType::INST_SAMPLER;
    state.instruments[0].sampler.play = 2; // FWDLOOP
    state.instruments[0].sampler.loop_st = 0x00;
    state.instruments[0].sampler.length = 0xFF;
    state.instruments[0].sampler.detune = 0x80;
    state.instruments[0].sampler.dry = 0xFF;
    state.instruments[0].sampler.del = 0xFF;
    state.instruments[0].sampler.rev = 0xFF;
    state.instruments[0].sampler.filter_type = 0;
    state.instruments[0].sampler.amp = 0x00;
    state.instruments[0].sampler.lim = 0;
    state.instruments[0].sampler.pan = 0x80;

    // Amp envelope: sustain full
    state.instruments[0].mods[0] = {0, 1, 0xFF, 0, 0, 2, 0};
    state.instruments[0].mods[1] = {0, 0, 0x80, 0, 0, 0, 0};
    state.instruments[0].mods[2] = {0, 0, 0x80, 0, 0, 0, 0};
    state.instruments[0].mods[3] = {0, 0, 0x80, 0, 0, 0, 0};

    // Max feedback
    state.effects.del_feedback = 0xFF;
    state.effects.rev_decay = 0xFF;
    state.mixer.del_vol = 0xE0;
    state.mixer.rev_vol = 0xE0;

    // Noise sample
    const int sampleFrames = 48000;
    std::vector<float> sampleBuf(sampleFrames);
    uint32_t s = 42;
    for (int i = 0; i < sampleFrames; ++i) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        sampleBuf[i] = 0.8f * static_cast<float>(s) / 4294967296.0f;
    }

    SampleData sd{};
    sd.data = sampleBuf.data();
    sd.frames = sampleFrames;
    sd.channels = 1;
    sd.sampleRate = 48000;
    std::strncpy(sd.path, "feedback_test.wav", 127);

    EngineCommand cmd{};
    cmd.type = CommandType::LOAD_SAMPLE;
    cmd.targetId = 0;
    cmd.u.sample = sd;
    host.push(cmd);

    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.renderSeconds(30.0);

    const auto& a = host.audio();
    REQUIRE(a.size() > 0);

    Metrics m = analyze(a.data(), a.size() / 2, 48000);
    REQUIRE(m.peak < 1.0f);
    REQUIRE(std::fabs(m.dcL) < 0.005f);
    REQUIRE(std::fabs(m.dcR) < 0.005f);
}

TEST_CASE("S-DET1 detune changes rendered pitch by the right amount", "[audio]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();

    // Set up instrument 0 as a sampler
    state.instruments[0].type = InstType::INST_SAMPLER;
    state.instruments[0].sampler.play = 2; // FWDLOOP
    state.instruments[0].sampler.loop_st = 0x00;
    state.instruments[0].sampler.length = 0xFF;
    state.instruments[0].sampler.detune = 0x80; // In tune
    state.instruments[0].sampler.dry = 0xFF;
    state.instruments[0].sampler.filter_type = 0;
    state.instruments[0].sampler.amp = 0x00;
    state.instruments[0].sampler.lim = 0;

    // Mod slot 0: AHD -> VOLUME (amp envelope) with no decay/hold to keep volume full
    state.instruments[0].mods[0] = {0, 1, 0xFF, 0, 0, 4, 0};
    state.instruments[0].mods[1] = {0, 0, 0x80, 0, 0, 0, 0};
    state.instruments[0].mods[2] = {0, 0, 0x80, 0, 0, 0, 0};
    state.instruments[0].mods[3] = {0, 0, 0x80, 0, 0, 0, 0};

    // Create a pure sine sample at 261.626 Hz (C-4)
    const int sampleFrames = 48000;
    std::vector<float> sampleBuf(sampleFrames);
    for (int i = 0; i < sampleFrames; ++i) {
        sampleBuf[i] = 0.8f * std::sin(6.2831853f * 261.626f * i / 48000.0f);
    }

    SampleData sd{};
    sd.data = sampleBuf.data();
    sd.frames = sampleFrames;
    sd.channels = 1;
    sd.sampleRate = 48000;
    std::strncpy(sd.path, "detune_sine.wav", 127);

    EngineCommand cmd{};
    cmd.type = CommandType::LOAD_SAMPLE;
    cmd.targetId = 0;
    cmd.u.sample = sd;
    host.push(cmd);

    // 1. Render with detune = 0x80 (in tune)
    setStep(host.sequencer(), 0, 0, 60, 100, 0); // C-4
    host.push(playPhrase(0, 0, 0));
    host.render(48000); // 1 second

    const auto& audioA = host.audio();
    REQUIRE(audioA.size() > 2400);

    std::vector<float> monoA(audioA.size() / 2);
    for (size_t i = 0; i < monoA.size(); ++i) {
        monoA[i] = audioA[i * 2];
    }

    float measuredA = pitchHz(monoA.data() + 500, std::min(monoA.size() - 500, static_cast<size_t>(24000)), 48000, 261.626f);
    std::cout << "S-DET1: measured C-4 (detune 0x80) = " << measuredA << " Hz" << std::endl;
    // Assert fundamental is ~261.63 Hz (within 20 cents)
    float centsA = 1200.0f * std::log2(measuredA / 261.626f);
    REQUIRE(std::fabs(centsA) < 20.0f);

    // 2. Render with detune = 0x80 + 16 (C#4, +1 semitone)
    // Clear host to reset audio buffers
    OfflineHost hostB;
    auto& stateB = hostB.engine().getStateForInit();
    stateB.instruments[0] = state.instruments[0];
    stateB.instruments[0].sampler.detune = 0x80 + 16; // C#4

    hostB.push(cmd); // Load the same sample
    setStep(hostB.sequencer(), 0, 0, 60, 100, 0); // C-4
    hostB.push(playPhrase(0, 0, 0));
    hostB.render(48000); // 1 second

    const auto& audioB = hostB.audio();
    std::vector<float> monoB(audioB.size() / 2);
    for (size_t i = 0; i < monoB.size(); ++i) {
        monoB[i] = audioB[i * 2];
    }

    float measuredB = pitchHz(monoB.data() + 500, std::min(monoB.size() - 500, static_cast<size_t>(24000)), 48000, 277.18f);
    std::cout << "S-DET1: measured C#4 (detune 0x90) = " << measuredB << " Hz" << std::endl;
    // Expected frequency = 261.626 * 2^(1/12) = 277.18 Hz
    float centsB = 1200.0f * std::log2(measuredB / 277.18f);
    REQUIRE(std::fabs(centsB) < 20.0f);
}

// Renders a single sampler note through the full engine and returns the
// measured fundamental in Hz. Sample is a pure sine at srcHz.
static float renderSamplerNote(int midiNote, float srcHz, int detune = 0x80) {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_SAMPLER;
    state.instruments[0].sampler.play = 2;      // FWDLOOP
    state.instruments[0].sampler.loop_st = 0x00;
    state.instruments[0].sampler.length = 0xFF;
    state.instruments[0].sampler.detune = detune;
    state.instruments[0].sampler.dry = 0xFF;
    state.instruments[0].sampler.filter_type = 0;
    state.instruments[0].sampler.amp = 0x00;
    state.instruments[0].sampler.lim = 0;
    // AHD->VOLUME held open so the tone sustains for the measurement window.
    state.instruments[0].mods[0] = {0, 1, 0xFF, 0, 0, 4, 0};
    state.instruments[0].mods[1] = {0, 0, 0x80, 0, 0, 0, 0};
    state.instruments[0].mods[2] = {0, 0, 0x80, 0, 0, 0, 0};
    state.instruments[0].mods[3] = {0, 0, 0x80, 0, 0, 0, 0};

    const int sampleFrames = 48000;
    std::vector<float> sampleBuf(sampleFrames);
    for (int i = 0; i < sampleFrames; ++i)
        sampleBuf[i] = 0.8f * std::sin(6.2831853f * srcHz * i / 48000.0f);

    SampleData sd{};
    sd.data = sampleBuf.data();
    sd.frames = sampleFrames;
    sd.channels = 1;
    sd.sampleRate = 48000;
    std::strncpy(sd.path, "note_sine.wav", 127);

    EngineCommand cmd{};
    cmd.type = CommandType::LOAD_SAMPLE;
    cmd.targetId = 0;
    cmd.u.sample = sd;
    host.push(cmd);

    setStep(host.sequencer(), 0, 0, midiNote, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(48000);

    const auto& audio = host.audio();
    std::vector<float> mono(audio.size() / 2);
    for (size_t i = 0; i < mono.size(); ++i) mono[i] = audio[i * 2];

    float expected = srcHz * std::pow(2.0f, (midiNote - 60) / 12.0f);
    return pitchHz(mono.data() + 500,
                   std::min(mono.size() - 500, static_cast<size_t>(24000)),
                   48000, expected);
}

TEST_CASE("S-NOTE1 sampler tracks the played note (C-4 root)", "[audio]") {
    // A 261.626 Hz sample played at C-4 (root) plays back at pitch; played an
    // octave up at C-5 it plays back one octave up. This locks the sampler's
    // note-tracking (kSamplerRootMidi = 60), which was previously absent.
    float c4 = renderSamplerNote(60, 261.626f);
    float c5 = renderSamplerNote(72, 261.626f);
    std::cout << "S-NOTE1: C-4 = " << c4 << " Hz, C-5 = " << c5 << " Hz" << std::endl;

    REQUIRE(std::fabs(1200.0f * std::log2(c4 / 261.626f)) < 25.0f);   // C-4 at pitch
    REQUIRE(std::fabs(1200.0f * std::log2(c5 / 523.251f)) < 25.0f);   // C-5 one octave up
    REQUIRE(std::fabs(1200.0f * std::log2(c5 / c4) - 1200.0f) < 30.0f); // ratio ~2x
}

TEST_CASE("S-LIM-POST POST:AD soft-clips after the filter, bounded and distinct from CLIP", "[audio]") {
    // Build a hot sampler note (full-scale sine amplified past unity) and render
    // it twice: LIM=CLIP (00) and LIM=POST:AD (05). POST:AD (tanh) must stay
    // bounded within [-1,1] and produce a measurably different waveform than the
    // hard clipper, proving the post-filter soft-clip path is wired.
    auto renderWithLim = [](int lim) {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_SAMPLER;
        state.instruments[0].sampler.play = 2;
        state.instruments[0].sampler.loop_st = 0x00;
        state.instruments[0].sampler.length = 0xFF;
        state.instruments[0].sampler.detune = 0x80;
        state.instruments[0].sampler.dry = 0xFF;
        state.instruments[0].sampler.filter_type = 1; // LP present so POST ordering matters
        state.instruments[0].sampler.cutoff = 0xFF;   // wide open — passes the tone
        state.instruments[0].sampler.amp = 0xFF;      // drive hard into the limiter
        state.instruments[0].sampler.lim = lim;
        state.instruments[0].mods[0] = {0, 1, 0xFF, 0, 0, 4, 0};
        state.instruments[0].mods[1] = {0, 0, 0x80, 0, 0, 0, 0};
        state.instruments[0].mods[2] = {0, 0, 0x80, 0, 0, 0, 0};
        state.instruments[0].mods[3] = {0, 0, 0x80, 0, 0, 0, 0};

        const int frames = 48000;
        std::vector<float> buf(frames);
        for (int i = 0; i < frames; ++i)
            buf[i] = 0.8f * std::sin(6.2831853f * 261.626f * i / 48000.0f);
        SampleData sd{};
        sd.data = buf.data(); sd.frames = frames; sd.channels = 1; sd.sampleRate = 48000;
        std::strncpy(sd.path, "hot_sine.wav", 127);
        EngineCommand cmd{}; cmd.type = CommandType::LOAD_SAMPLE; cmd.targetId = 0; cmd.u.sample = sd;
        host.push(cmd);
        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(frames);
        const auto& a = host.audio();
        std::vector<float> mono(a.size() / 2);
        for (size_t i = 0; i < mono.size(); ++i) mono[i] = a[i * 2];
        return mono;
    };

    auto clip = renderWithLim(0);   // CLIP (hard, pre-filter)
    auto post = renderWithLim(5);   // POST:AD (tanh, post-filter)
    REQUIRE(clip.size() == post.size());
    REQUIRE(post.size() > 2400);

    float postPeak = 0.0f, maxDiff = 0.0f;
    for (size_t i = 0; i < post.size(); ++i) {
        postPeak = std::max(postPeak, std::fabs(post[i]));
        maxDiff = std::max(maxDiff, std::fabs(post[i] - clip[i]));
    }
    std::cout << "S-LIM-POST: POST:AD peak = " << postPeak << ", maxDiff vs CLIP = " << maxDiff << std::endl;
    REQUIRE(postPeak <= 1.0001f);   // tanh soft clip stays bounded
    REQUIRE(maxDiff > 0.02f);       // measurably different from hard clip
}

// ---------------------------------------------------------------------------
// A6-A8 -- the effect-return controls that were stored but silent until
// 2026-08-14: STEREO WIDTH on each of the three returns, and the REVERB SEND
// that ModFX and Delay each have.
//
// A6 is the one that protects everything else. All three widths default to
// 0xFF and both reverb sends to 0x00, which is deliberately the identity, so
// adding these must not alter a single sample of an existing song.
// ---------------------------------------------------------------------------

namespace {

// Renders a short burst through a noise sampler with the sends open, and
// returns the interleaved audio. `tweak` gets to change the effects state.
std::vector<float> renderWithEffects(const std::function<void(EffectsState&)>& tweak) {
    static std::vector<float> sampleBuf;
    if (sampleBuf.empty()) {
        sampleBuf.resize(48000);
        uint32_t s = 1234;
        for (size_t i = 0; i < sampleBuf.size(); ++i) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            sampleBuf[i] = 0.5f * static_cast<float>(s) / 4294967296.0f;
        }
    }

    auto host = std::make_unique<OfflineHost>();
    auto& state = host->engine().getStateForInit();
    auto& seq   = host->sequencer();
    seq.clear();

    auto& inst = state.instruments[0];
    inst.type = InstType::INST_SAMPLER;
    inst.sampler.play    = 2;      // FWDLOOP, so it keeps sounding
    inst.sampler.loop_st = 0x00;
    inst.sampler.length  = 0xFF;
    inst.sampler.detune  = 0x80;
    inst.sampler.dry     = 0x40;
    inst.sampler.cho     = 0xFF;   // all three sends open so every return is live
    inst.sampler.del     = 0xFF;
    inst.sampler.rev     = 0x00;   // reverb fed only via the modfx/delay sends
    inst.sampler.pan     = 0x80;
    inst.sampler.amp     = 0x00;
    inst.sampler.filter_type = 0;
    inst.mods[0] = {0, 1, 0xFF, 0, 0, 2, 0};   // AHD -> VOLUME, sustaining

    SampleData sd{};
    sd.data = sampleBuf.data();
    sd.frames = static_cast<uint32_t>(sampleBuf.size());
    sd.channels = 1;
    sd.sampleRate = 48000;
    std::strncpy(sd.path, "noise.wav", sizeof(sd.path) - 1);
    EngineCommand load{};
    load.type = CommandType::LOAD_SAMPLE;
    load.targetId = 0;
    load.u.sample = sd;
    host->push(load);

    tweak(state.effects);

    m8::test::setStep(seq, 0, 0, 60, 0x7F, 0);
    host->push(m8::test::playPhrase(0, 0, 0));
    host->render(24000);
    return host->audio();
}

// Mean |L-R| -- a proxy for how wide the output is.
float meanStereoDiff(const std::vector<float>& a) {
    double acc = 0.0;
    size_t n = a.size() / 2;
    for (size_t i = 0; i < n; ++i) acc += std::fabs(a[i * 2] - a[i * 2 + 1]);
    return n ? static_cast<float>(acc / n) : 0.0f;
}

} // namespace

TEST_CASE("A6 effect-return defaults are the exact identity", "[audio]") {
    // Widths at 0xFF and reverb sends at 0x00 must reproduce the pre-existing
    // signal path bit-for-bit, not approximately. If this fails, adding these
    // controls silently changed the sound of every song that never used them.
    auto base = renderWithEffects([](EffectsState&) {});
    auto same = renderWithEffects([](EffectsState& fx) {
        fx.cho_width = 0xFF; fx.del_width = 0xFF; fx.rev_width = 0xFF;
        fx.cho_reverb = 0x00; fx.del_reverb = 0x00;
    });

    REQUIRE(base.size() == same.size());
    bool identical = true;
    for (size_t i = 0; i < base.size(); ++i)
        if (base[i] != same[i]) { identical = false; break; }
    REQUIRE(identical);
}

TEST_CASE("A7 STEREO WIDTH narrows the returns", "[audio]") {
    auto wide   = renderWithEffects([](EffectsState&) {});          // all 0xFF
    auto narrow = renderWithEffects([](EffectsState& fx) {
        fx.cho_width = 0x00; fx.del_width = 0x00; fx.rev_width = 0x00;
    });

    const float dWide   = meanStereoDiff(wide);
    const float dNarrow = meanStereoDiff(narrow);

    // The dry path is still stereo, so this does not reach zero -- but the
    // returns collapsing to mono has to reduce the channel difference.
    REQUIRE(dWide > 0.0f);
    REQUIRE(dNarrow < dWide);
}

TEST_CASE("A8 ModFX and Delay reach the reverb through their own sends", "[audio]") {
    // The instrument's own reverb send is 0x00 here, so any reverb at all can
    // only have arrived via the modfx/delay REVERB SEND controls.
    auto off = renderWithEffects([](EffectsState& fx) {
        fx.cho_reverb = 0x00; fx.del_reverb = 0x00;
    });
    auto on = renderWithEffects([](EffectsState& fx) {
        fx.cho_reverb = 0xFF; fx.del_reverb = 0xFF;
        fx.rev_decay  = 0xE0;
    });

    REQUIRE(off.size() == on.size());
    float maxDiff = 0.0f;
    for (size_t i = 0; i < off.size(); ++i)
        maxDiff = std::max(maxDiff, std::fabs(off[i] - on[i]));
    REQUIRE(maxDiff > 0.001f);
    // OfflineHost already gates NaN/Inf and |s| <= 1 on every chunk it renders,
    // so there is no per-sample assertion here -- AGENTS.md §2.
}

// ---------------------------------------------------------------------------
// A9-A10 -- reverb ROOM SIZE / MOD DEPTH / MOD FREQ, live since ReverbSc was
// vendored and given setters (see src/engine/ReverbScM8.h).
//
// A9 is the guard, same job A6 does for the returns: the engine's default
// bytes (size 0xFF, mod depth 0x20, mod freq 0xFF) are anchored to the stock
// tuning these had as constants, so an untouched song must render bit-for-bit
// as before.
// ---------------------------------------------------------------------------

TEST_CASE("A9 reverb defaults reproduce the stock tuning exactly", "[audio]") {
    auto base = renderWithEffects([](EffectsState& fx) { fx.rev_decay = 0xE0; });
    auto same = renderWithEffects([](EffectsState& fx) {
        fx.rev_decay     = 0xE0;
        fx.rev_size      = 0xFF;   // -> room scale 1.0
        fx.rev_mod_depth = 0x20;   // -> pitch mod 1.0
        fx.rev_mod_freq  = 0xFF;   // -> mod rate 1.0
    });

    REQUIRE(base.size() == same.size());
    bool identical = true;
    for (size_t i = 0; i < base.size(); ++i)
        if (base[i] != same[i]) { identical = false; break; }
    REQUIRE(identical);
}

TEST_CASE("A10 each reverb control changes the output", "[audio]") {
    // Reverb reached via the modfx/delay sends (the helper leaves the
    // instrument's own reverb send at 0), so the tail is genuinely present.
    auto ref = renderWithEffects([](EffectsState& fx) {
        fx.rev_decay = 0xE0; fx.cho_reverb = 0xFF; fx.del_reverb = 0xFF;
    });

    auto differsFrom = [&](const std::vector<float>& other) {
        REQUIRE(ref.size() == other.size());
        float maxDiff = 0.0f;
        for (size_t i = 0; i < ref.size(); ++i)
            maxDiff = std::max(maxDiff, std::fabs(ref[i] - other[i]));
        return maxDiff;
    };

    auto smallRoom = renderWithEffects([](EffectsState& fx) {
        fx.rev_decay = 0xE0; fx.cho_reverb = 0xFF; fx.del_reverb = 0xFF;
        fx.rev_size = 0x10;
    });
    auto deepMod = renderWithEffects([](EffectsState& fx) {
        fx.rev_decay = 0xE0; fx.cho_reverb = 0xFF; fx.del_reverb = 0xFF;
        fx.rev_mod_depth = 0xC0;
    });
    auto slowMod = renderWithEffects([](EffectsState& fx) {
        fx.rev_decay = 0xE0; fx.cho_reverb = 0xFF; fx.del_reverb = 0xFF;
        fx.rev_mod_freq = 0x08;
    });

    REQUIRE(differsFrom(smallRoom) > 0.001f);
    REQUIRE(differsFrom(deepMod)   > 0.001f);
    REQUIRE(differsFrom(slowMod)   > 0.001f);
}

// A11 -- the scope parameters that drive existing DSP are anchored so their
// defaults reproduce the constants they replaced. ATK 0x00 is the old
// "attack immediately", REL 0x00 is AUTO whose fast end is where the old fixed
// release sat, RES 0x00 is the old 0.3, and OTT TIME/COLOR 0x80 are 100% and
// neutral. A song that never touches them must be unchanged.
TEST_CASE("A11 scope-parameter defaults reproduce the old constants", "[audio]") {
    auto base = renderWithEffects([](EffectsState&) {});
    auto same = renderWithEffects([](EffectsState& fx) {
        fx.ott_time = 0x80; fx.ott_color = 0x80;
    });
    REQUIRE(base.size() == same.size());
    bool identical = true;
    for (size_t i = 0; i < base.size(); ++i)
        if (base[i] != same[i]) { identical = false; break; }
    REQUIRE(identical);
}

// A12 -- SOFT CLIP is a switch now, and its default keeps the old sound.
//
// The engine default is ON, which is deliberately not the device's factory
// default: our songs were authored while the master tanh was unconditional and
// their levels rely on it. So "default" must still mean "exactly as before",
// and "off" must be audibly different rather than a no-op.
TEST_CASE("A12 SOFT CLIP defaults to the old unconditional tanh", "[audio]") {
    OfflineHost host;
    host.engine().loadDemoSong();
    REQUIRE(host.engine().getStateForInit().mixer.soft_clip != 0);
    host.push(playSong(0));
    host.renderSeconds(4.0);
    const auto on = host.audio();

    OfflineHost host2;
    host2.engine().loadDemoSong();
    host2.engine().getStateForInit().mixer.soft_clip = 0;
    host2.push(playSong(0));
    host2.renderSeconds(4.0);
    const auto off = host2.audio();

    REQUIRE(on.size() == off.size());

    // Switching it off has to change the output, and in the direction that
    // makes it louder -- tanh only ever pulls a signal down.
    float maxDiff = 0.0f;
    double sumOn = 0.0, sumOff = 0.0;
    for (size_t i = 0; i < on.size(); ++i) {
        maxDiff = std::max(maxDiff, std::fabs(on[i] - off[i]));
        sumOn  += double(on[i])  * on[i];
        sumOff += double(off[i]) * off[i];
    }
    REQUIRE(maxDiff > 0.01f);
    REQUIRE(sumOff > sumOn);
}

// ---------------------------------------------------------------------------
// A13-A14 -- ModFX is a slot with three algorithms (§UI-7), not just a chorus.
// ---------------------------------------------------------------------------

TEST_CASE("A13 ModFX type 0 is the untouched chorus", "[audio]") {
    // Everything we have authored leaves modfx_type at 0, so introducing the
    // dispatch must not have altered a single sample of the chorus path.
    auto base = renderWithEffects([](EffectsState&) {});
    auto same = renderWithEffects([](EffectsState& fx) { fx.modfx_type = 0; });
    REQUIRE(base.size() == same.size());
    bool identical = true;
    for (size_t i = 0; i < base.size(); ++i)
        if (base[i] != same[i]) { identical = false; break; }
    REQUIRE(identical);
}

TEST_CASE("A14 phaser and flanger are distinct, stable effects", "[audio]") {
    auto chorus  = renderWithEffects([](EffectsState& fx) { fx.modfx_type = 0; });
    auto phaser  = renderWithEffects([](EffectsState& fx) { fx.modfx_type = 1; });
    auto flanger = renderWithEffects([](EffectsState& fx) { fx.modfx_type = 2; });

    REQUIRE(chorus.size() == phaser.size());
    REQUIRE(chorus.size() == flanger.size());

    auto maxDiff = [](const std::vector<float>& a, const std::vector<float>& b) {
        float m = 0.0f;
        for (size_t i = 0; i < a.size(); ++i) m = std::max(m, std::fabs(a[i] - b[i]));
        return m;
    };

    // Each type has to be audibly its own thing -- not just different from the
    // chorus, but from each other.
    REQUIRE(maxDiff(chorus,  phaser)  > 0.001f);
    REQUIRE(maxDiff(chorus,  flanger) > 0.001f);
    REQUIRE(maxDiff(phaser,  flanger) > 0.001f);

    // Both carry feedback, so the thing worth guarding is that neither runs
    // away. OfflineHost already gates NaN/Inf and |s| <= 1 per chunk; this
    // pins that they stay well short of the ceiling on a hot input.
    float pkP = 0.0f, pkF = 0.0f;
    for (float v : phaser)  pkP = std::max(pkP, std::fabs(v));
    for (float v : flanger) pkF = std::max(pkF, std::fabs(v));
    REQUIRE(pkP < 0.99f);
    REQUIRE(pkF < 0.99f);
}
