#include <catch2/catch_test_macros.hpp>
#include "support/OfflineHost.h"
#include <atomic>
#include <cmath>

extern std::atomic<int> g_allocCount;

using namespace m8::test;
using namespace m8::engine;

TEST_CASE("HyperSynth renders chord intervals without NaN/Inf", "[hypersynth]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_HYPERSYN;
    auto& h = state.instruments[0].hyper;

    // Set chord bank 0 to Minor 7th add 9 (0, 3, 7, 10, 14, 17)
    h.chord_bank = 0;
    h.chords[0][0] = 0;
    h.chords[0][1] = 3;
    h.chords[0][2] = 7;
    h.chords[0][3] = 10;
    h.chords[0][4] = 14;
    h.chords[0][5] = 17;
    h.shift = 0x80;
    h.swarm = 0x40;
    h.width = 0x80;
    h.subosc = 0x80;
    h.volume = 0x40;
    h.lim = 0;
    h.filter_type = 0;
    h.dry = 0xC0;
    h.pan = 0x80;

    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(1000);

    const auto& a = host.audio();
    bool hasNonZero = false;
    for (float val : a) {
        REQUIRE(std::isfinite(val));
        REQUIRE(val >= -1.05f);
        REQUIRE(val <= 1.05f);
        if (std::abs(val) > 0.001f) hasNonZero = true;
    }
    REQUIRE(hasNonZero);
}

TEST_CASE("HyperSynth SHIFT cross-fades lower and upper intervals", "[hypersynth]") {
    auto renderWithShift = [](uint8_t shiftVal) -> float {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_HYPERSYN;
        auto& h = state.instruments[0].hyper;
        h.chord_bank = 0;
        h.chords[0][0] = 0;  h.chords[0][1] = 0;  h.chords[0][2] = 0;
        h.chords[0][3] = 24; h.chords[0][4] = 24; h.chords[0][5] = 24;
        h.shift = shiftVal;
        h.swarm = 0;
        h.width = 0;
        h.subosc = 0;
        h.volume = 0x40;
        h.lim = 0;
        h.filter_type = 0;
        h.dry = 0xC0;
        h.pan = 0x80;

        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(1000);

        float sum = 0.0f;
        for (float v : host.audio()) sum += std::abs(v);
        return sum;
    };

    float lowerOnly = renderWithShift(0x00);
    float balanced  = renderWithShift(0x80);
    float upperOnly = renderWithShift(0xFF);

    REQUIRE(lowerOnly > 0.0f);
    REQUIRE(balanced > 0.0f);
    REQUIRE(upperOnly > 0.0f);
    REQUIRE(std::abs(lowerOnly - upperOnly) > 0.0001f);
}

TEST_CASE("HyperSynth SUBOSC octave toggle and level", "[hypersynth]") {
    auto renderWithSub = [](uint8_t subVal) -> float {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_HYPERSYN;
        auto& h = state.instruments[0].hyper;
        h.chord_bank = 0;
        h.shift = 0x80;
        h.swarm = 0;
        h.width = 0;
        h.subosc = subVal;
        h.volume = 0x40;
        h.lim = 0;
        h.filter_type = 0;
        h.dry = 0xC0;
        h.pan = 0x80;

        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(1000);

        float sum = 0.0f;
        for (float v : host.audio()) sum += std::abs(v);
        return sum;
    };

    float noSub = renderWithSub(0x00);
    float sub2Oct = renderWithSub(0x40);
    float sub1Oct = renderWithSub(0xC0);

    REQUIRE(sub2Oct != noSub);
    REQUIRE(sub1Oct != noSub);
    REQUIRE(sub2Oct != sub1Oct);
}

TEST_CASE("HyperSynth filter and limiter modes apply cleanly", "[hypersynth]") {
    for (int filt = 0; filt < 8; ++filt) {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_HYPERSYN;
        auto& h = state.instruments[0].hyper;
        h.chord_bank = 0;
        h.chords[0][0] = 0;
        h.chords[0][1] = 7;
        h.chords[0][2] = 12;
        h.shift = 0x80;
        h.swarm = 0x20;
        h.width = 0x80;
        h.subosc = 0x40;
        h.filter_type = filt;
        h.cutoff = 0x80;
        h.res = 0x80;
        h.volume = 0x40;
        h.lim = filt % 9;
        h.dry = 0xC0;
        h.pan = 0x80;

        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(1000);

        for (float val : host.audio()) {
            REQUIRE(std::isfinite(val));
        }
    }
}

