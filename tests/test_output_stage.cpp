// Shared per-voice output stage: SynthVoice::applyAmpLimFilter (AMP drive ->
// LIM waveshaper -> FILTER, with the LIM 04-08 "POST" ordering flip) and
// SynthVoice::applyDegrade (sample-and-hold decimator).
//
// These two helpers were extracted from five near-identical inline copies
// (ARCHITECTURE.md §5.2 #8). The extraction is meant to be behaviour-
// preserving, so the tests here do not re-measure the DSP -- they pin the
// wiring, which is what a deduplication can plausibly break:
//
//   OS1  the POST ordering branch still exists and is reachable
//   OS2  WavSynth's filter modes 8-11 still map to "no output-stage filter"
//   OS3  every instrument type that shares the stage actually calls it
//   OS4  DEGRADE is still applied on both paths that own a copy of it
//
// NOTE: the macrosyn path deliberately keeps its own inline chain and is NOT
// covered by OS1-OS3 -- see the comment at its call site in SynthVoice.cpp for
// the four ways it diverges. OS4 does cover its DEGRADE, which was folded in.

#include <catch2/catch_test_macros.hpp>
#include "support/OfflineHost.h"
#include <cmath>
#include <cstring>
#include <vector>

using namespace m8::test;
using namespace m8::engine;

namespace {

// Sum of |sample| over a short render. Renders are deterministic (no std::rand
// in the engine; the LFO's xorshift is seeded in trigger()), so two runs of the
// same patch give the same number to the bit.
float renderSum(const std::vector<float>& audio) {
    float sum = 0.0f;
    for (float v : audio) sum += std::abs(v);
    return sum;
}

// NOTE 2026-08-19: the `amp` parameter below sets each instrument's VOLUME
// byte, not its AMP byte. The output-stage gain has always been driven by
// volume -- SongIO was loading `amp` FROM `volume`, so the whole chain was
// merely misnamed (AGENTS.md 7). With the byte map corrected, AMP is a separate
// control that this engine does not apply, so these tests must drive `volume`
// or they measure nothing. The assertions below are unchanged.
//
// The other suites' 'make it audible' knobs were repointed at `.volume` on
// 2026-08-20 (test_audio, test_eq, test_fmsynth, test_hypersynth,
// test_macrosynth, test_mixer_bus). They had gone inert -- setting `.amp`
// applies nothing -- and would have shifted under them the moment AMP is
// modelled as a drive. The `.amp = 0x00` sites were left as they are: zero is
// 'no drive', which stays correct under either model. Persistence and IO tests
// set `.amp` on purpose -- they check the byte map, not the level.

// A 4-operator patch loud enough that the limiter genuinely engages, which is
// what makes the AMP/LIM/FILTER ordering observable at all.
void setupFM(EngineState& state, int amp, int lim, int filterType) {
    state.instruments[0].type = InstType::INST_FMSYNTH;
    auto& fm = state.instruments[0].fm;
    fm.algo = 0;
    for (int i = 0; i < 4; ++i) {
        fm.ops[i].shape = 0;
        fm.ops[i].level = 0x80;
        fm.ops[i].ratio = i + 1;
    }
    fm.volume = amp;   // the output-stage gain knob -- see note above
    fm.lim = lim;
    fm.filter_type = filterType;
    fm.cutoff = 0x60;
    fm.res = 0x20;
    fm.dry = 0xC0;
    fm.pan = 0x80;
}

void setupWav(EngineState& state, int amp, int lim, int filterType) {
    state.instruments[0].type = InstType::INST_WAVSYNTH;
    auto& ws = state.instruments[0].wav;
    // Sine, not a pulse: a full-scale pulse is already at +/-1, so the hard
    // clamp swallows an AMP increase almost entirely and OS3's margin
    // collapses to the handful of interpolated transition samples. A sine
    // starts below the clamp everywhere except its peaks.
    ws.shape = 6;
    ws.size = 0x80;
    ws.mult = 0x00;
    ws.warp = 0x80;
    ws.scan = 0x00;
    ws.volume = amp;   // the output-stage gain knob -- see note above
    ws.lim = lim;
    ws.filter_type = filterType;
    ws.cutoff = 0x60;
    ws.res = 0x20;
    ws.dry = 0xC0;
    ws.pan = 0x80;
}

void setupHyper(EngineState& state, int amp, int lim, int filterType) {
    state.instruments[0].type = InstType::INST_HYPERSYN;
    auto& h = state.instruments[0].hyper;
    h.swarm = 0x80;
    h.width = 0x80;
    h.subosc = 0x40;
    h.volume = amp;   // the output-stage gain knob -- see note above
    h.lim = lim;
    h.filter_type = filterType;
    h.cutoff = 0x60;
    h.res = 0x20;
    h.dry = 0xC0;
    h.pan = 0x80;
}

std::vector<float> renderOneNote(void (*setup)(EngineState&, int, int, int),
                                 int amp, int lim, int filterType, int frames = 1500) {
    OfflineHost host;
    setup(host.engine().getStateForInit(), amp, lim, filterType);
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(frames);
    return host.audio();
}

bool sameSamples(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return false;
    bool identical = true;                 // accumulate, assert once (AGENTS.md §2)
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) { identical = false; break; }
    return identical;
}

} // namespace

