#include <catch2/catch_test_macros.hpp>
#include "support/OfflineHost.h"
#include <atomic>
#include <cmath>

extern std::atomic<int> g_allocCount;

using namespace m8::test;
using namespace m8::engine;

TEST_CASE("Macrosynth Phase 2 exhaustive shape checks", "[macrosynth]") {
    for (int shapeIdx = 0; shapeIdx <= 0x2B; ++shapeIdx) {
        DYNAMIC_SECTION("Shape " << shapeIdx) {
            OfflineHost host;
            auto& state = host.engine().getStateForInit();

            state.instruments[0].type = InstType::INST_MACROSYN;
            state.instruments[0].macrosyn.shape = shapeIdx;
            state.instruments[0].macrosyn.timbre = 0x80;
            state.instruments[0].macrosyn.color = 0x80;
            state.instruments[0].macrosyn.amp = 0x40; // normal amplitude
            state.instruments[0].macrosyn.lim = 0; // CLIP
            state.instruments[0].macrosyn.filter_type = 0; // OFF

            g_allocCount = 0;

            // Trigger note C-4
            setStep(host.sequencer(), 0, 0, 60, 100, 0); // Note C-4, Volume 100, Inst 0
            host.push(playPhrase(0, 0, 0));

            // Render 1000 samples to verify real-time safety and lack of NaNs
            host.render(1000);

            // Verify zero allocations occurred during render
            REQUIRE(g_allocCount == 0);

            // Verify audio output is finite and not completely silent
            const auto& a = host.audio();
            bool hasNonZero = false;
            for (float val : a) {
                REQUIRE(std::isfinite(val));
                REQUIRE(val >= -1.05f);
                REQUIRE(val <= 1.05f);
                if (std::abs(val) > 0.0001f) {
                    hasNonZero = true;
                }
            }
            REQUIRE(hasNonZero);
        }
    }
}

TEST_CASE("HyperSynth renders supersaw chord without NaN/clipping", "[hypersynth]") {
    OfflineHost host;
    auto& state = host.engine().getStateForInit();

    state.instruments[0].type = InstType::INST_HYPERSYN;
    auto& h = state.instruments[0].hyper;
    h.transp = 1;
    h.scale = 0xFF;
    h.shift = 128;
    h.swarm = 0x80;
    h.width = 0x80;
    h.subosc = 0x80;
    for (int c = 0; c < 7; ++c) h.default_chord[c] = 0x3C;
    h.amp = 0x40;
    h.filter_type = 0;
    h.cutoff = 0xFF;
    h.res = 0x00;
    h.lim = 0;
    h.pan = 0x80;
    h.dry = 0xC0;

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

TEST_CASE("HyperSynth different swarm/width settings produce different output", "[hypersynth]") {
    auto runHyper = [](uint8_t swarm, uint8_t width) -> float {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_HYPERSYN;
        auto& h = state.instruments[0].hyper;
        h.transp = 1;
        h.shift = 128;
        h.swarm = swarm;
        h.width = width;
        h.subosc = 0x00;
        for (int c = 0; c < 7; ++c) h.default_chord[c] = 0x3C;
        h.amp = 0x40;
        h.lim = 0;
        h.pan = 0x80;
        h.dry = 0xC0;
        h.filter_type = 0;

        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(500);
        float sum = 0;
        for (float v : host.audio()) sum += std::abs(v);
        return sum;
    };

    float narrow = runHyper(0x00, 0x40);
    float wide   = runHyper(0xFF, 0xFF);
    REQUIRE(narrow != wide);
}
