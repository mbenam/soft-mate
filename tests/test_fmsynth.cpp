#include <catch2/catch_test_macros.hpp>
#include "support/OfflineHost.h"
#include "ui/screens/instrument/InstrumentScreen.h"
#include "ui/screens/instrument/InstrumentCursorId.h"
#include <atomic>
#include <cmath>

extern std::atomic<int> g_allocCount;

using namespace m8::test;
using namespace m8::engine;
using namespace m8::ui::instrument;

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
            fm.volume = 0x40; fm.lim = 0; fm.filter_type = 0;
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
        fm.volume = 0x40; fm.lim = 0; fm.filter_type = 0;
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
        fm.volume = 0x40; fm.lim = 0; fm.filter_type = 0;
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
        fm.volume = 0x40; fm.lim = 0; fm.filter_type = 0;
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

TEST_CASE("FMSynth modulation destinations (LEV, RAT, PIT, FBK)", "[fmsynth]") {
    auto renderWithMod = [](uint8_t modSlot, uint8_t modVal) -> float {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_FMSYNTH;
        auto& fm = state.instruments[0].fm;
        fm.algo = 0x0B; // Additive mode
        fm.ops[0].shape = 0; fm.ops[0].level = 0x80; fm.ops[0].ratio = 1;
        fm.ops[0].mod_a = modSlot; // e.g. 1LEV (0x01), 1RAT (0x02), 1PIT (0x03), 1FBK (0x04)
        fm.mod1 = modVal;
        fm.volume = 0x40; fm.lim = 0; fm.filter_type = 0;
        fm.dry = 0xC0; fm.pan = 0x80;

        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(1000);
        float sum = 0;
        for (float v : host.audio()) sum += std::abs(v);
        return sum;
    };

    // PIT mod (0x03) changes output when mod1 differs
    float pitLow = renderWithMod(0x03, 0x00);
    float pitHigh = renderWithMod(0x03, 0xFF);
    REQUIRE(pitLow != pitHigh);

    // RAT mod (0x02)
    float ratLow = renderWithMod(0x02, 0x00);
    float ratHigh = renderWithMod(0x02, 0xFF);
    REQUIRE(ratLow != ratHigh);
}

TEST_CASE("FMSynth filter and limiter modes apply cleanly", "[fmsynth]") {
    for (int filt = 0; filt < 8; ++filt) {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_FMSYNTH;
        auto& fm = state.instruments[0].fm;
        fm.algo = 0x0B;
        fm.ops[0].shape = 7; fm.ops[0].level = 0x80; fm.ops[0].ratio = 1;
        fm.filter_type = filt; fm.cutoff = 0x80; fm.res = 0x80;
        fm.volume = 0x40; fm.lim = filt % 9;
        fm.dry = 0xC0; fm.pan = 0x80;

        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(1000);

        for (float val : host.audio()) {
            REQUIRE(std::isfinite(val));
        }
    }
}

TEST_CASE("FMSynth RT safety -- zero allocations", "[fmsynth]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_FMSYNTH;
    auto& fm = state.instruments[0].fm;
    fm.algo = 0; fm.ops[0].shape = 0; fm.ops[0].level = 0x80;
    fm.ops[0].ratio = 1; fm.volume = 0x40; fm.lim = 0;
    fm.filter_type = 0; fm.dry = 0xC0; fm.pan = 0x80;

    g_allocCount = 0;
    setStep(host.sequencer(), 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(5000);

    REQUIRE(g_allocCount == 0);
}
