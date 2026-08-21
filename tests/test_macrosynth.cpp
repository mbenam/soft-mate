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
            state.instruments[0].macrosyn.volume = 0x40; // normal amplitude
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

TEST_CASE("Macrosynth timbre and color affect rendered audio", "[macrosynth]") {
    auto renderMacro = [](uint8_t shape, uint8_t timbre, uint8_t color, uint8_t degrade, uint8_t redux, uint8_t filter, uint8_t cutoff, uint8_t lim) -> float {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_MACROSYN;
        auto& m = state.instruments[0].macrosyn;
        m.shape = shape;
        m.timbre = timbre;
        m.color = color;
        m.degrade = degrade;
        m.redux = redux;
        m.filter_type = filter;
        m.cutoff = cutoff;
        m.lim = lim;
        m.volume = 0x40;
        m.pan = 0x80;
        m.dry = 0xC0;

        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(1000);
        float sum = 0.0f;
        for (float v : host.audio()) sum += std::abs(v);
        return sum;
    };

    float a1 = renderMacro(0, 0x20, 0x80, 0, 0, 0, 0xFF, 0);
    float a2 = renderMacro(0, 0xE0, 0x80, 0, 0, 0, 0xFF, 0);
    REQUIRE(a1 != a2);

    float c1 = renderMacro(0, 0x80, 0x20, 0, 0, 0, 0xFF, 0);
    float c2 = renderMacro(0, 0x80, 0xE0, 0, 0, 0, 0xFF, 0);
    REQUIRE(c1 != c2);
}

TEST_CASE("Macrosynth degrade and redux modify audio signal", "[macrosynth]") {
    auto renderMacro = [](uint8_t degrade, uint8_t redux) -> std::vector<float> {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_MACROSYN;
        auto& m = state.instruments[0].macrosyn;
        m.shape = 0;
        m.timbre = 0x80;
        m.color = 0x80;
        m.degrade = degrade;
        m.redux = redux;
        // VOLUME, not AMP: redux quantises BEFORE the output-stage gain, so the
        // difference this test looks for is scaled by that gain. Driving `amp`
        // stopped working when the byte map was corrected (AGENTS.md 7) -- the
        // gain reads `volume` now, and at unity the redux delta falls under the
        // 0.01 threshold. Every other level knob in this file was repointed at
        // `volume` too (2026-08-20); `amp` is inert, so setting it measured
        // nothing. Assertions unchanged.
        m.volume = 0x40;
        m.pan = 0x80;
        m.dry = 0xC0;

        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(500);
        return host.audio();
    };

    auto clean = renderMacro(0, 0);
    auto degraded = renderMacro(0x80, 0);
    auto reduxt = renderMacro(0, 0xC0);

    bool diffDegrade = false;
    for (size_t i = 0; i < clean.size(); ++i) {
        if (std::abs(clean[i] - degraded[i]) > 0.01f) diffDegrade = true;
    }
    REQUIRE(diffDegrade);

    bool diffRedux = false;
    for (size_t i = 0; i < clean.size(); ++i) {
        if (std::abs(clean[i] - reduxt[i]) > 0.01f) diffRedux = true;
    }
    REQUIRE(diffRedux);
}

TEST_CASE("Macrosynth filter modes and lim modes render finite output", "[macrosynth]") {
    for (int filt = 0; filt <= 7; ++filt) {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_MACROSYN;
        auto& m = state.instruments[0].macrosyn;
        m.shape = 0;
        m.timbre = 0x80;
        m.color = 0x80;
        m.filter_type = filt;
        m.cutoff = 0x80;
        m.res = 0x80;
        m.volume = 0x40;

        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(500);

        bool finite = true;
        for (float v : host.audio()) {
            if (!std::isfinite(v)) finite = false;
        }
        REQUIRE(finite);
    }

    for (int lim = 0; lim <= 8; ++lim) {
        OfflineHost host;
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_MACROSYN;
        auto& m = state.instruments[0].macrosyn;
        m.shape = 0;
        m.timbre = 0x80;
        m.color = 0x80;
        m.lim = lim;
        m.volume = 0x80;

        setStep(host.sequencer(), 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(500);

        bool finite = true;
        for (float v : host.audio()) {
            if (!std::isfinite(v)) finite = false;
        }
        REQUIRE(finite);
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
    h.volume = 0x40;
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
        h.volume = 0x40;
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
