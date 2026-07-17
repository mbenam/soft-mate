#include <catch2/catch_test_macros.hpp>
#include "support/OfflineHost.h"
#include <atomic>
#include <cmath>

extern std::atomic<int> g_allocCount;

using namespace m8::test;
using namespace m8::engine;

TEST_CASE("WavSynth renders all 9 base shapes without NaN", "[wavsynth]") {
    for (int shape = 0; shape < 9; ++shape) {
        DYNAMIC_SECTION("Shape " << shape) {
            OfflineHost host;
            auto& state = host.engine().getStateForInit();
            state.instruments[0].type = InstType::INST_WAVSYNTH;
            auto& ws = state.instruments[0].wav;
            ws.shape = shape;
            ws.size = 0x80; ws.mult = 0x00; ws.warp = 0x80; ws.scan = 0x00;
            ws.amp = 0x40; ws.lim = 0; ws.filter_type = 0;
            ws.dry = 0xC0; ws.pan = 0x80;

            g_allocCount = 0;
            setStep(host.sequencer(), 0, 0, 60, 100, 0);
            host.push(playPhrase(0, 0, 0));
            host.render(1000);

            REQUIRE(g_allocCount == 0);

            const auto& a = host.audio();
            bool hasNonZero = false;
            for (float val : a) {
                REQUIRE(std::isfinite(val));
                REQUIRE(val >= -1.05f);
                REQUIRE(val <= 1.05f);
                if (std::abs(val) > 0.0001f) hasNonZero = true;
            }
            REQUIRE(hasNonZero);
        }
    }
}

TEST_CASE("WavSynth different shapes produce different output", "[wavsynth]") {
    auto renderWav = [](int shape) -> float {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_WAVSYNTH;
        auto& ws = state.instruments[0].wav;
        ws.shape = shape; ws.size = 0x80; ws.amp = 0x40;
        ws.lim = 0; ws.filter_type = 0; ws.dry = 0xC0; ws.pan = 0x80;
        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(1000);
        float sum = 0;
        for (float v : host.audio()) sum += std::abs(v);
        return sum;
    };
    float sumSine = renderWav(6);
    float sumSaw = renderWav(4);
    REQUIRE(sumSine != sumSaw);
}

TEST_CASE("WavSynth SIZE parameter changes output", "[wavsynth]") {
    auto renderWavSize = [](int size) -> float {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_WAVSYNTH;
        auto& ws = state.instruments[0].wav;
        ws.shape = 6; ws.size = size; ws.amp = 0x40;
        ws.lim = 0; ws.filter_type = 0; ws.dry = 0xC0; ws.pan = 0x80;
        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(1000);
        float sum = 0;
        for (float v : host.audio()) sum += std::abs(v);
        return sum;
    };
    float sumSmall = renderWavSize(0x20);
    float sumLarge = renderWavSize(0xF0);
    REQUIRE(sumSmall != sumLarge);
}

TEST_CASE("WavSynth MULT parameter changes output", "[wavsynth]") {
    auto renderWavMult = [](int mult) -> float {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_WAVSYNTH;
        auto& ws = state.instruments[0].wav;
        ws.shape = 6; ws.size = 0x80; ws.mult = mult; ws.amp = 0x40;
        ws.lim = 0; ws.filter_type = 0; ws.dry = 0xC0; ws.pan = 0x80;
        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(1000);
        float sum = 0;
        for (float v : host.audio()) sum += std::abs(v);
        return sum;
    };
    float sumNoMult = renderWavMult(0x00);
    float sumHighMult = renderWavMult(0x0F);
    REQUIRE(sumNoMult != sumHighMult);
}

TEST_CASE("WavSynth RT safety -- zero allocations", "[wavsynth]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_WAVSYNTH;
    auto& ws = state.instruments[0].wav;
    ws.shape = 6; ws.size = 0x80; ws.amp = 0x40;
    ws.lim = 0; ws.filter_type = 0; ws.dry = 0xC0; ws.pan = 0x80;

    g_allocCount = 0;
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(5000);

    REQUIRE(g_allocCount == 0);
}
