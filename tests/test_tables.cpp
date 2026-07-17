#include <catch2/catch_test_macros.hpp>
#include "support/OfflineHost.h"
#include <atomic>
#include <cmath>

extern std::atomic<int> g_allocCount;

using namespace m8::test;
using namespace m8::engine;

TEST_CASE("Table auto-assigns on note-on", "[tables]") {
    OfflineHost host;
    auto& seq = host.sequencer();
    auto& state = host.engine().getStateForInit();

    state.instruments[0].type = InstType::INST_MACROSYN;
    state.instruments[0].macrosyn.tbl_tic = 0x06;
    state.instruments[0].macrosyn.dry = 0xC0;
    state.instruments[0].macrosyn.pan = 0x80;

    // Table 0, row 0: transpose +5 semitones
    seq.tables[0][0].transp = 5;
    seq.tables[0][0].vol = VOL_EMPTY;

    setStep(seq, 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(500);

    // Voice should have received a note-on
    auto noteOns = host.noteOnsForTrack(0);
    REQUIRE(noteOns.size() == 1);
}

TEST_CASE("Table transpose changes pitch", "[tables]") {
    auto renderWithTableTransp = [](int transp) -> float {
        OfflineHost host;
        auto& seq = host.sequencer();
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_MACROSYN;
        state.instruments[0].macrosyn.tbl_tic = 0x01;
        state.instruments[0].macrosyn.dry = 0xC0;
        state.instruments[0].macrosyn.pan = 0x80;
        seq.tables[0][0].transp = transp;
        seq.tables[0][0].vol = 0x7F;
        setStep(seq, 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(2000);
        float sum = 0;
        for (float v : host.audio()) sum += std::abs(v);
        return sum;
    };
    float sumTransp0 = renderWithTableTransp(0);
    float sumTransp12 = renderWithTableTransp(12); // +1 octave
    REQUIRE(sumTransp0 != sumTransp12);
}

TEST_CASE("Table volume scales output", "[tables]") {
    auto renderWithTableVol = [](int vol) -> float {
        OfflineHost host;
        auto& seq = host.sequencer();
        auto& state = host.engine().getStateForInit();
        state.instruments[0].type = InstType::INST_MACROSYN;
        state.instruments[0].macrosyn.tbl_tic = 0x01;
        state.instruments[0].macrosyn.dry = 0xC0;
        state.instruments[0].macrosyn.pan = 0x80;
        seq.tables[0][0].transp = 0;
        seq.tables[0][0].vol = vol;
        setStep(seq, 0, 0, 60, 100, 0);
        host.push(playPhrase(0, 0, 0));
        host.render(2000);
        float peak = 0;
        for (float v : host.audio()) peak = std::max(peak, std::abs(v));
        return peak;
    };
    float peakFull = renderWithTableVol(0x7F);
    float peakHalf = renderWithTableVol(0x40);
    REQUIRE(peakFull > peakHalf);
}

TEST_CASE("Table HOP jumps to row", "[tables]") {
    OfflineHost host;
    auto& seq = host.sequencer();
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_MACROSYN;
    state.instruments[0].macrosyn.tbl_tic = 0x01; // 1 tick per row (fast)
    state.instruments[0].macrosyn.dry = 0xC0;
    state.instruments[0].macrosyn.pan = 0x80;

    // Row 0: HOP to row 5
    seq.tables[0][0].fx[0] = {FxCmd::HOP, 5};
    // Row 5: volume 0 (silent)
    seq.tables[0][5].vol = 0;

    setStep(seq, 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(2000);

    // Should have jumped to row 5 and be silent or near-silent
    float peak = 0;
    for (float v : host.audio()) peak = std::max(peak, std::abs(v));
    REQUIRE(peak < 0.1f);
}

TEST_CASE("Per-track groove override", "[tables]") {
    OfflineHost host;
    auto& seq = host.sequencer();
    auto& state = host.engine().getStateForInit();

    // Groove 0: straight 6/6/6
    seq.grooves[0] = Groove{};
    for (int i = 0; i < 16; ++i) seq.grooves[0].steps[i] = 6;
    seq.grooves[0].length = 16;

    // Groove 1: swing 7/5
    seq.grooves[1] = Groove{};
    for (int i = 0; i < 16; ++i) seq.grooves[1].steps[i] = (i % 2 == 0) ? 7 : 5;
    seq.grooves[1].length = 16;

    state.project.groove = 0;
    state.trackGroove[0] = 1; // track 0 uses swing

    state.instruments[0].type = InstType::INST_MACROSYN;
    state.instruments[0].macrosyn.dry = 0xC0;
    state.instruments[0].macrosyn.pan = 0x80;

    setStep(seq, 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(1000);

    // Just verify it renders without crash
    REQUIRE(host.audio().size() > 0);
}

TEST_CASE("Tables RT safety -- zero allocations", "[tables]") {
    OfflineHost host;
    auto& seq = host.sequencer();
    auto& state = host.engine().getStateForInit();
    state.instruments[0].type = InstType::INST_MACROSYN;
    state.instruments[0].macrosyn.tbl_tic = 0x02;
    state.instruments[0].macrosyn.dry = 0xC0;
    state.instruments[0].macrosyn.pan = 0x80;
    seq.tables[0][0].transp = 3;
    seq.tables[0][0].vol = 0x60;

    g_allocCount = 0;
    setStep(seq, 0, 0, 60, 100, 0);
    host.push(playPhrase(0, 0, 0));
    host.render(5000);

    REQUIRE(g_allocCount == 0);
}
