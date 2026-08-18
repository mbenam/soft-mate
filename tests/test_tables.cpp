#include <catch2/catch_test_macros.hpp>
#include "support/OfflineHost.h"
#include "ui/screens/table/TableScreen.h"
#include "ui/UiCommands.h"
#include "engine/CommandRing.h"
#include <atomic>
#include <cmath>

extern std::atomic<int> g_allocCount;

using namespace m8::test;
using namespace m8::engine;
using namespace m8::ui;
using namespace m8::ui::table;

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

TEST_CASE("SET_TABLE_STEP command updates engine state", "[tables]") {
    OfflineHost host;
    EngineCommand cmd;
    cmd.type = CommandType::SET_TABLE_STEP;
    cmd.targetId = 3;
    cmd.row = 2;
    cmd.u.tableStep.transp = 7;
    cmd.u.tableStep.vol = 0x7F;
    cmd.u.tableStep.fx[0] = {FxCmd::HOP, 0};

    host.push(cmd);
    host.render(100);

    const auto& table = host.engine().getSequencer().tables[3];
    REQUIRE(table[2].transp == 7);
    REQUIRE(table[2].vol == 0x7F);
    REQUIRE(table[2].fx[0].cmd == FxCmd::HOP);
    REQUIRE(table[2].fx[0].val == 0);
}

TEST_CASE("Table UI: Navigation and Editing", "[tables]") {
    Sequencer seq;
    CommandRing<EngineCommand, 1024> ring;
    CommandSink sink{ring};

    int currentTable = 0;
    int cursor_x = 0;
    int cursor_y = 0;
    bool arrowPressed = false;

    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;

    // 1. Option navigation across tables
    ev.key.key = SDLK_RIGHT;
    HandleTableInput(ev, false, true, false, arrowPressed, seq, currentTable, cursor_x, cursor_y, sink);
    REQUIRE(currentTable == 1);

    ev.key.key = SDLK_DOWN;
    HandleTableInput(ev, false, true, false, arrowPressed, seq, currentTable, cursor_x, cursor_y, sink);
    REQUIRE(currentTable == 17); // +16

    // 2. Transpose editing (cursor_x = 0)
    cursor_x = 0; cursor_y = 0;
    ev.key.key = SDLK_UP;
    // EDIT+UP: transpose coarse +12 semitones
    HandleTableInput(ev, true, false, false, arrowPressed, seq, currentTable, cursor_x, cursor_y, sink);
    REQUIRE(seq.tables[17][0].transp == 12);

    // EDIT+RIGHT: transpose fine +1 semitone -> 13
    ev.key.key = SDLK_RIGHT;
    HandleTableInput(ev, true, false, false, arrowPressed, seq, currentTable, cursor_x, cursor_y, sink);
    REQUIRE(seq.tables[17][0].transp == 13);

    // 3. Volume editing (cursor_x = 1)
    cursor_x = 1;
    // Initially empty, EDIT+UP sets default 0x64
    ev.key.key = SDLK_UP;
    HandleTableInput(ev, true, false, false, arrowPressed, seq, currentTable, cursor_x, cursor_y, sink);
    REQUIRE(seq.tables[17][0].vol == 0x64);
    // Next EDIT+UP steps by +0x10 -> 0x74
    HandleTableInput(ev, true, false, false, arrowPressed, seq, currentTable, cursor_x, cursor_y, sink);
    REQUIRE(seq.tables[17][0].vol == 0x74);

    // 4. FX Command and Value editing (cursor_x = 2 and 3)
    cursor_x = 2; // FX1 Command
    ev.key.key = SDLK_UP;
    HandleTableInput(ev, true, false, false, arrowPressed, seq, currentTable, cursor_x, cursor_y, sink);
    REQUIRE(seq.tables[17][0].fx[0].cmd == FxCmd::VOL);

    cursor_x = 3; // FX1 Value
    ev.key.key = SDLK_RIGHT;
    HandleTableInput(ev, true, false, false, arrowPressed, seq, currentTable, cursor_x, cursor_y, sink);
    REQUIRE(seq.tables[17][0].fx[0].val == 1);

    // 5. Delete cell value
    ev.key.key = SDLK_DELETE;
    HandleTableInput(ev, true, false, false, arrowPressed, seq, currentTable, cursor_x, cursor_y, sink);
    REQUIRE(seq.tables[17][0].fx[0].cmd == FxCmd::NONE);

    // 6. Release tap insert default
    cursor_x = 2;
    HandleTableEditRelease(seq.tables[17][0], currentTable, cursor_x, cursor_y, sink);
    REQUIRE(seq.tables[17][0].fx[0].cmd == FxCmd::VOL);
}