TEST_CASE("HyperSynth WIDTH produces real stereo, and WIDTH 00 is exactly mono",
          "[hypersynth]") {
    // WIDTH detunes the left and right saw stacks in opposite directions, so the
    // channels genuinely differ -- and until 2026-08-24 the voice summed them to
    // mono before the output stage, discarding it.
    //
    // Hardware settled both the shape and the polarity (hw_findings.md UI-11,
    // measured 2026-08-14): two probes differing only in WIDTH captured off the
    // device as exactly mono at 00 (side RMS 0.000000, correlation 1.0000) and
    // genuinely stereo at FF (side RMS 0.002136, correlation 0.9984). So WIDTH
    // is unipolar -- 00 is no spread, FF is maximum -- not bipolar about 0x80.
    // The same probes through m8_render read 0.000086 and 0.000085: no response
    // to WIDTH at all.
    //
    // This asserts the structure the device showed -- silent at 00, non-silent
    // and still highly correlated at FF -- not the captured magnitudes, which
    // are a level question hardware does not answer for us.
    auto sideRms = [](int width) {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_HYPERSYN;
        auto& h = state.instruments[0].hyper;
        h.chord_bank = 0;
        h.chords[0][0] = 0;  h.chords[0][1] = 4;  h.chords[0][2] = 7;
        h.chords[0][3] = 12; h.chords[0][4] = 16; h.chords[0][5] = 19;
        h.shift = 0x80;
        h.swarm = 0x40;
        h.width = static_cast<uint8_t>(width);
        h.subosc = 0x00;
        h.volume = 0x40;
        h.lim = 0;
        h.filter_type = 0;
        h.dry = 0xC0;
        h.pan = 0x80;                 // centred, so any L/R difference is WIDTH's

        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(4000);

        const auto& a = host.audio();
        double side = 0.0, mid = 0.0;
        size_t frames = a.size() / 2;
        for (size_t i = 0; i < frames; ++i) {
            const double l = a[i * 2], r = a[i * 2 + 1];
            side += ((l - r) * 0.5) * ((l - r) * 0.5);
            mid  += ((l + r) * 0.5) * ((l + r) * 0.5);
        }
        return std::pair<double, double>{ std::sqrt(side / double(frames)),
                                          std::sqrt(mid / double(frames)) };
    };

    auto [side00, mid00] = sideRms(0x00);
    auto [sideFF, midFF] = sideRms(0xFF);

    INFO("WIDTH 00 side=" << side00 << " mid=" << mid00);
    INFO("WIDTH FF side=" << sideFF << " mid=" << midFF);

    // Both must actually make sound, or the comparison is between two silences
    // -- the failure mode that let the app-vs-render comparison pass on silence.
    CHECK(mid00 > 1e-4);
    CHECK(midFF > 1e-4);

    // 00: no spread at all. The two stacks are detuned by +/- 0, so the channels
    // are identical sample for sample.
    CHECK(side00 < 1e-6);

    // FF: genuinely stereo, and by a wide margin over 00 rather than by noise.
    CHECK(sideFF > 1e-4);
    CHECK(sideFF > side00 * 100.0);

    // Still an image rather than two unrelated signals.
    //
    // Deliberately loose. The device measured side/mid = 0.029 (-31 dB) at
    // maximum width; this engine produces about 0.55, because `widthSpread`
    // (SynthVoice.cpp, 0.05 semitones at FF) is far wider than whatever the M8
    // uses. Tightening the constant to hit 0.029 would be fitting the engine to
    // a captured magnitude, which is a level question hardware is not used to
    // answer here -- so the gap is recorded in status.md instead of quietly
    // tuned away. What this pins is the structure: the channels differ, and
    // they are not anti-phase.
    CHECK(sideFF < midFF);
}

TEST_CASE("HyperSynth RT safety -- zero audio-thread allocations", "[hypersynth]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_HYPERSYN;
    auto& h = state.instruments[0].hyper;
    h.chord_bank = 0;
    h.shift = 0x80;
    h.swarm = 0x80;
    h.width = 0x80;
    h.subosc = 0x80;
    h.volume = 0x40;
    h.dry = 0xC0;

    g_allocCount = 0;

    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(1000);

    REQUIRE(g_allocCount == 0);
}