TEST_CASE("OS1 LIM POST modes filter before AMP, non-POST after", "[output_stage]") {
    // LIM 00 (CLIP) and LIM 04 (POST) are the SAME transfer curve -- a hard
    // clamp to +/-1. The only thing that separates them is where the AMP gain
    // and that clamp sit relative to the filter. So with a filter engaged and
    // an AMP high enough to clip, any difference in output is ordering and
    // nothing else. If applyAmpLimFilter ever loses its `limMode < 4` branch,
    // these two collapse onto each other and this goes red.
    auto clipFirst = renderOneNote(setupFM, 0xFF, /*lim=*/0, /*filter=*/1);
    auto filterFirst = renderOneNote(setupFM, 0xFF, /*lim=*/4, /*filter=*/1);

    REQUIRE(clipFirst.size() == filterFirst.size());
    REQUIRE_FALSE(sameSamples(clipFirst, filterFirst));
}

TEST_CASE("OS2 WavSynth filter modes 8-11 skip the output-stage filter", "[output_stage]") {
    // WAV filter modes 8-11 are applied into the wavetable buffer, not at the
    // output stage, so the stage is handed filter type 0. With no filter in the
    // chain, the POST and non-POST orderings reduce to the same three
    // operations (LIM 00 and LIM 04 are both a hard clamp), and the two renders
    // must agree exactly.
    //
    // Passing ws.filter_type (8) straight through instead of the mapped 0 would
    // reach applyFilter's fall-through: it returns the sample unchanged but
    // still runs m_filter.Process() first, and m_filter is the same SVF the
    // in-buffer WAV filtering uses -- so the filter state would advance from a
    // different input in each ordering and the renders would drift apart.
    auto limClip = renderOneNote(setupWav, 0x80, /*lim=*/0, /*filter=*/8);
    auto limPost = renderOneNote(setupWav, 0x80, /*lim=*/4, /*filter=*/8);

    REQUIRE(limClip.size() == limPost.size());
    REQUIRE(sameSamples(limClip, limPost));
}

TEST_CASE("OS3 AMP drive reaches every type sharing the output stage", "[output_stage]") {
    // One case per instrument type routed through applyAmpLimFilter. Raising
    // AMP with no filter and a hard clamp is monotone per sample, so the summed
    // magnitude must rise. A type that stopped calling the shared helper would
    // simply ignore AMP and fail here.
    struct Case { const char* name; void (*setup)(EngineState&, int, int, int); };
    const Case cases[] = {
        {"FMSynth",   setupFM},
        {"WavSynth",  setupWav},
        {"HyperSynth", setupHyper},
    };

    for (const auto& c : cases) {
        DYNAMIC_SECTION(c.name) {
            float quiet = renderSum(renderOneNote(c.setup, 0x00, 0, 0));
            float loud  = renderSum(renderOneNote(c.setup, 0x40, 0, 0));
            REQUIRE(loud > quiet);
        }
    }
}

TEST_CASE("OS4 DEGRADE is applied on the sampler and macrosyn paths", "[output_stage]") {
    SECTION("macrosyn") {
        auto render = [](int degrade) -> float {
            OfflineHost host;
            auto& state = host.engine().getStateForInit();
            state.instruments[0].type = InstType::INST_MACROSYN;
            auto& m = state.instruments[0].macrosyn;
            m.shape = 0x00;
            m.timbre = 0x80;
            m.color = 0x80;
            m.degrade = degrade;
            m.amp = 0x20;
            m.lim = 0;
            m.filter_type = 0;
            m.dry = 0xC0;
            m.pan = 0x80;
            setStep(host.sequencer(), 0, 0, 60, 100, 0);
            host.push(playPhrase(0, 0, 0));
            host.render(1500);
            return renderSum(host.audio());
        };
        REQUIRE(render(0x00) != render(0xC0));
    }

    SECTION("sampler") {
        // Alternating +/-0.6 is the worst case for a sample-and-hold decimator:
        // holding any value at all changes the waveform, so DEGRADE cannot be a
        // no-op here.
        auto render = [](int degrade) -> float {
            // Declared before the host so it outlives the pool slot that points
            // at it (locals are destroyed in reverse order).
            std::vector<float> buf(512);
            for (size_t i = 0; i < buf.size(); ++i) buf[i] = (i % 2 == 0) ? 0.6f : -0.6f;

            OfflineHost host;
            auto& state = host.engine().getStateForInit();
            state.instruments[0].type = InstType::INST_SAMPLER;
            auto& s = state.instruments[0].sampler;
            s.play = 2;            // FWD LOOP -- keeps sounding for the whole render
            s.start = 0x00;
            s.loop_st = 0x00;
            s.length = 0xFF;
            s.detune = 0x80;
            s.degrade = degrade;
            s.amp = 0x00;
            s.lim = 0;
            s.filter_type = 0;
            s.dry = 0xC0;
            s.pan = 0x80;

            SampleData sd{};
            sd.data = buf.data();
            sd.frames = static_cast<uint32_t>(buf.size());
            sd.channels = 1;
            sd.sampleRate = 48000;
            std::strncpy(sd.path, "os_degrade.wav", sizeof(sd.path) - 1);

            EngineCommand load{};
            load.type = CommandType::LOAD_SAMPLE;
            load.targetId = 0;
            load.u.sample = sd;
            host.push(load);
            host.render(16);       // let the audio thread install it

            setStep(host.sequencer(), 0, 0, 60, 100, 0);
            host.push(playPhrase(0, 0, 0));
            host.render(1500);
            return renderSum(host.audio());
        };
        REQUIRE(render(0x00) != render(0xC0));
    }
}
