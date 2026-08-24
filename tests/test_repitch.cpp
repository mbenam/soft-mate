// ===========================================================================
// test_repitch.cpp — the REPITCH loop length, pinned to a hardware measurement.
//
// PLAY 09-0B stretch the sample so one pass lasts a musical duration set by the
// STEPS byte. The engine had that duration 2.67x too long, and nothing caught
// it because nothing knew what the right answer was.
//
// MEASURED on hardware 2026-08-24 (fw 6.5.2, COM3, instrument 09 in REPITCH,
// keyjazz C-4, m8_capture, period averaged over ~20 repeats):
//
//   STEPS  BPM   period       in beats
//   0x40   140   5140.4 smp   0.2499
//   0x40    90   7996.0 smp   0.2499
//   0x80   140  10157.8 smp   0.4938
//
// Linear in STEPS, inversely proportional to BPM, and the loop is STEPS/256 of
// a BEAT -- so the default 0x80 is an eighth note. This is a *measurement*, not
// a reference reading, so the test asserts the number rather than a shape.
// ===========================================================================

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <vector>

#include "support/OfflineHost.h"

using namespace m8::test;
using namespace m8::engine;

namespace {

// A pure tone in the sample. REPITCH stretches the sample to the loop length,
// so the pitch it comes out at tells you the loop length directly:
//
//     ratio       = sampleFrames / loopSamples
//     f_out       = f_in * ratio
//     loopSamples = sampleFrames * f_in / f_out
//
// Onsets were tried first and fought the sequencer the whole way. A single
// click can only be measured loop-to-loop, and at STEPS 0x80 the pass is longer
// than the note so it never loops. Two clicks measure inside one pass, but the
// frame-0 one is missed as the voice opens, so the spacing came out 0.75 of a
// period and every case read 3x high. Pitch needs no onsets, no loop and no
// note length -- just a window of steady tone.
constexpr float kToneHz = 1000.0f;
constexpr int   kSampleFrames = 9600;

std::vector<float> toneBuf(int frames) {
    std::vector<float> b(frames);
    for (int i = 0; i < frames; ++i)
        b[i] = 0.8f * std::sin(6.2831853f * kToneHz * float(i) / 48000.0f);
    return b;
}

// Zero-crossing rate over a steady window well inside the first pass.
double measuredPeriod(const std::vector<float>& a) {
    const size_t n = a.size() / 2;
    // Skip the attack, then take a window short enough to stay inside one pass
    // at every tempo tested.
    const size_t from = 600, to = std::min<size_t>(n, 3000);
    if (to <= from + 100) return 0.0;
    int crossings = 0;
    for (size_t i = from + 1; i < to; ++i)
        if ((a[(i - 1) * 2] < 0.0f) != (a[i * 2] < 0.0f)) ++crossings;
    if (crossings < 4) return 0.0;
    const double seconds = double(to - from) / 48000.0;
    const double fOut = (crossings / 2.0) / seconds;
    if (fOut <= 0.0) return 0.0;
    return double(kSampleFrames) * double(kToneHz) / fOut;
}

double renderPeriod(int stepsByte, int bpm) {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();

    // Tempo goes through UPDATE_PARAM, not by writing state.bpm -- recalcBPM()
    // is what turns it into samplesPerTick, and a bare field write never calls
    // it. The first version of this test set the field and every case came back
    // with the same period, which read as "the formula ignores tempo" when it
    // was the test that did.
    {
        EngineCommand t{};
        t.type = CommandType::UPDATE_PARAM;
        t.paramId = ParamID::BPM_INT;
        t.value = bpm;
        host.push(t);
    }

    auto& inst = state.instruments[0];
    inst.type = InstType::INST_SAMPLER;
    inst.sampler.play    = 9;            // REPITCH
    inst.sampler.detune  = stepsByte;    // STEPS
    inst.sampler.volume  = 0x40;
    inst.sampler.dry     = 0xFF;
    inst.sampler.pan     = 0x80;
    inst.sampler.lim     = 0;
    inst.sampler.filter_type = 0;
    inst.sampler.start   = 0x00;
    inst.sampler.loop_st = 0x00;
    inst.sampler.length  = 0xFF;

    static std::vector<float> buf;       // outlives the render
    buf = toneBuf(kSampleFrames);
    SampleData sd{};
    sd.data = buf.data();
    sd.frames = static_cast<int>(buf.size());
    sd.channels = 1;
    sd.sampleRate = 48000;
    std::strncpy(sd.path, "repitch_click.wav", 127);

    EngineCommand cmd{};
    cmd.type = CommandType::LOAD_SAMPLE;
    cmd.targetId = 0;
    cmd.u.sample = sd;
    host.push(cmd);

    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));

    // Render less than one pass of the 16-step phrase, or the phrase wraps and
    // retriggers the note -- and then the spacing measured is the ROW rate, not
    // the loop.
    //
    // That is not hypothetical: STEPS 0x40 is a quarter of a beat, which is
    // exactly one row, so the two rates coincide and the first version of this
    // test "passed" twice while measuring the wrong thing entirely. 1.2 s is
    // under one pass at every tempo used here (16 rows is 1.71 s at 140 BPM,
    // longer still below it).
    host.render(static_cast<int>(48000 * 1.2));
    return measuredPeriod(host.audio());
}

// STEPS/256 of a beat, in samples at 48 kHz.
double expectedPeriod(int stepsByte, int bpm) {
    return (double(stepsByte) / 256.0) * (60.0 / double(bpm)) * 48000.0;
}

} // namespace

TEST_CASE("REPITCH loop length is STEPS/256 of a beat", "[sampler][repitch]") {
    struct Case { int steps; int bpm; };
    const Case cases[] = { {0x40, 140}, {0x40, 90}, {0x80, 140}, {0x80, 120} };

    for (const Case& c : cases) {
        const double got = renderPeriod(c.steps, c.bpm);
        const double want = expectedPeriod(c.steps, c.bpm);
        INFO("STEPS 0x" << std::hex << c.steps << std::dec << " @ " << c.bpm
             << " BPM: got " << got << " want " << want);
        REQUIRE(got > 0.0);
        // 5%: comfortably tighter than the 2.67x error this replaced, and loose
        // enough for onset picking on a click that the resampler has smeared.
        CHECK(std::fabs(got - want) / want < 0.05);
    }
}

TEST_CASE("REPITCH period scales with STEPS and inversely with tempo",
          "[sampler][repitch]") {
    // The two axes the hardware sweep established, asserted as ratios so they
    // survive any later change to the absolute constant.
    const double a = renderPeriod(0x40, 140);
    const double b = renderPeriod(0x80, 140);
    const double c = renderPeriod(0x40, 70);
    REQUIRE(a > 0.0);
    REQUIRE(b > 0.0);
    REQUIRE(c > 0.0);

    INFO("STEPS doubling: " << b / a << " (want 2)");
    CHECK(std::fabs(b / a - 2.0) < 0.1);

    INFO("tempo halving: " << c / a << " (want 2)");
    CHECK(std::fabs(c / a - 2.0) < 0.1);
}
