#include <catch2/catch_test_macros.hpp>
#include "support/OfflineHost.h"
#include <atomic>
#include <cmath>

extern std::atomic<int> g_allocCount;

using namespace m8::test;
using namespace m8::engine;

TEST_CASE("FMSynth renders all 12 algorithms without NaN", "[fmsynth]") {
    for (int algo = 0; algo < 12; ++algo) {
        DYNAMIC_SECTION("Algorithm " << algo) {
            OfflineHost host;
            auto& state = host.engine().getStateForInit();
            state.instruments[0].type = InstType::INST_FMSYNTH;
            auto& fm = state.instruments[0].fm;
            fm.algo = algo;
            fm.ops[0].shape = 0; fm.ops[0].level = 0x80; fm.ops[0].ratio = 1;
            fm.ops[1].shape = 0; fm.ops[1].level = 0x80; fm.ops[1].ratio = 2;
            fm.ops[2].shape = 0; fm.ops[2].level = 0x80; fm.ops[2].ratio = 3;
            fm.ops[3].shape = 0; fm.ops[3].level = 0x80; fm.ops[3].ratio = 4;
            fm.amp = 0x40; fm.lim = 0; fm.filter_type = 0;
            fm.dry = 0xC0; fm.pan = 0x80;

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

TEST_CASE("FMSynth different algorithms produce different output", "[fmsynth]") {
    auto renderFM = [](int algo) -> float {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_FMSYNTH;
        auto& fm = state.instruments[0].fm;
        fm.algo = algo;
        fm.ops[0].shape = 0; fm.ops[0].level = 0x80; fm.ops[0].ratio = 1;
        fm.ops[1].shape = 0; fm.ops[1].level = 0x80; fm.ops[1].ratio = 2;
        fm.ops[2].shape = 0; fm.ops[2].level = 0x80; fm.ops[2].ratio = 3;
        fm.ops[3].shape = 0; fm.ops[3].level = 0x80; fm.ops[3].ratio = 4;
        fm.amp = 0x40; fm.lim = 0; fm.filter_type = 0;
        fm.dry = 0xC0; fm.pan = 0x80;

        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(1000);
        float sum = 0;
        for (float v : host.audio()) sum += std::abs(v);
        return sum;
    };

    float sum00 = renderFM(0);
    float sum0B = renderFM(0xB);
    REQUIRE(sum00 != sum0B);
}

TEST_CASE("FMSynth different shapes produce different output", "[fmsynth]") {
    auto renderFMShape = [](int shape) -> float {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_FMSYNTH;
        auto& fm = state.instruments[0].fm;
        fm.algo = 0;
        fm.ops[0].shape = shape; fm.ops[0].level = 0x80; fm.ops[0].ratio = 1;
        fm.ops[1].shape = shape; fm.ops[1].level = 0x80; fm.ops[1].ratio = 2;
        fm.ops[2].shape = shape; fm.ops[2].level = 0x80; fm.ops[2].ratio = 3;
        fm.ops[3].shape = shape; fm.ops[3].level = 0x80; fm.ops[3].ratio = 4;
        fm.amp = 0x40; fm.lim = 0; fm.filter_type = 0;
        fm.dry = 0xC0; fm.pan = 0x80;

        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(1000);
        float sum = 0;
        for (float v : host.audio()) sum += std::abs(v);
        return sum;
    };

    float sumSIN = renderFMShape(0);
    float sumSAW = renderFMShape(7);
    REQUIRE(sumSIN != sumSAW);
}

TEST_CASE("FMSynth feedback increases complexity", "[fmsynth]") {
    auto renderFMFB = [](int feedback) -> float {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_FMSYNTH;
        auto& fm = state.instruments[0].fm;
        fm.algo = 0;
        fm.ops[0].shape = 0; fm.ops[0].level = 0x80; fm.ops[0].ratio = 1;
        fm.ops[0].feedback = feedback;
        fm.ops[1].shape = 0; fm.ops[1].level = 0x80; fm.ops[1].ratio = 2;
        fm.ops[2].shape = 0; fm.ops[2].level = 0x80; fm.ops[2].ratio = 3;
        fm.ops[3].shape = 0; fm.ops[3].level = 0x80; fm.ops[3].ratio = 4;
        fm.amp = 0x40; fm.lim = 0; fm.filter_type = 0;
        fm.dry = 0xC0; fm.pan = 0x80;

        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(1000);
        float sum = 0;
        for (float v : host.audio()) sum += std::abs(v);
        return sum;
    };

    float sumNoFB = renderFMFB(0);
    float sumHighFB = renderFMFB(0xFF);
    REQUIRE(sumHighFB != sumNoFB);
}

TEST_CASE("FMSynth RT safety -- zero allocations", "[fmsynth]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_FMSYNTH;
    auto& fm = state.instruments[0].fm;
    fm.algo = 0; fm.ops[0].shape = 0; fm.ops[0].level = 0x80;
    fm.ops[0].ratio = 1; fm.amp = 0x40; fm.lim = 0;
    fm.filter_type = 0; fm.dry = 0xC0; fm.pan = 0x80;

    g_allocCount = 0;
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(5000);

    REQUIRE(g_allocCount == 0);
}
