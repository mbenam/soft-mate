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
