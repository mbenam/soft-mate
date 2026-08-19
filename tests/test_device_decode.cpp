// ===========================================================================
// tests/test_device_decode.cpp
//
// Offline replay tests for the M8Device perception layer. Replays synthetic
// SLIP frames (not from a real device — those would be in tests/hw/frames/)
// and asserts the decoded ScreenGrid produces the expected text grid.
//
// Tier 0 of M8_DEVICE_CONTROL_SPEC.md.
// ===========================================================================

#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <cstdint>

#include "m8/M8Device.h"
#include "m8/ScreenModel.h"
#include "m8/Primitives.h"
#include "m8/DeviceScriptRunner.h"
#include "m8/Semantic.h"
#include "m8/UiCapture.h"
#include "m8/DeviceTheme.h"
#include <fstream>
#include <cstdio>
#include <string>

using namespace m8::dev;

// ---- Helper: build a 0xFD draw-char frame ---------------------------------

static std::vector<uint8_t> makeCharFrame(char ch, int x, int y,
                                           uint8_t fgR, uint8_t fgG, uint8_t fgB,
                                           uint8_t bgR, uint8_t bgG, uint8_t bgB) {
    return {
        0xFD,
        static_cast<uint8_t>(ch),
        static_cast<uint8_t>(x & 0xFF), static_cast<uint8_t>((x >> 8) & 0xFF),
        static_cast<uint8_t>(y & 0xFF), static_cast<uint8_t>((y >> 8) & 0xFF),
        fgR, fgG, fgB,
        bgR, bgG, bgB
    };
}

// ---- Helper: build a 0xFF system-info frame --------------------------------

static std::vector<uint8_t> makeSysInfoFrame(int hwType, int fwMajor, int fwMinor,
                                              int fwPatch, int fontMode = 0) {
    return {
        0xFF,
        static_cast<uint8_t>(hwType),
        static_cast<uint8_t>(fwMajor),
        static_cast<uint8_t>(fwMinor),
        static_cast<uint8_t>(fwPatch),
        static_cast<uint8_t>(fontMode)
    };
}

// ---- Helper: build a 0xFE rect frame (colored fill) -----------------------

static std::vector<uint8_t> makeRectFrame(int x, int y, int w, int h,
                                           uint8_t r, uint8_t g, uint8_t b) {
    return {
        0xFE,
        static_cast<uint8_t>(x & 0xFF), static_cast<uint8_t>((x >> 8) & 0xFF),
        static_cast<uint8_t>(y & 0xFF), static_cast<uint8_t>((y >> 8) & 0xFF),
        static_cast<uint8_t>(w & 0xFF), static_cast<uint8_t>((w >> 8) & 0xFF),
        static_cast<uint8_t>(h & 0xFF), static_cast<uint8_t>((h >> 8) & 0xFF),
        r, g, b
    };
}

// ---- ScreenGrid decode tests ----------------------------------------------

TEST_CASE("ScreenGrid decodes draw-char frames", "[hwdecode]") {
    ScreenGrid grid;
    auto frame = makeCharFrame('S', 10, 20, 0, 252, 248, 0, 0, 0);
    grid.handleFrame(frame);

    REQUIRE(grid.cells.size() == 1);
    auto it = grid.cells.find({20, 10});
    REQUIRE(it != grid.cells.end());
    CHECK(it->second.ch == 'S');
    CHECK(it->second.fg[0] == 0);
    CHECK(it->second.fg[1] == 252);
    CHECK(it->second.fg[2] == 248);
}

TEST_CASE("ScreenGrid decodes system-info frames", "[hwdecode]") {
    ScreenGrid grid;
    auto frame = makeSysInfoFrame(3, 6, 5, 2, 0);
    grid.handleFrame(frame);

    CHECK(grid.hwType == 3);
    CHECK(grid.fwMajor == 6);
    CHECK(grid.fwMinor == 5);
    CHECK(grid.fwPatch == 2);
    CHECK(grid.fontMode == 0);
}

TEST_CASE("ScreenGrid cursor detection", "[hwdecode]") {
    ScreenGrid grid;
    // Default cursor color is the fw 6.5.2 stock accent (0, 240, 248); this
    // (0, 252, 248) is 12 off in green and so still inside the tolerance, which
    // is the point -- see the cursorColor note in M8Device.h and hw_findings
    // UI-14.
    auto cursorFrame = makeCharFrame('X', 50, 30, 0, 252, 248, 0, 0, 0);
    auto normalFrame = makeCharFrame('Y', 60, 30, 255, 255, 255, 0, 0, 0);
    grid.handleFrame(cursorFrame);
    grid.handleFrame(normalFrame);

    CHECK(grid.isCursor(grid.cells[{30, 50}]));
    CHECK_FALSE(grid.isCursor(grid.cells[{30, 60}]));
}

// Regression for hw_findings UI-14. isCursor() was an exact three-channel
// equality against a constant that did not match the device's actual stock
// accent, so it returned false for every cell on every screen -- and the
// symptom was not "no cursor", it was every cursor read coming back -1, which
// the driver reported as "the press is not landing". A tolerance test is what
// makes that unable to recur silently for a near-stock theme; a theme further
// out is meant to fail loudly and be pointed at `m8_nav --cursor-color`.
TEST_CASE("cursor detection tolerates a near-stock theme accent", "[hwdecode]") {
    ScreenGrid grid;
    // The measured fw 6.5.2 accent itself must match.
    grid.handleFrame(makeCharFrame('A', 0, 30, 0, 240, 248, 0, 0, 0));
    // The pre-2026-08-18 constant is within tolerance of it.
    grid.handleFrame(makeCharFrame('B', 8, 30, 0, 252, 248, 0, 0, 0));
    // A different theme's accent is not, and must not be mistaken for one.
    grid.handleFrame(makeCharFrame('C', 16, 30, 248, 160, 0, 0, 0, 0));
    // Neither may a neighbouring entry of the stock palette -- the values here
    // are the measured "values" white and "titles" red from UI-14.
    grid.handleFrame(makeCharFrame('D', 24, 30, 248, 252, 248, 0, 0, 0));
    grid.handleFrame(makeCharFrame('E', 32, 30, 248, 32, 48, 0, 0, 0));

    CHECK(grid.isCursor(grid.cells[{30, 0}]));
    CHECK(grid.isCursor(grid.cells[{30, 8}]));
    CHECK_FALSE(grid.isCursor(grid.cells[{30, 16}]));
    CHECK_FALSE(grid.isCursor(grid.cells[{30, 24}]));
    CHECK_FALSE(grid.isCursor(grid.cells[{30, 32}]));
}

// M8_DRIVER_BUGS.md #22. Layout and colours taken from a real fw 6.5.2 SONG
// screen (2026-08-14, COM3, captured with `m8drv inspect --key DOWN`): the
// column-header row carries an accent-coloured track indicator, and the cursor
// marks its own row by recolouring that row's leading row-number.
TEST_CASE("cursorRowY prefers the row label over the column header", "[hwdecode]") {
    ScreenGrid grid;
    const uint8_t A0 = 0, A1 = 252, A2 = 248;   // accent == cursorColor

    // Column-header row at y=50: the track-6 indicator, accent, at col 19.
    grid.handleFrame(makeCharFrame('6', 19 * 8, 50, A0, A1, A2, 0, 0, 0));
    // Cursor's row at y=110: row number "05" accent in the left label columns,
    // plus the cell itself accent out in the body.
    grid.handleFrame(makeCharFrame('0', 1 * 8, 110, A0, A1, A2, 0, 0, 0));
    grid.handleFrame(makeCharFrame('5', 2 * 8, 110, A0, A1, A2, 0, 0, 0));
    grid.handleFrame(makeCharFrame('-', 19 * 8, 110, A0, A1, A2, 0, 0, 0));

    // Topmost-accent would answer 50, the header. The row label must win.
    REQUIRE(grid.cursorRowY() == 110);

    // A stale accent-coloured blank at the vacated row must not be picked up --
    // the M8 skips redrawing trailing blanks, and (row 11, col 3, ' ') was
    // observed surviving exactly this transition on hardware.
    grid.handleFrame(makeCharFrame(' ', 3 * 8, 110, A0, A1, A2, 0, 0, 0));
    grid.handleFrame(makeCharFrame('0', 1 * 8, 120, A0, A1, A2, 0, 0, 0));
    grid.handleFrame(makeCharFrame('6', 2 * 8, 120, A0, A1, A2, 0, 0, 0));
    // Row 110's real label is still accent here (the M8 recolours it back, but
    // this asserts the ghost-blank rule alone, so leave it), so the topmost
    // real label is still 110 -- what matters is that the ' ' at col 3 never
    // becomes the answer on its own.
    REQUIRE(grid.cursorRowY() == 110);
}

// M8_DRIVER_BUGS.md #23. Column edges must come from the header row's runs, not
// from the smallest glyph gap on screen -- the old code measured 8px (adjacent
// glyphs inside one cell) where SONG's track columns are 24px apart.
// The label/value split. cursorMainText() concatenates every accent cell on the
// cursor's row, so the label and value arrive glued together -- with a gap on
// PROJECT's TEMPO row ("TEMPO   120", observed on fw 6.5.2) and with none at all
// on TRANSPOSE ("TRANSPOSE00"), which is why splitting on whitespace cannot work.
TEST_CASE("cursorValueText strips a form field's label", "[hwdecode]") {
    ScreenGrid grid;
    const uint8_t A0 = 0, A1 = 252, A2 = 248;
    const uint8_t W = 255;

    // "PROJECT" title so identifyScreen resolves, then an accent TRANSPOSE row.
    const char* title = "PROJECT";
    for (int i = 0; title[i]; ++i)
        grid.handleFrame(makeCharFrame(title[i], i * 8, 30, W, W, W, 0, 0, 0));
    const char* row = "TRANSPOSE00";
    for (int i = 0; row[i]; ++i)
        grid.handleFrame(makeCharFrame(row[i], (1 + i) * 8, 60, A0, A1, A2, 0, 0, 0));

    REQUIRE(m8::dev::cursorValueText(grid) == "00");
}

// Observed on fw 6.5.2: the accent run extends past the value into the row's
// trailing padding, so INSTRUMENT's AMP read back as "40       ". A caller
// comparing that against "40" fails on whitespace alone.
TEST_CASE("cursorValueText trims trailing padding", "[hwdecode]") {
    ScreenGrid grid;
    const uint8_t A0 = 0, A1 = 252, A2 = 248;
    const uint8_t W = 255;

    const char* title = "INST. 00";
    for (int i = 0; title[i]; ++i)
        grid.handleFrame(makeCharFrame(title[i], i * 8, 30, W, W, W, 0, 0, 0));
    // TYPE row included so readInstrumentType resolves deterministically -- it
    // picks which instrument field map cursorValueText looks the label up in.
    const char* type = "TYPE    MACROSYN";
    for (int i = 0; type[i]; ++i)
        grid.handleFrame(makeCharFrame(type[i], i * 8, 50, W, W, W, 0, 0, 0));
    // "AMP 40" then several accent-coloured blanks, as the device sends it.
    const char* row = "AMP 40       ";
    for (int i = 0; row[i]; ++i)
        grid.handleFrame(makeCharFrame(row[i], (24 + i) * 8, 110, A0, A1, A2, 0, 0, 0));

    REQUIRE(m8::dev::cursorValueText(grid) == "40");
}

// Observed on fw 6.5.2 MIXER: the accent run can START on a blank cell, so the row
// arrived as " OUTPUT VOL  F0". Trimming after the label match instead of before
// meant no label prefix-matched and the whole row was returned as the value.
// Whether the leading blank appears varies between reads, so it looked flaky.
TEST_CASE("cursorValueText survives a leading accent blank", "[hwdecode]") {
    ScreenGrid grid;
    const uint8_t A0 = 0, A1 = 252, A2 = 248;
    const uint8_t W = 255;

    const char* title = "MIXER";
    for (int i = 0; title[i]; ++i)
        grid.handleFrame(makeCharFrame(title[i], i * 8, 30, W, W, W, 0, 0, 0));
    const char* row = " OUTPUT VOL  F0";     // note the leading space, accent-coloured
    for (int i = 0; row[i]; ++i)
        grid.handleFrame(makeCharFrame(row[i], i * 8, 50, A0, A1, A2, 0, 0, 0));

    REQUIRE(m8::dev::cursorValueText(grid) == "F0");
}

// Same field, next frame: the interior blanks were NOT accent-coloured that read,
// so cursorMainText returned "OUTPUTVOLF0" with the spaces collapsed and the label
// "OUTPUT VOL" no longer prefix-matched. Observed on fw 6.5.2 immediately after a
// home call. The label match has to ignore whitespace on both sides.
TEST_CASE("cursorValueText matches a label across collapsed spaces", "[hwdecode]") {
    ScreenGrid grid;
    const uint8_t A0 = 0, A1 = 252, A2 = 248;
    const uint8_t W = 255;

    const char* title = "MIXER";
    for (int i = 0; title[i]; ++i)
        grid.handleFrame(makeCharFrame(title[i], i * 8, 30, W, W, W, 0, 0, 0));
    // Only the glyph cells are accent-coloured; the gaps between them are not sent
    // as accent, so they never reach cursorMainText at all.
    const char* glyphs = "OUTPUTVOLF0";
    for (int i = 0; glyphs[i]; ++i)
        grid.handleFrame(makeCharFrame(glyphs[i], (1 + i) * 8, 50, A0, A1, A2, 0, 0, 0));

    REQUIRE(m8::dev::cursorValueText(grid) == "F0");
}

TEST_CASE("gridColumnEdges reads SONG's eight track columns", "[hwdecode]") {
    ScreenGrid grid;
    // SONG header at y=50: single digits 1..8, 24px apart starting at x=32.
    for (int i = 0; i < 8; ++i)
        grid.handleFrame(makeCharFrame('1' + i, 32 + i * 24, 50, 255, 255, 255, 0, 0, 0));
    // A data row below must not contribute columns.
    grid.handleFrame(makeCharFrame('0', 8, 60, 255, 255, 255, 0, 0, 0));
    grid.handleFrame(makeCharFrame('0', 16, 60, 255, 255, 255, 0, 0, 0));

    auto edges = m8::dev::gridColumnEdges(grid, 50);
    REQUIRE(edges.size() == 8);
    REQUIRE(edges.front() == 32);
    REQUIRE(edges[1] == 56);
    REQUIRE(edges.back() == 32 + 7 * 24);
}

TEST_CASE("gridColumnEdges groups multi-character column labels", "[hwdecode]") {
    ScreenGrid grid;
    // PHRASE-style header: "N" then "FX1" as a 3-glyph run, then "FX2".
    grid.handleFrame(makeCharFrame('N', 32, 50, 255, 255, 255, 0, 0, 0));
    grid.handleFrame(makeCharFrame('F', 80, 50, 255, 255, 255, 0, 0, 0));
    grid.handleFrame(makeCharFrame('X', 88, 50, 255, 255, 255, 0, 0, 0));
    grid.handleFrame(makeCharFrame('1', 96, 50, 255, 255, 255, 0, 0, 0));
    grid.handleFrame(makeCharFrame('F', 128, 50, 255, 255, 255, 0, 0, 0));
    grid.handleFrame(makeCharFrame('X', 136, 50, 255, 255, 255, 0, 0, 0));
    grid.handleFrame(makeCharFrame('2', 144, 50, 255, 255, 255, 0, 0, 0));

    auto edges = m8::dev::gridColumnEdges(grid, 50);
    REQUIRE(edges.size() == 3);      // N, FX1, FX2 -- not 7 separate glyphs
    REQUIRE(edges[0] == 32);
    REQUIRE(edges[1] == 80);
    REQUIRE(edges[2] == 128);
}

// Built cell-for-cell from a real fw 6.5.2 capture (`m8drv inspect`, 2026-08-14)
// taken right after `cursor-grid 4 2` succeeded, so this pins the indexing the
// tool's API now exposes: accented header digit "3" at (row 5, col 10), cursor
// row label "04" at (row 10, cols 1-2), cursor cell at (row 10, cols 10-11).
// step must read 4 and col must read 2 -- i.e. col is 0-based and track 3 == 2.
TEST_CASE("gridCursorPosition matches the captured SONG cursor", "[hwdecode]") {
    ScreenGrid grid;
    const uint8_t A0 = 0, A1 = 252, A2 = 248;
    const uint8_t W = 255;

    // Header row y=50: digits 1..8 at cols 4,7,10,... Track 3 (col 10) accented.
    for (int i = 0; i < 8; ++i) {
        const int col = 4 + i * 3;
        const bool on = (col == 10);
        grid.handleFrame(makeCharFrame('1' + i, col * 8, 50,
                                       on ? A0 : W, on ? A1 : W, on ? A2 : W, 0, 0, 0));
    }
    // Sixteen data rows y=60..210 with hex row labels; row 04 (y=100) accented.
    for (int r = 0; r < 16; ++r) {
        const int y = 60 + r * 10;
        const bool on = (r == 4);
        const char hi = '0';
        const char lo = static_cast<char>(r < 10 ? '0' + r : 'A' + (r - 10));
        grid.handleFrame(makeCharFrame(hi, 1 * 8, y, on ? A0 : W, on ? A1 : W, on ? A2 : W, 0, 0, 0));
        grid.handleFrame(makeCharFrame(lo, 2 * 8, y, on ? A0 : W, on ? A1 : W, on ? A2 : W, 0, 0, 0));
        for (int t = 0; t < 8; ++t) {
            const int col = 4 + t * 3;
            const bool cur = on && col == 10;
            for (int d = 0; d < 2; ++d)
                grid.handleFrame(makeCharFrame('-', (col + d) * 8, y,
                                               cur ? A0 : W, cur ? A1 : W, cur ? A2 : W, 0, 0, 0));
        }
    }
    // The ghost the M8 leaves behind: a stale accent blank at the cursor row.
    grid.handleFrame(makeCharFrame(' ', 3 * 8, 100, A0, A1, A2, 0, 0, 0));

    auto gc = m8::dev::gridCursorPosition(grid);
    REQUIRE(gc.valid);
    REQUIRE(gc.columns == 8);
    REQUIRE(gc.step == 4);
    REQUIRE(gc.col == 2);
}

// M8_DRIVER_BUGS.md #24. Measured on hardware: within one session, DOWN advanced
// cursor_row 150 -> 160 -> 170 while grid_step stayed pinned at 8, because
// findCursorCell() had no ch != ' ' guard and latched onto the accent-coloured
// blank the M8 leaves at the vacated row. A fresh process read it correctly,
// since open()'s 'R' full-framebuffer resend repaints the ghosts away -- which is
// exactly why this hid behind one-shot invocations.
TEST_CASE("gridCursorPosition ignores the ghost row the cursor left", "[hwdecode]") {
    ScreenGrid grid;
    const uint8_t A0 = 0, A1 = 252, A2 = 248;
    const uint8_t W = 255;

    for (int i = 0; i < 8; ++i) {
        const int col = 4 + i * 3;
        const bool on = (col == 10);
        grid.handleFrame(makeCharFrame('1' + i, col * 8, 50,
                                       on ? A0 : W, on ? A1 : W, on ? A2 : W, 0, 0, 0));
    }
    for (int r = 0; r < 16; ++r) {
        const int y = 60 + r * 10;
        const bool on = (r == 12);                     // cursor is on row 0C
        const char lo = static_cast<char>(r < 10 ? '0' + r : 'A' + (r - 10));
        grid.handleFrame(makeCharFrame('0', 1 * 8, y, on ? A0 : W, on ? A1 : W, on ? A2 : W, 0, 0, 0));
        grid.handleFrame(makeCharFrame(lo, 2 * 8, y, on ? A0 : W, on ? A1 : W, on ? A2 : W, 0, 0, 0));
        grid.handleFrame(makeCharFrame('-', 10 * 8, y, on ? A0 : W, on ? A1 : W, on ? A2 : W, 0, 0, 0));
        grid.handleFrame(makeCharFrame('-', 11 * 8, y, on ? A0 : W, on ? A1 : W, on ? A2 : W, 0, 0, 0));
    }
    // The ghost: row 08 (y=140) is a row the cursor previously occupied, and its
    // trailing blank kept the accent colour while its real text recoloured back.
    grid.handleFrame(makeCharFrame(' ', 3 * 8, 140, A0, A1, A2, 0, 0, 0));

    auto gc = m8::dev::gridCursorPosition(grid);
    REQUIRE(gc.valid);
    REQUIRE(gc.step == 12);      // not 8, the ghost's row
    REQUIRE(gc.col == 2);
}

TEST_CASE("gridCursorPosition reports invalid on a form screen", "[hwdecode]") {
    ScreenGrid grid;
    // PROJECT-like: a title and one accent label, no row-number grid or header.
    grid.handleFrame(makeCharFrame('T', 1 * 8, 60, 0, 252, 248, 0, 0, 0));
    grid.handleFrame(makeCharFrame('E', 2 * 8, 60, 0, 252, 248, 0, 0, 0));
    REQUIRE_FALSE(m8::dev::gridCursorPosition(grid).valid);
}

TEST_CASE("gridColumnEdges returns empty for a blank header row", "[hwdecode]") {
    ScreenGrid grid;
    grid.handleFrame(makeCharFrame('0', 8, 60, 255, 255, 255, 0, 0, 0));
    REQUIRE(m8::dev::gridColumnEdges(grid, 50).empty());
}

TEST_CASE("cursorRowY still finds a form-screen cursor label", "[hwdecode]") {
    ScreenGrid grid;
    const uint8_t A0 = 0, A1 = 252, A2 = 248;

    // PROJECT-style: white title, then an accent label on the cursor's row.
    grid.handleFrame(makeCharFrame('P', 0, 30, 255, 255, 255, 0, 0, 0));
    grid.handleFrame(makeCharFrame('T', 1 * 8, 60, A0, A1, A2, 0, 0, 0));
    grid.handleFrame(makeCharFrame('R', 2 * 8, 60, A0, A1, A2, 0, 0, 0));
    REQUIRE(grid.cursorRowY() == 60);
}

TEST_CASE("ScreenGrid topHeader extracts title", "[hwdecode]") {
    ScreenGrid grid;
    // Row 10: "S", "O", "N", "G" at x=0,8,16,24
    grid.handleFrame(makeCharFrame('S', 0, 10, 255, 255, 255, 0, 0, 0));
    grid.handleFrame(makeCharFrame('O', 8, 10, 255, 255, 255, 0, 0, 0));
    grid.handleFrame(makeCharFrame('N', 16, 10, 255, 255, 255, 0, 0, 0));
    grid.handleFrame(makeCharFrame('G', 24, 10, 255, 255, 255, 0, 0, 0));

    CHECK(grid.topHeader() == "SONG");
}

TEST_CASE("ScreenGrid cursorMainText", "[hwdecode]") {
    ScreenGrid grid;
    // Cursor on "FWD" at row 50.
    grid.handleFrame(makeCharFrame('F', 0, 50, 0, 252, 248, 0, 0, 0));
    grid.handleFrame(makeCharFrame('W', 8, 50, 0, 252, 248, 0, 0, 0));
    grid.handleFrame(makeCharFrame('D', 16, 50, 0, 252, 248, 0, 0, 0));
    // Non-cursor text on same row.
    grid.handleFrame(makeCharFrame('X', 24, 50, 255, 255, 255, 0, 0, 0));

    std::string txt = grid.cursorMainText();
    // cursorMainText joins per row, so should contain "FWD".
    CHECK(txt.find("FWD") != std::string::npos);
    CHECK(txt.find("X") == std::string::npos);  // non-cursor excluded
}

TEST_CASE("ScreenGrid mainRows", "[hwdecode]") {
    ScreenGrid grid;
    // Two rows of text.
    grid.handleFrame(makeCharFrame('A', 0, 10, 255, 255, 255, 0, 0, 0));
    grid.handleFrame(makeCharFrame('B', 8, 10, 255, 255, 255, 0, 0, 0));
    grid.handleFrame(makeCharFrame('C', 0, 20, 255, 255, 255, 0, 0, 0));

    auto rows = grid.mainRows();
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].second == "AB");
    CHECK(rows[1].second == "C");
}

TEST_CASE("ScreenGrid cursorRowY", "[hwdecode]") {
    ScreenGrid grid;
    // Cursor at row 30, non-cursor at row 20.
    grid.handleFrame(makeCharFrame('X', 0, 20, 255, 255, 255, 0, 0, 0));
    grid.handleFrame(makeCharFrame('Y', 0, 30, 0, 252, 248, 0, 0, 0));

    CHECK(grid.cursorRowY() == 30);
}

TEST_CASE("ScreenGrid findField", "[hwdecode]") {
    ScreenGrid grid;
    // "TEMPO" label at row 20.
    const char* label = "TEMPO";
    for (int i = 0; i < 5; ++i)
        grid.handleFrame(makeCharFrame(label[i], i * 8, 20, 100, 100, 100, 0, 0, 0));

    auto field = grid.findField("TEMPO");
    REQUIRE(field.has_value());
    CHECK(field->name == "TEMPO");
    CHECK(field->row == 20);
}

TEST_CASE("ScreenGrid valueAt", "[hwdecode]") {
    ScreenGrid grid;
    // Value "140" at col 100, row 20.
    grid.handleFrame(makeCharFrame('1', 100, 20, 255, 255, 255, 0, 0, 0));
    grid.handleFrame(makeCharFrame('4', 108, 20, 255, 255, 255, 0, 0, 0));
    grid.handleFrame(makeCharFrame('0', 116, 20, 255, 255, 255, 0, 0, 0));

    auto val = grid.valueAt(100, 20);
    REQUIRE(val.has_value());
    CHECK(*val == "140");
}

// ---- SlipDecoder tests ----------------------------------------------------

TEST_CASE("SlipDecoder basic frame", "[hwdecode]") {
    SlipDecoder slip;
    std::vector<uint8_t> out;

    // Simple frame: END data END
    CHECK_FALSE(slip.feed(0xC0, out));  // opening END (empty frame, ignored)
    CHECK_FALSE(slip.feed('A', out));
    CHECK_FALSE(slip.feed('B', out));
    CHECK(slip.feed(0xC0, out));        // closing END
    REQUIRE(out.size() == 2);
    CHECK(out[0] == 'A');
    CHECK(out[1] == 'B');
}

TEST_CASE("SlipDecoder escape sequences", "[hwdecode]") {
    SlipDecoder slip;
    std::vector<uint8_t> out;

    CHECK_FALSE(slip.feed(0xC0, out));
    CHECK_FALSE(slip.feed(0xDB, out));  // ESC
    CHECK_FALSE(slip.feed(0xDC, out));  // ESC_END -> 0xC0
    CHECK(slip.feed(0xC0, out));
    REQUIRE(out.size() == 1);
    CHECK(out[0] == 0xC0);
}

// ---- ScreenModel tests ----------------------------------------------------

TEST_CASE("identifyScreen", "[hwdecode]") {
    CHECK(identifyScreen("SONG") == Screen::SONG);
    CHECK(identifyScreen("CHAIN") == Screen::CHAIN);
    CHECK(identifyScreen("PHRASE") == Screen::PHRASE);
    CHECK(identifyScreen("INST.") == Screen::INSTRUMENT);
    CHECK(identifyScreen("TABLE") == Screen::TABLE);
    CHECK(identifyScreen("PROJECT") == Screen::PROJECT);
    CHECK(identifyScreen("GROOVE") == Screen::GROOVE);
    CHECK(identifyScreen("SCALE") == Screen::SCALE);
    CHECK(identifyScreen("MIXER") == Screen::MIXER);
    CHECK(identifyScreen("EFFECTS") == Screen::EFFECTS);
    CHECK(identifyScreen("LOADPROJECT") == Screen::LOAD_PROJECT_MODAL);
    CHECK(identifyScreen("INSTPOOL") == Screen::INST_POOL);
}

TEST_CASE("identifyScreen: hex IDs ending in a letter (A-F)", "[hwdecode]") {
    // Regression: hardware-confirmed 2026-07-18 (real M8 fw 6.5.2). Phrase/
    // chain/table IDs are hex (00-FF); stripDigits only strips trailing
    // decimal digits, so an ID ending in A-F (e.g. phrase 0xFC, table 0x0E)
    // used to leave identifyScreen returning UNKNOWN, which broke gotoScreen
    // for roughly half of all possible IDs.
    CHECK(identifyScreen("PHRASEFC") == Screen::PHRASE);
    CHECK(identifyScreen("TABLE0E") == Screen::TABLE);
    CHECK(identifyScreen("CHAINAB") == Screen::CHAIN);
    // Decimal-suffixed IDs must still work (previous behavior, unaffected).
    CHECK(identifyScreen("PHRASE01") == Screen::PHRASE);
    CHECK(identifyScreen("TABLE00") == Screen::TABLE);
    // TABLE's own bare name ends in the hex digit 'E' -- must not be
    // corrupted by an overly aggressive hex-suffix stripper.
    CHECK(identifyScreen("TABLE") == Screen::TABLE);
}

TEST_CASE("computeRoute SONG to PROJECT", "[hwdecode]") {
    auto steps = computeRoute(Screen::SONG, Screen::PROJECT);
    REQUIRE_FALSE(steps.empty());
    // SONG is at (0,0), PROJECT is at (0,1) — one step UP.
    CHECK(steps.size() == 1);
    CHECK(steps[0].keyMask == (Key::SHIFT | Key::UP));
    CHECK(steps[0].viaScreen == Screen::PROJECT);
}

TEST_CASE("computeRoute SONG to TABLE", "[hwdecode]") {
    auto steps = computeRoute(Screen::SONG, Screen::TABLE);
    REQUIRE_FALSE(steps.empty());
    // SONG(0,0) -> TABLE(4,0) — four steps RIGHT.
    CHECK(steps.size() == 4);
    for (auto& s : steps) {
        CHECK(s.keyMask == (Key::SHIFT | Key::RIGHT));
    }
}

TEST_CASE("computeRoute same screen returns empty", "[hwdecode]") {
    auto steps = computeRoute(Screen::SONG, Screen::SONG);
    CHECK(steps.empty());
}

TEST_CASE("gridDims", "[hwdecode]") {
    CHECK(gridDims(Screen::SONG).rows == 16);
    CHECK(gridDims(Screen::SONG).cols == 8);
    CHECK(gridDims(Screen::PHRASE).cols == 9);
    CHECK(gridDims(Screen::TABLE).rows == 16);
    CHECK(gridDims(Screen::GROOVE).cols == 1);
}

// ---- Multi-frame scenario: build a "SONG" screen and verify identity -------

TEST_CASE("Full screen identity scenario", "[hwdecode]") {
    ScreenGrid grid;

    // System info.
    grid.handleFrame(makeSysInfoFrame(3, 6, 5, 2, 0));

    // Title "SONG" at row 10, x=0..24.
    grid.handleFrame(makeCharFrame('S', 0,  10, 255, 60, 60, 0, 0, 0));
    grid.handleFrame(makeCharFrame('O', 8,  10, 255, 60, 60, 0, 0, 0));
    grid.handleFrame(makeCharFrame('N', 16, 10, 255, 60, 60, 0, 0, 0));
    grid.handleFrame(makeCharFrame('G', 24, 10, 255, 60, 60, 0, 0, 0));

    // Some track data.
    grid.handleFrame(makeCharFrame('0', 0,  30, 100, 100, 100, 0, 0, 0));
    grid.handleFrame(makeCharFrame('0', 8,  30, 100, 100, 100, 0, 0, 0));

    // Cursor on a chain value.
    grid.handleFrame(makeCharFrame('0', 16, 30, 0, 252, 248, 0, 0, 0));

    CHECK(grid.topHeader() == "SONG");
    CHECK(grid.canon() == "SONG");
    CHECK(grid.firmware().hwType == 3);
    CHECK(grid.firmware().major == 6);

    Screen id = identifyScreen(grid.canon());
    CHECK(id == Screen::SONG);
}

// ---- Instrument field-map selection tests ----------------------------------
//
// Verifies that getFieldMap / findFieldOnScreen select the correct field map
// (Sampler vs MacroSynth) based on the instrument type hint.

// Build a synthetic Sampler instrument screen.
// TYPE at row 2 shows "SAMPLER", SAMPLE at row 6, SLICE at row 8, START at row 10.
static ScreenGrid makeSamplerGrid() {
    ScreenGrid grid;
    // Header "INST." at row 10.
    grid.handleFrame(makeCharFrame('I', 0,  10, 255, 60, 60, 0, 0, 0));
    grid.handleFrame(makeCharFrame('N', 8,  10, 255, 60, 60, 0, 0, 0));
    grid.handleFrame(makeCharFrame('S', 16, 10, 255, 60, 60, 0, 0, 0));
    grid.handleFrame(makeCharFrame('T', 24, 10, 255, 60, 60, 0, 0, 0));
    grid.handleFrame(makeCharFrame('.', 32, 10, 255, 60, 60, 0, 0, 0));
    // "TYPE" label at row 20, "SAMPLER" value at row 20.
    for (int i = 0; i < 4; ++i) {
        const char* label = "TYPE";
        grid.handleFrame(makeCharFrame(label[i], i * 8, 20, 100, 100, 100, 0, 0, 0));
    }
    const char* val = "SAMPLER";
    for (int i = 0; i < 7; ++i) {
        grid.handleFrame(makeCharFrame(val[i], 40 + i * 8, 20, 200, 200, 200, 0, 0, 0));
    }
    // "SAMPLE" label at row 60.
    for (int i = 0; i < 6; ++i) {
        const char* label = "SAMPLE";
        grid.handleFrame(makeCharFrame(label[i], i * 8, 60, 100, 100, 100, 0, 0, 0));
    }
    // "SLICE" label at row 80.
    for (int i = 0; i < 5; ++i) {
        const char* label = "SLICE";
        grid.handleFrame(makeCharFrame(label[i], i * 8, 80, 100, 100, 100, 0, 0, 0));
    }
    // "START" label at row 100.
    for (int i = 0; i < 5; ++i) {
        const char* label = "START";
        grid.handleFrame(makeCharFrame(label[i], i * 8, 100, 100, 100, 100, 0, 0, 0));
    }
    // "LOOP ST" label at row 110.
    for (int i = 0; i < 7; ++i) {
        const char* label = "LOOP ST";
        grid.handleFrame(makeCharFrame(label[i], i * 8, 110, 100, 100, 100, 0, 0, 0));
    }
    return grid;
}

// Build a synthetic MacroSynth instrument screen.
// TYPE at row 2 shows "MACROSYN", SHAPE at row 6, TIMBRE at row 8, COLOR at row 9.
static ScreenGrid makeMacrosynGrid() {
    ScreenGrid grid;
    // Header "INST." at row 10.
    grid.handleFrame(makeCharFrame('I', 0,  10, 255, 60, 60, 0, 0, 0));
    grid.handleFrame(makeCharFrame('N', 8,  10, 255, 60, 60, 0, 0, 0));
    grid.handleFrame(makeCharFrame('S', 16, 10, 255, 60, 60, 0, 0, 0));
    grid.handleFrame(makeCharFrame('T', 24, 10, 255, 60, 60, 0, 0, 0));
    grid.handleFrame(makeCharFrame('.', 32, 10, 255, 60, 60, 0, 0, 0));
    // "TYPE" label at row 20, "MACROSYN" value at row 20.
    for (int i = 0; i < 4; ++i) {
        const char* label = "TYPE";
        grid.handleFrame(makeCharFrame(label[i], i * 8, 20, 100, 100, 100, 0, 0, 0));
    }
    const char* val = "MACROSYN";
    for (int i = 0; i < 8; ++i) {
        grid.handleFrame(makeCharFrame(val[i], 40 + i * 8, 20, 200, 200, 200, 0, 0, 0));
    }
    // "SHAPE" label at row 60.
    for (int i = 0; i < 5; ++i) {
        const char* label = "SHAPE";
        grid.handleFrame(makeCharFrame(label[i], i * 8, 60, 100, 100, 100, 0, 0, 0));
    }
    // "TIMBRE" label at row 80.
    for (int i = 0; i < 6; ++i) {
        const char* label = "TIMBRE";
        grid.handleFrame(makeCharFrame(label[i], i * 8, 80, 100, 100, 100, 0, 0, 0));
    }
    // "COLOR" label at row 90.
    for (int i = 0; i < 5; ++i) {
        const char* label = "COLOR";
        grid.handleFrame(makeCharFrame(label[i], i * 8, 90, 100, 100, 100, 0, 0, 0));
    }
    // "CUTOFF" label at row 130 (present in both layouts).
    for (int i = 0; i < 6; ++i) {
        const char* label = "CUTOFF";
        grid.handleFrame(makeCharFrame(label[i], i * 8, 130, 100, 100, 100, 0, 0, 0));
    }
    return grid;
}

TEST_CASE("getFieldMap type-aware: Sampler map for SAMPLER", "[hwdecode]") {
    auto map = getFieldMap(Screen::INSTRUMENT, "SAMPLER");
    CHECK_FALSE(map.isGrid);
    CHECK(map.count > 0);

    // SAMPLER-specific fields should be in the map.
    bool hasSample = false, hasSlice = false, hasStart = false, hasLoopSt = false;
    for (size_t i = 0; i < map.count; ++i) {
        std::string n = map.fields[i].name;
        if (n == "SAMPLE")  hasSample = true;
        if (n == "SLICE")   hasSlice = true;
        if (n == "START")   hasStart = true;
        if (n == "LOOP_ST") hasLoopSt = true;
    }
    CHECK(hasSample);
    CHECK(hasSlice);
    CHECK(hasStart);
    CHECK(hasLoopSt);

    // MacroSynth-only fields should NOT be in the Sampler map.
    bool hasShape = false, hasTimbre = false, hasColor = false;
    for (size_t i = 0; i < map.count; ++i) {
        std::string n = map.fields[i].name;
        if (n == "SHAPE")  hasShape = true;
        if (n == "TIMBRE") hasTimbre = true;
        if (n == "COLOR")  hasColor = true;
    }
    CHECK_FALSE(hasShape);
    CHECK_FALSE(hasTimbre);
    CHECK_FALSE(hasColor);
}

TEST_CASE("getFieldMap type-aware: MacroSynth map for MACROSYN", "[hwdecode]") {
    auto map = getFieldMap(Screen::INSTRUMENT, "MACROSYN");
    CHECK_FALSE(map.isGrid);
    CHECK(map.count > 0);

    // MacroSynth-specific fields should be in the map.
    bool hasShape = false, hasTimbre = false, hasColor = false;
    for (size_t i = 0; i < map.count; ++i) {
        std::string n = map.fields[i].name;
        if (n == "SHAPE")  hasShape = true;
        if (n == "TIMBRE") hasTimbre = true;
        if (n == "COLOR")  hasColor = true;
    }
    CHECK(hasShape);
    CHECK(hasTimbre);
    CHECK(hasColor);

    // Sampler-specific fields should NOT be in the MacroSynth map.
    bool hasSample = false, hasSlice = false, hasStart = false, hasLoopSt = false;
    for (size_t i = 0; i < map.count; ++i) {
        std::string n = map.fields[i].name;
        if (n == "SAMPLE")  hasSample = true;
        if (n == "SLICE")   hasSlice = true;
        if (n == "START")   hasStart = true;
        if (n == "LOOP_ST") hasLoopSt = true;
    }
    CHECK_FALSE(hasSample);
    CHECK_FALSE(hasSlice);
    CHECK_FALSE(hasStart);
    CHECK_FALSE(hasLoopSt);
}

TEST_CASE("getFieldMap type-aware: CUTOFF at different rows per layout", "[hwdecode]") {
    auto samplerMap = getFieldMap(Screen::INSTRUMENT, "SAMPLER");
    auto macrosynMap = getFieldMap(Screen::INSTRUMENT, "MACROSYN");

    // CUTOFF exists in both maps but at different rows.
    std::optional<FieldRef> samplerCutoff, macrosynCutoff;
    for (size_t i = 0; i < samplerMap.count; ++i) {
        if (std::string(samplerMap.fields[i].name) == "CUTOFF")
            samplerCutoff = FieldRef{samplerMap.fields[i].name,
                                     samplerMap.fields[i].col, samplerMap.fields[i].row};
    }
    for (size_t i = 0; i < macrosynMap.count; ++i) {
        if (std::string(macrosynMap.fields[i].name) == "CUTOFF")
            macrosynCutoff = FieldRef{macrosynMap.fields[i].name,
                                      macrosynMap.fields[i].col, macrosynMap.fields[i].row};
    }
    REQUIRE(samplerCutoff.has_value());
    REQUIRE(macrosynCutoff.has_value());

    // Sampler CUTOFF is at row 16; MacroSynth CUTOFF is at row 13.
    CHECK(samplerCutoff->row == 16);
    CHECK(macrosynCutoff->row == 13);
    CHECK(samplerCutoff->row != macrosynCutoff->row);
}

TEST_CASE("findScreenForField resolves Instrument via union of both layouts", "[hwdecode]") {
    // Regression: before navigating anywhere, the framebuffer isn't readable,
    // so findScreenForField can't know the instrument's actual type. It must
    // still resolve MacroSynth-only field names to Screen::INSTRUMENT (not
    // UNKNOWN) so editValue's initial screen lookup doesn't reject them
    // before ever reaching moveCursorTo's type-aware position resolution.
    CHECK(findScreenForField("SHAPE")  == Screen::INSTRUMENT);
    CHECK(findScreenForField("TIMBRE") == Screen::INSTRUMENT);
    CHECK(findScreenForField("COLOR")  == Screen::INSTRUMENT);
    CHECK(findScreenForField("REDUX")  == Screen::INSTRUMENT);
    // Sampler-only field names must resolve too.
    CHECK(findScreenForField("SLICE")    == Screen::INSTRUMENT);
    CHECK(findScreenForField("LOOP_ST")  == Screen::INSTRUMENT);
    // Shared field names still resolve (unchanged behavior).
    CHECK(findScreenForField("CUTOFF") == Screen::INSTRUMENT);
    // A genuinely nonexistent field must still resolve to UNKNOWN.
    CHECK(findScreenForField("NOT_A_REAL_FIELD_XYZ") == Screen::UNKNOWN);
}

TEST_CASE("findFieldOnScreen type-aware: SHAPE on MacroSynth only", "[hwdecode]") {
    // findFieldOnScreen with Sampler type should NOT find SHAPE.
    auto samplerResult = findFieldOnScreen(Screen::INSTRUMENT, "SHAPE", "SAMPLER");
    CHECK_FALSE(samplerResult.has_value());

    // findFieldOnScreen with MacroSynth type should find SHAPE.
    auto macrosynResult = findFieldOnScreen(Screen::INSTRUMENT, "SHAPE", "MACROSYN");
    REQUIRE(macrosynResult.has_value());
    CHECK(macrosynResult->name == "SHAPE");
}

TEST_CASE("findFieldOnScreen type-aware: SLICE on Sampler only", "[hwdecode]") {
    auto samplerResult = findFieldOnScreen(Screen::INSTRUMENT, "SLICE", "SAMPLER");
    REQUIRE(samplerResult.has_value());
    CHECK(samplerResult->name == "SLICE");

    // SLICE should NOT be found via the MacroSynth field map.
    // Note: findFieldOnScreen uses bidirectional substring matching, so we pick
    // "SLICE" which has no substring overlap with any MacroSynth field name.
    auto macrosynResult = findFieldOnScreen(Screen::INSTRUMENT, "SLICE", "MACROSYN");
    CHECK_FALSE(macrosynResult.has_value());
}

TEST_CASE("findFieldOnScreen type-aware: CUTOFF on both layouts", "[hwdecode]") {
    auto samplerResult = findFieldOnScreen(Screen::INSTRUMENT, "CUTOFF", "SAMPLER");
    REQUIRE(samplerResult.has_value());
    CHECK(samplerResult->row == 16);

    auto macrosynResult = findFieldOnScreen(Screen::INSTRUMENT, "CUTOFF", "MACROSYN");
    REQUIRE(macrosynResult.has_value());
    CHECK(macrosynResult->row == 13);
}

TEST_CASE("readInstrumentType on Sampler grid", "[hwdecode]") {
    auto grid = makeSamplerGrid();
    std::string type = readInstrumentType(grid);
    // Should detect SAMPLER from the TYPE row text.
    std::string upper = type;
    for (auto& c : upper)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    CHECK(upper.find("SAMPLER") != std::string::npos);
}

TEST_CASE("readInstrumentType on MacroSynth grid", "[hwdecode]") {
    auto grid = makeMacrosynGrid();
    std::string type = readInstrumentType(grid);
    std::string upper = type;
    for (auto& c : upper)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    CHECK(upper.find("MACROSYN") != std::string::npos);
}

// ---- Tier 5 script parsing tests ------------------------------------------

TEST_CASE("set_param.m8script parses without error", "[hwdecode]") {
    DeviceScriptRunner runner;
    bool loaded = runner.loadScript("tests/hw/set_param.m8script");
    REQUIRE(loaded);
    CHECK(runner.exitCode() == 0);
    CHECK(runner.loadScript_count() == 4);  // goto, cursor, set, assert_field
}

TEST_CASE("set_param.m8script field names exist in Sampler map", "[hwdecode]") {
    // Verify the field names used in the script are present in both field maps.
    auto samplerCut = findFieldOnScreen(Screen::INSTRUMENT, "CUTOFF", "SAMPLER");
    REQUIRE(samplerCut.has_value());
    CHECK(samplerCut->name == "CUTOFF");

    auto macrosynCut = findFieldOnScreen(Screen::INSTRUMENT, "CUTOFF", "MACROSYN");
    REQUIRE(macrosynCut.has_value());
    CHECK(macrosynCut->name == "CUTOFF");
}

// ---- Tier 4.5 offline regression tests ------------------------------------
//
// These test the settle-confirm and modal primitives offline using synthetic
// frames. They cannot test actual serial timing, but they verify the frame-
// matching logic that the primitives rely on.

// Build a synthetic SONG screen header.
static ScreenGrid makeSongGrid() {
    ScreenGrid grid;
    const char* title = "SONG";
    for (int i = 0; i < 4; ++i)
        grid.handleFrame(makeCharFrame(title[i], i * 8, 10, 255, 60, 60, 0, 0, 0));
    return grid;
}

// Build a synthetic PROJECT screen header.
static ScreenGrid makeProjectGrid() {
    ScreenGrid grid;
    const char* title = "PROJECT";
    for (int i = 0; i < 7; ++i)
        grid.handleFrame(makeCharFrame(title[i], i * 8, 10, 255, 60, 60, 0, 0, 0));
    return grid;
}

// Build a synthetic LOAD PROJECT modal header.
static ScreenGrid makeLoseChangesModal() {
    ScreenGrid grid;
    // Include space characters between words so topHeader() produces
    // "LOSE CHANGES TO CURRENT SONG?" (with spaces).
    const char* title = "LOSE CHANGES TO CURRENT SONG?";
    int x = 0;
    for (int i = 0; title[i]; ++i) {
        grid.handleFrame(makeCharFrame(title[i], x, 10, 255, 60, 60, 0, 0, 0));
        x += 8;
    }
    return grid;
}

TEST_CASE("identifyScreen: modal detection via isModal patterns", "[hwdecode]") {
    // Verify identifyScreen correctly identifies modals by header text.
    auto modal = makeLoseChangesModal();
    CHECK(identifyScreen(modal.canon()) == Screen::LOAD_PROJECT_MODAL);

    auto song = makeSongGrid();
    CHECK(identifyScreen(song.canon()) == Screen::SONG);

    auto proj = makeProjectGrid();
    CHECK(identifyScreen(proj.canon()) == Screen::PROJECT);
}

TEST_CASE("ScreenGrid: cursorMainText on accent-colored cells", "[hwdecode]") {
    // Regression for Tier 4.5 item 2: verify cursorMainText returns only
    // accent-colored cells, not the entire row.
    ScreenGrid grid;
    // Non-cursor text on a row.
    grid.handleFrame(makeCharFrame('A', 0, 50, 100, 100, 100, 0, 0, 0));
    grid.handleFrame(makeCharFrame('M', 8, 50, 100, 100, 100, 0, 0, 0));
    grid.handleFrame(makeCharFrame('P', 16, 50, 100, 100, 100, 0, 0, 0));
    // Accent-colored cells on the same row (cursor value).
    grid.handleFrame(makeCharFrame('4', 40, 50, 0, 252, 248, 0, 0, 0));
    grid.handleFrame(makeCharFrame('0', 48, 50, 0, 252, 248, 0, 0, 0));

    std::string txt = grid.cursorMainText();
    CHECK(txt.find("40") != std::string::npos);
    // Non-accent cells should not appear.
    CHECK(txt.find("AMP") == std::string::npos);
}

TEST_CASE("dismissModal: detects modal present and absent", "[hwdecode]") {
    // Verify the isModal predicate that dismissModal uses.
    // Note: topHeader() filters space characters, so the header is
    // "LOSECHANGESTOCURRENTSONG?" not "LOSE CHANGES TO CURRENT SONG?".
    auto modal = makeLoseChangesModal();
    std::string h = modal.topHeader();
    std::string upper = h;
    for (auto& c : upper)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    // The isModal() predicate checks for "LOSE CHANGES" or "SONG?".
    // topHeader() strips spaces, so check the no-space variant.
    CHECK(upper.find("LOSECHANGES") != std::string::npos);
    CHECK(upper.find("SONG?") != std::string::npos);

    auto song = makeSongGrid();
    std::string sh = song.topHeader();
    std::string supper = sh;
    for (auto& c : supper)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    CHECK(supper.find("LOSECHANGES") == std::string::npos);
    CHECK(supper.find("SONG?") == std::string::npos);
}

TEST_CASE("computeRoute: all 12 non-modal screens to PROJECT", "[hwdecode]") {
    // Verify computeRoute returns non-empty routes from every non-modal screen
    // to PROJECT. This is the route table that gotoScreen relies on.
    Screen targets[] = {
        Screen::SONG, Screen::CHAIN, Screen::PHRASE, Screen::INSTRUMENT,
        Screen::TABLE, Screen::GROOVE, Screen::MODS, Screen::SCALE,
        Screen::INST_POOL, Screen::MIXER, Screen::EFFECTS, Screen::PROJECT
    };
    for (auto t : targets) {
        auto steps = computeRoute(t, Screen::PROJECT);
        if (t == Screen::PROJECT) {
            CHECK(steps.empty());  // same screen = no steps
        } else {
            CHECK_FALSE(steps.empty());  // all others need at least one hop
        }
    }
}

TEST_CASE("findFieldOnScreen: every field in kProjectFields is findable", "[hwdecode]") {
    for (size_t i = 0; i < std::size(kProjectFields); ++i) {
        auto f = findFieldOnScreen(Screen::PROJECT, kProjectFields[i].name);
        REQUIRE(f.has_value());
        CHECK(f->name == std::string(kProjectFields[i].name));
    }
}

TEST_CASE("findFieldOnScreen: every field in kEffectsFields is findable", "[hwdecode]") {
    for (size_t i = 0; i < std::size(kEffectsFields); ++i) {
        auto f = findFieldOnScreen(Screen::EFFECTS, kEffectsFields[i].name);
        REQUIRE(f.has_value());
        CHECK(f->name == std::string(kEffectsFields[i].name));
    }
}

TEST_CASE("findFieldOnScreen: every field in kMixerFields is findable", "[hwdecode]") {
    for (size_t i = 0; i < std::size(kMixerFields); ++i) {
        auto f = findFieldOnScreen(Screen::MIXER, kMixerFields[i].name);
        REQUIRE(f.has_value());
        CHECK(f->name == std::string(kMixerFields[i].name));
    }
}

TEST_CASE("findFieldOnScreen: every field in kScaleFields is findable", "[hwdecode]") {
    for (size_t i = 0; i < std::size(kScaleFields); ++i) {
        auto f = findFieldOnScreen(Screen::SCALE, kScaleFields[i].name);
        REQUIRE(f.has_value());
        CHECK(f->name == std::string(kScaleFields[i].name));
    }
}

TEST_CASE("findFieldOnScreen: every Sampler field is findable with SAMPLER type", "[hwdecode]") {
    for (size_t i = 0; i < std::size(kInstrumentSamplerFields); ++i) {
        auto f = findFieldOnScreen(Screen::INSTRUMENT, kInstrumentSamplerFields[i].name, "SAMPLER");
        REQUIRE(f.has_value());
        CHECK(f->name == std::string(kInstrumentSamplerFields[i].name));
    }
}

TEST_CASE("findFieldOnScreen: every MacroSynth field is findable with MACROSYN type", "[hwdecode]") {
    for (size_t i = 0; i < std::size(kInstrumentMacrosynFields); ++i) {
        auto f = findFieldOnScreen(Screen::INSTRUMENT, kInstrumentMacrosynFields[i].name, "MACROSYN");
        REQUIRE(f.has_value());
        CHECK(f->name == std::string(kInstrumentMacrosynFields[i].name));
    }
}

TEST_CASE("identifyScreen: overload disambiguates PROJECT vs LOAD_PROJECT_MODAL", "[hwdecode]") {
    // 1. PROJECT header settings screen with no LOAD highlighted.
    ScreenGrid gridProj = makeProjectGrid();
    CHECK(identifyScreen(gridProj) == Screen::PROJECT);

    // 2. PROJECT settings screen, but cursor is on LOAD button, so M8 changes header to "LOAD PROJECT".
    ScreenGrid gridLoadFocus;
    const char* headerFocus = "LOAD PROJECT";
    for (int i = 0; headerFocus[i]; ++i) {
        gridLoadFocus.handleFrame(makeCharFrame(headerFocus[i], i * 8, 10, 255, 60, 60, 0, 0, 0));
    }
    // Add "TEMPO" to main rows to simulate settings screen.
    const char* tempoText = "TEMPO";
    for (int i = 0; tempoText[i]; ++i) {
        gridLoadFocus.handleFrame(makeCharFrame(tempoText[i], i * 8, 20, 255, 255, 255, 0, 0, 0));
    }
    CHECK(identifyScreen(gridLoadFocus) == Screen::PROJECT);

    // 3. Genuinely in LOAD_PROJECT modal (file browser).
    ScreenGrid gridModal;
    const char* headerModal = "LOAD PROJECT";
    for (int i = 0; headerModal[i]; ++i) {
        gridModal.handleFrame(makeCharFrame(headerModal[i], i * 8, 10, 255, 60, 60, 0, 0, 0));
    }
    CHECK(identifyScreen(gridModal) == Screen::LOAD_PROJECT_MODAL);
}

TEST_CASE("identifyScreen: detects GROOVE when truncated to GROOV", "[hwdecode]") {
    ScreenGrid grid;
    const char* title = "GROOV";
    for (int i = 0; title[i]; ++i) {
        grid.handleFrame(makeCharFrame(title[i], i * 8, 10, 255, 60, 60, 0, 0, 0));
    }
    CHECK(identifyScreen(grid.canon()) == Screen::GROOVE);
    CHECK(identifyScreen(grid) == Screen::GROOVE);
}

TEST_CASE("computeRoute: restricted horizontal moves to y=0 only", "[hwdecode]") {
    // From GROOVE (2, 1) to PROJECT (0, 1):
    // Should drop down to PHRASE (2, 0), move to SONG (0, 0) via CHAIN (1, 0), and climb to PROJECT (0, 1)
    auto steps = computeRoute(Screen::GROOVE, Screen::PROJECT);
    REQUIRE(steps.size() == 4);
    
    // Step 1: SHIFT+DOWN to PHRASE (2, 0)
    CHECK(steps[0].keyMask == (Key::SHIFT | Key::DOWN));
    CHECK(steps[0].viaScreen == Screen::PHRASE);
    
    // Step 2: SHIFT+LEFT to CHAIN (1, 0)
    CHECK(steps[1].keyMask == (Key::SHIFT | Key::LEFT));
    CHECK(steps[1].viaScreen == Screen::CHAIN);
    
    // Step 3: SHIFT+LEFT to SONG (0, 0)
    CHECK(steps[2].keyMask == (Key::SHIFT | Key::LEFT));
    CHECK(steps[2].viaScreen == Screen::SONG);
    
    // Step 4: SHIFT+UP to PROJECT (0, 1)
    CHECK(steps[3].keyMask == (Key::SHIFT | Key::UP));
    CHECK(steps[3].viaScreen == Screen::PROJECT);
}

TEST_CASE("M8Device ReadStats telemetry offline", "[m8_device]") {
    M8Device dev;
    const auto& stats = dev.lastRead();
    CHECK(stats.elapsedMs == 0);
    CHECK(stats.quietMs == 0);
    CHECK(stats.framesSeen == 0);
    CHECK_FALSE(stats.settled);
    CHECK_FALSE(stats.timedOut);
}

TEST_CASE("ScreenGrid listRows filters y=30 to y=210", "[m8_device]") {
    ScreenGrid grid;
    // Row at y=10 (top header, should be excluded from listRows)
    grid.handleFrame(makeCharFrame('H', 0, 10, 255, 255, 255, 0, 0, 0));
    // Row at y=50 (main area, should be included)
    grid.handleFrame(makeCharFrame('M', 0, 50, 255, 255, 255, 0, 0, 0));
    // Row at y=220 (footer, should be excluded)
    grid.handleFrame(makeCharFrame('F', 0, 220, 255, 255, 255, 0, 0, 0));

    auto rows = grid.listRows();
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].first == 50);
    CHECK(rows[0].second == "M");
}

TEST_CASE("semanticState extraction and toJson", "[m8_device]") {
    M8Device dev;
    auto state = semanticState(dev);
    CHECK(state.screen == Screen::UNKNOWN);
    CHECK_FALSE(state.isModal);
    CHECK_FALSE(state.isLiveMode);
    CHECK_FALSE(state.settled);

    std::string json = state.toJson();
    CHECK(json.find("\"screen\":") != std::string::npos);
    CHECK(json.find("\"is_modal\": false") != std::string::npos);
    CHECK(json.find("\"rows\":") != std::string::npos);
}

TEST_CASE("captureFromGrid col/row at non-symmetric pixel position", "[hwdecode]") {
    // Regression: UiCapture.cpp used to read cells keyed (y,x) as (x,y),
    // swapping col and row. A glyph at x=80,y=10 must produce col=10,row=1
    // (not col=1,row=10). This position is not diagonally symmetric, so a
    // swap would fail the assertions.
    ScreenGrid grid;
    grid.handleFrame(makeCharFrame('A', 80, 10, 255, 255, 255, 0, 0, 0));
    auto cap = captureFromGrid(grid, true, "TEST");

    REQUIRE(cap.cells.size() == 1);
    CHECK(cap.cells[0].col == 10);   // 80 / pitchX(8)
    CHECK(cap.cells[0].row == 1);    // 10 / pitchY(10)
}

TEST_CASE("ScreenGrid printJson emits highlights array", "[m8_device]") {
    ScreenGrid grid;
    grid.highlights.push_back({10, 20, 30, 40, {255, 0, 0}});
    grid.handleFrame(makeCharFrame('X', 10, 20, 255, 255, 255, 0, 0, 0));

    std::string tmpPath = "test_out_ui/test_highlights.json";
    grid.printJson(tmpPath);

    std::ifstream in(tmpPath);
    REQUIRE(in.is_open());
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(content.find("\"highlights\": [") != std::string::npos);
    CHECK(content.find("\"x\":10") != std::string::npos);
    CHECK(content.find("\"y\":20") != std::string::npos);
    CHECK(content.find("\"w\":30") != std::string::npos);
    CHECK(content.find("\"h\":40") != std::string::npos);
}

// ---- Task 1a: palette id remapping bug test ---------------------------------
//
// buildPalette() sorts the palette by RGB but never remaps the cells'
// fgStyle/bgStyle values. This test builds a grid whose colours are inserted
// in a non-RGB-sorted order, runs captureFromGrid(), and asserts that every
// cell's palette index resolves to the RGB that was put into the grid.

TEST_CASE("captureFromGrid palette ids resolve to correct RGB after sort", "[hwdecode]") {
    // Insert colours in a non-sorted order:
    //   cell (0,0): fg=(248,252,248) white, bg=(0,0,0) black
    //   cell (1,0): fg=(0,252,248)   cyan,   bg=(0,0,0) black
    //
    // First-seen order: white=0, black=1, cyan=2
    // Sorted RGB order: black=0, cyan=1, white=2
    //
    // After a correct remap, cell (0,0).fgStyle must be 2 (white), not 0.
    // After a correct remap, cell (1,0).fgStyle must be 1 (cyan), not 2.
    ScreenGrid grid;
    grid.handleFrame(makeCharFrame('A', 0,  0, 248, 252, 248, 0, 0, 0));  // fg=white, bg=black
    grid.handleFrame(makeCharFrame('B', 8,  0,   0, 252, 248, 0, 0, 0));  // fg=cyan,   bg=black

    auto cap = captureFromGrid(grid, true, "TEST");

    // Expected sorted palette: [0,0,0]=0, [0,252,248]=1, [248,252,248]=2
    REQUIRE(cap.palette.size() == 3);

    // Verify the sorted palette order.
    CHECK(cap.palette[0] == (std::array<uint8_t,3>{0, 0, 0}));
    CHECK(cap.palette[1] == (std::array<uint8_t,3>{0, 252, 248}));
    CHECK(cap.palette[2] == (std::array<uint8_t,3>{248, 252, 248}));

    REQUIRE(cap.cells.size() == 2);

    // For each cell, the palette id must resolve to the original RGB.
    for (const auto& cell : cap.cells) {
        REQUIRE(cell.fgStyle >= 0);
        REQUIRE(cell.fgStyle < (int)cap.palette.size());
        REQUIRE(cell.bgStyle >= 0);
        REQUIRE(cell.bgStyle < (int)cap.palette.size());

        if (cell.ch == 'A') {
            // fg was white (248,252,248) → must resolve to palette[2]
            CHECK(cap.palette[cell.fgStyle] == (std::array<uint8_t,3>{248, 252, 248}));
            // bg was black (0,0,0) → must resolve to palette[0]
            CHECK(cap.palette[cell.bgStyle] == (std::array<uint8_t,3>{0, 0, 0}));
        } else if (cell.ch == 'B') {
            // fg was cyan (0,252,248) → must resolve to palette[1]
            CHECK(cap.palette[cell.fgStyle] == (std::array<uint8_t,3>{0, 252, 248}));
            // bg was black (0,0,0) → must resolve to palette[0]
            CHECK(cap.palette[cell.bgStyle] == (std::array<uint8_t,3>{0, 0, 0}));
        }
    }
}

// ---- Transport visibility (2026-08-15) -------------------------------------
//
// MEASURED on fw 6.5.2: the M8 marks the playing step with a '>' (0x3E) in the
// row-label gutter of a grid screen and removes it on stop. Nothing else on
// screen changes -- no colour moves at all -- which is why `inspect` reported
// "the press is not landing" for a PLAY that had worked. Without this signal
// SemanticState had no transport field, so any playback-dependent probe was
// unverifiable, and one measurement was abandoned because of it.

TEST_CASE("playheadVisible sees the gutter marker", "[hwdecode]") {
    ScreenGrid grid;
    CHECK_FALSE(playheadVisible(grid));                    // nothing drawn yet

    // Row label "5" then the playhead '>' beside it, as the device draws it.
    grid.handleFrame(makeCharFrame('5', 8,  110, 255, 255, 255, 0, 0, 0));
    CHECK_FALSE(playheadVisible(grid));
    grid.handleFrame(makeCharFrame('>', 16, 110, 255, 255, 255, 0, 0, 0));
    CHECK(playheadVisible(grid));
}

TEST_CASE("playheadVisible ignores the cursor marker and the chrome", "[hwdecode]") {
    // '<' at x == 0 is the CURSOR indicator, not the playhead.
    ScreenGrid cursorOnly;
    cursorOnly.handleFrame(makeCharFrame('<', 0, 60, 255, 255, 255, 0, 0, 0));
    CHECK_FALSE(playheadVisible(cursorOnly));

    // The chrome's tempo readout "T>120" also contains a '>', but it lives past
    // MAIN_X_MAX in the right-hand column. Counting it would report "playing"
    // on every screen forever.
    ScreenGrid chrome;
    chrome.handleFrame(makeCharFrame('T', ScreenGrid::MAIN_X_MAX + 16, 20, 100, 100, 100, 0, 0, 0));
    chrome.handleFrame(makeCharFrame('>', ScreenGrid::MAIN_X_MAX + 24, 20, 100, 100, 100, 0, 0, 0));
    CHECK_FALSE(playheadVisible(chrome));
}

// ---- Compound grid columns (2026-08-15) ------------------------------------
//
// MEASURED by walking a row one press at a time and watching the reported
// column: on PHRASE, leaving groups 3/4/5 took TWO presses each while 0/1/2 took
// one; on TABLE the same held for groups 2/3/4. Those are the FX cells, drawn as
// `SCG10` -- a 3-glyph command and a 2-glyph value, each its own cursor stop
// under a single header label.

TEST_CASE("gridSubStops splits FX cells only", "[hwdecode]") {
    for (int g = 0; g <= 2; ++g) CHECK(gridSubStops(Screen::PHRASE, g) == 1);
    for (int g = 3; g <= 5; ++g) CHECK(gridSubStops(Screen::PHRASE, g) == 2);

    for (int g = 0; g <= 1; ++g) CHECK(gridSubStops(Screen::TABLE, g) == 1);
    for (int g = 2; g <= 4; ++g) CHECK(gridSubStops(Screen::TABLE, g) == 2);

    // SONG's columns are single values; its 8 header groups are its 8 stops.
    for (int g = 0; g <= 7; ++g) CHECK(gridSubStops(Screen::SONG, g) == 1);
}

TEST_CASE("gridDims counts cursor stops, not header groups", "[hwdecode]") {
    // PHRASE: N V I + three FX cells of two stops each.
    CHECK(gridDims(Screen::PHRASE).cols == 9);
    // TABLE said 5 until 2026-08-15 -- it counted header groups, so the FX
    // values were uncounted and unreachable.
    CHECK(gridDims(Screen::TABLE).cols == 8);
    CHECK(gridDims(Screen::SONG).cols == 8);
}

TEST_CASE("isGridScreen agrees with gridDims", "[hwdecode]") {
    for (Screen s : {Screen::SONG, Screen::CHAIN, Screen::PHRASE,
                     Screen::TABLE, Screen::GROOVE, Screen::INST_POOL}) {
        CHECK(isGridScreen(s));
        CHECK(gridDims(s).cols > 0);
    }
    for (Screen s : {Screen::PROJECT, Screen::INSTRUMENT, Screen::SCALE,
                     Screen::MIXER, Screen::MODS}) {
        CHECK_FALSE(isGridScreen(s));
        CHECK(gridDims(s).cols == 0);
    }
}


// Regression: toJson() escaped only quote and backslash, so a cell carrying one
// of the M8 font custom glyphs (the meter and slider fills live outside
// printable ASCII) wrote a raw byte into the file. The result was not valid
// JSON: the hand-rolled fromJson() below read it fine, so nothing in the C++
// tests noticed, while `m8drv inspect` -- which parses the same file with
// Python -- died with a JSONDecodeError and reported nothing at all.
TEST_CASE("capture JSON escapes glyphs outside printable ASCII", "[hwdecode]") {
    m8::dev::UiCapture cap;
    cap.screen = "INST.00";
    cap.palette = {{{0, 0, 0}}, {{0, 240, 248}}};
    cap.cells.push_back({0, 0, static_cast<char>(0x01), 1, 0});   // control byte
    cap.cells.push_back({1, 0, '"', 1, 0});                        // quote glyph
    cap.cells.push_back({2, 0, '\\', 1, 0});                       // backslash glyph
    cap.cells.push_back({3, 0, 'A', 1, 0});                        // ordinary

    const std::string text = m8::dev::toJson(cap);

    // No raw control byte may reach the file -- that is what breaks strict
    // parsers. Newline is the one legal control character here: toJson writes
    // one per line by design. Accumulate, assert once.
    bool rawControl = false;
    for (unsigned char c : text)
        if (c < 0x20 && c != '\n') rawControl = true;
    CHECK_FALSE(rawControl);

    m8::dev::UiCapture back;
    std::string err;
    REQUIRE(m8::dev::fromJson(text, back, err));
    REQUIRE(back.cells.size() == 4);
    CHECK(back.cells[0].ch == static_cast<char>(0x01));
    CHECK(back.cells[1].ch == '"');
    CHECK(back.cells[2].ch == '\\');
    CHECK(back.cells[3].ch == 'A');
}

// --- Theme robustness (hw_findings UI-14) ------------------------------------

// The grid cursor is drawn as inverse video -- accent as BACKGROUND behind a
// dark glyph -- and isCursor() tested the foreground alone, so it was
// structurally blind to it. m8drv inspect documented this in a docstring for
// weeks without anything acting on it.
TEST_CASE("cursor detection sees an inverse-video cursor", "[hwdecode]") {
    ScreenGrid grid;
    // Accent as foreground: the form-screen cursor.
    grid.handleFrame(makeCharFrame('A', 0, 30, 0, 240, 248, 0, 0, 0));
    // Accent as background behind a dark glyph: the grid cursor.
    grid.handleFrame(makeCharFrame('B', 8, 30, 0, 0, 0, 0, 240, 248));
    // Neither: ordinary text.
    grid.handleFrame(makeCharFrame('C', 16, 30, 144, 172, 184, 0, 0, 0));

    CHECK(grid.isCursorFg(grid.cells[{30, 0}]));
    CHECK(grid.isCursor(grid.cells[{30, 0}]));
    CHECK(grid.isCursorBg(grid.cells[{30, 8}]));
    CHECK(grid.isCursor(grid.cells[{30, 8}]));
    CHECK_FALSE(grid.isCursor(grid.cells[{30, 16}]));
}

// The blindness check. This is the state that cost a whole session: a wrong
// accent makes isCursor() false everywhere, so every cursor query returns -1
// and the driver reports a device that is ignoring keys. A screen that decoded
// cells but carries no accent anywhere is the signature of that, and it has to
// be distinguishable from an empty screen.
TEST_CASE("accentPresent reports whether the accent is findable at all", "[hwdecode]") {
    ScreenGrid grid;
    CHECK_FALSE(grid.accentPresent());          // no cells at all

    grid.handleFrame(makeCharFrame('X', 0, 30, 144, 172, 184, 0, 0, 0));
    CHECK_FALSE(grid.accentPresent());          // cells, but none accented

    grid.handleFrame(makeCharFrame('Y', 8, 30, 0, 240, 248, 0, 0, 0));
    CHECK(grid.accentPresent());

    // A device themed away from the pinned accent reads as blind, not as a
    // screen with no cursor -- which is exactly what the warning must say.
    ScreenGrid themed;
    themed.handleFrame(makeCharFrame('Z', 0, 30, 248, 160, 0, 0, 0, 0));
    CHECK_FALSE(themed.accentPresent());
    themed.cursorColor[0] = 248; themed.cursorColor[1] = 160; themed.cursorColor[2] = 0;
    CHECK(themed.accentPresent());
}

TEST_CASE("ThemeTable round-trips a pinned accent", "[hwdecode]") {
    using namespace m8::dev;
    const std::string path = "hw_theme_test_tmp.json";

    ThemeTable out;
    out.pinned = true;
    out.accent[0] = 248; out.accent[1] = 160; out.accent[2] = 0;
    out.tolerance = 12;
    out.themeId = "custom-orange";
    out.pinnedFwMajor = 6; out.pinnedFwMinor = 5; out.pinnedFwPatch = 2;
    out.method = "pressed DOWN";
    REQUIRE(out.saveToFile(path));

    ThemeTable in;
    REQUIRE(in.loadFromFile(path));
    CHECK(in.isPinned());
    CHECK(in.accent[0] == 248);
    CHECK(in.accent[1] == 160);
    CHECK(in.accent[2] == 0);
    CHECK(in.tolerance == 12);
    CHECK(in.themeId == "custom-orange");
    CHECK(in.pinnedFwMinor == 5);
    CHECK(in.source == AccentSource::FILE);

    const uint8_t near_[3] = {248, 168, 8};
    const uint8_t far_[3]  = {0, 240, 248};
    CHECK(in.matches(near_));
    CHECK_FALSE(in.matches(far_));

    std::remove(path.c_str());
}

// An unpinned file is a draft. Trusting one would put us back to assuming a
// colour, just with an extra step, so loadFromFile must refuse it.
TEST_CASE("ThemeTable refuses an unpinned theme file", "[hwdecode]") {
    using namespace m8::dev;
    const std::string path = "hw_theme_unpinned_tmp.json";
    {
        std::ofstream f(path);
        f << "{\"pinned\": false, \"accent\": [1, 2, 3]}";
    }
    ThemeTable in;
    CHECK_FALSE(in.loadFromFile(path));
    CHECK_FALSE(in.isPinned());
    CHECK(in.source == AccentSource::BUILTIN_DEFAULT);
    std::remove(path.c_str());
}

// The accent outranks the '<' glyph, and that ordering is the whole fix.
//
// cursorRowY used to accept either, ORed together, which was harmless only
// while the accent match was broken. Once the accent worked again, PHRASE's
// row 0 -- which carries "<0" as the PLAYHEAD marker, not a cursor -- won the
// OR and pinned the reported row there permanently. Observed on hardware:
// grid coordinates walked 15 -> 14 -> 13 while cursorRowY answered row 60
// every time. A workaround for a fixed bug, still confidently answering.
TEST_CASE("cursorRowY prefers the accent over a stray marker glyph", "[hwdecode]") {
    ScreenGrid grid;
    // PHRASE-shaped: the playhead marker sits on row 0 ...
    grid.handleFrame(makeCharFrame('<', 0, 60, 144, 172, 184, 0, 0, 0));
    grid.handleFrame(makeCharFrame('0', 8, 60, 144, 172, 184, 0, 0, 0));
    // ... while the real cursor, in the accent, is far down the grid.
    grid.handleFrame(makeCharFrame('9', 0, 150, 0, 240, 248, 0, 0, 0));

    CHECK(grid.cursorRowY() == 150);
}

// The marker must still work where it is genuinely all there is: a screen
// carrying no accent at all. Dropping the fallback outright would trade one
// silent wrong answer for another.
TEST_CASE("cursorRowY falls back to the marker when no accent is present", "[hwdecode]") {
    ScreenGrid grid;
    grid.handleFrame(makeCharFrame('<', 0, 60, 144, 172, 184, 0, 0, 0));
    grid.handleFrame(makeCharFrame('X', 8, 150, 144, 172, 184, 0, 0, 0));

    CHECK(grid.cursorRowY() == 60);
}

// The three instrument variants were unaddressable by name: readInstrumentType
// has returned "WAV"/"FM"/"HYPER" all along, but getFieldMap had no maps for
// them, so each fell through to the Sampler layout and pointed the cursor at
// rows holding something else entirely. Coordinates measured on fw 6.5.2.
TEST_CASE("instrument field maps exist for every synth type", "[hwdecode]") {
    using namespace m8::dev;

    auto fieldOf = [](const ScreenFieldMap& m, const char* want) -> const FieldInfo* {
        for (size_t i = 0; i < m.count; ++i)
            if (std::string(m.fields[i].name) == want) return &m.fields[i];
        return nullptr;
    };

    const auto wav = getFieldMap(Screen::INSTRUMENT, "WAV");
    const auto fm  = getFieldMap(Screen::INSTRUMENT, "FM");
    const auto hyp = getFieldMap(Screen::INSTRUMENT, "HYPER");
    const auto smp = getFieldMap(Screen::INSTRUMENT, "SAMPLER");

    // Each map must carry the field that only that instrument type has --
    // which is also what proves the dispatch is not quietly returning Sampler.
    REQUIRE(fieldOf(wav, "SCAN")  != nullptr);
    REQUIRE(fieldOf(fm,  "ALGO")  != nullptr);
    REQUIRE(fieldOf(hyp, "SWARM") != nullptr);
    CHECK(fieldOf(smp, "SCAN")  == nullptr);
    CHECK(fieldOf(smp, "ALGO")  == nullptr);
    CHECK(fieldOf(wav, "ALGO")  == nullptr);

    // Measured positions, so a silent regression to the Sampler map fails here
    // rather than on the device three steps later.
    CHECK(fieldOf(wav, "SCAN")->row == 11);
    CHECK(fieldOf(fm,  "ALGO")->row == 6);
    CHECK(fieldOf(hyp, "SWARM")->row == 11);

    // HyperSynth has no AMP on the device; every other type does. Pinning the
    // absence keeps a future "surely this was an oversight" edit honest.
    CHECK(fieldOf(hyp, "AMP") == nullptr);
    CHECK(fieldOf(wav, "AMP") != nullptr);
    CHECK(fieldOf(fm,  "AMP") != nullptr);
}

// All five instrument types share one right-hand column, and every map had it
// at 17 while the device draws it at 18 (measured on fw 6.5.2 across all five,
// M8_DRIVER_BUGS.md #29). The error was exactly 1, which sits inside
// moveCursorTo's >1-column tolerance from bug #10 -- so it never failed, it
// just took the slow path forever. That is precisely the kind of wrongness no
// hardware run reports, so it needs pinning here.
TEST_CASE("instrument maps agree on the right-hand column", "[hwdecode]") {
    using namespace m8::dev;
    const char* kTypes[] = {"SAMPLER", "MACROSYN", "WAV", "FM", "HYPER"};
    // Fields that live in the right column on every type that has them.
    const char* kRight[] = {"LIM", "PAN", "DRY", "CHO", "DEL", "REV"};

    bool wrongColumn = false;
    int checked = 0;
    for (const char* t : kTypes) {
        const auto m = getFieldMap(Screen::INSTRUMENT, t);
        for (size_t i = 0; i < m.count; ++i) {
            for (const char* want : kRight) {
                if (std::string(m.fields[i].name) != want) continue;
                ++checked;
                if (m.fields[i].col != 18) wrongColumn = true;
            }
        }
    }
    CHECK_FALSE(wrongColumn);
    CHECK(checked >= 25);   // 5 types x ~5-6 right-column fields

    // And the mixer-send row is labelled MFX on the device, whatever we call it.
    for (const char* t : kTypes) {
        const auto m = getFieldMap(Screen::INSTRUMENT, t);
        for (size_t i = 0; i < m.count; ++i) {
            if (std::string(m.fields[i].name) == "CHO")
                CHECK(std::string(m.fields[i].label) == "MFX");
        }
    }
}

// LOAD and SAVE are cursor stops on the instrument TYPE row. Until they were
// mapped, a cursor on either was named TYPE by identifyCursorField -- so
// moveCursorTo reported arriving at TYPE while parked on a button, and the
// edit that followed read the button. M8_DRIVER_BUGS.md #31.
TEST_CASE("instrument maps include the TYPE row buttons", "[hwdecode]") {
    using namespace m8::dev;
    const char* kTypes[] = {"SAMPLER", "MACROSYN", "WAV", "FM", "HYPER"};
    for (const char* t : kTypes) {
        const auto m = getFieldMap(Screen::INSTRUMENT, t);
        const FieldInfo* load = nullptr;
        const FieldInfo* save = nullptr;
        for (size_t i = 0; i < m.count; ++i) {
            if (std::string(m.fields[i].name) == "LOAD") load = &m.fields[i];
            if (std::string(m.fields[i].name) == "SAVE") save = &m.fields[i];
        }
        REQUIRE(load != nullptr);
        REQUIRE(save != nullptr);
        CHECK(load->col == 22);
        CHECK(save->col == 27);
        CHECK(load->row == 2);
        CHECK(save->row == 2);
    }
}

// M8_DRIVER_BUGS.md #32. A desynced stream shifts every field boundary, so x/y
// decode to nonsense -- and `cells[{y,x}] = c` used to accept any coordinate,
// which turned a corrupt stream into a plausible screen that everything above
// believed. The failures it produced downstream ("field not found",
// "could not find enum") both read as navigation bugs and were not.
TEST_CASE("frame decode rejects impossible coordinates", "[hwdecode]") {
    ScreenGrid grid;

    // A good cell lands and is not counted against anything.
    grid.handleFrame(makeCharFrame(0x41, 8, 30, 255, 255, 255, 0, 0, 0));
    CHECK(grid.cells.size() == 1);
    CHECK(grid.decodeLooksSane());

    // One cell past the edge: dropped, but NOT evidence of anything. The M8
    // really does draw a space at (0,240) on the INSTRUMENT screen, on every
    // read. This assertion previously demanded these be counted as corruption,
    // which is what made the first version of the check warn on every healthy
    // connection -- the assertion was changed deliberately, not relaxed to get
    // green.
    grid.handleFrame(makeCharFrame(0x42, 320, 30, 255, 255, 255, 0, 0, 0));
    grid.handleFrame(makeCharFrame(0x43, 8, 240, 255, 255, 255, 0, 0, 0));
    CHECK(grid.cells.size() == 1);          // neither stored
    CHECK(grid.offEdgeCells == 2);
    CHECK(grid.offPanelCells == 0);
    CHECK(grid.decodeLooksSane());          // benign

    // Wild values are a different matter: a coordinate no panel could address
    // is not a coordinate, it is a field boundary read at the wrong offset.
    grid.handleFrame(makeCharFrame(0x45, 4000, 30, 255, 255, 255, 0, 0, 0));
    CHECK(grid.offPanelCells == 1);
    CHECK_FALSE(grid.decodeLooksSane());

    // Off-pitch is counted but still stored -- a weaker signal than the bounds
    // check, and the cost of a false reject is a missing glyph.
    ScreenGrid g2;
    g2.handleFrame(makeCharFrame(0x44, 12, 30, 255, 255, 255, 0, 0, 0));
    CHECK(g2.cells.size() == 1);
    CHECK(g2.offPitchCells == 1);
    CHECK(g2.decodeLooksSane());            // suspicious, not fatal

    // clear() must reset the counters, or one bad read poisons every later one.
    grid.clear();
    CHECK(grid.offPanelCells == 0);
    CHECK(grid.offEdgeCells == 0);
    CHECK(grid.shortFrames == 0);
    CHECK(grid.decodeLooksSane());
}

// The check that actually catches M8_DRIVER_BUGS.md #32.
//
// A desynced stream decodes cells to WRONG BUT LEGAL coordinates, so bounds
// checks see nothing and the settle check passes (the damage is stable, and
// the header sits in the undamaged top rows). Measured on real captures: a
// healthy INSTRUMENT read scores 23/23 labels in place, two independently
// garbled reads of the same screen score 2/23. Nothing to tune between those.
TEST_CASE("layoutMatchRatio separates a decoded screen from a smeared one", "[hwdecode]") {
    using namespace m8::dev;

    auto drawLabel = [](ScreenGrid& g, const char* text, int mapCol, int mapRow, int shift) {
        for (int k = 0; text[k]; ++k) {
            const int x = (mapCol + 1 + k) * 8 + shift;
            const int y = (mapRow + 3) * 10;
            g.handleFrame(makeCharFrame(text[k], x, y, 144, 172, 184, 0, 0, 0));
        }
    };

    // A well-formed SAMPLER screen: labels exactly where the map puts them.
    ScreenGrid good;
    drawLabel(good, "TYPE",    0,  2, 0);
    drawLabel(good, "NAME",    0,  3, 0);
    drawLabel(good, "TRANSP.", 0,  4, 0);
    drawLabel(good, "SAMPLE",  0,  6, 0);
    drawLabel(good, "SLICE",   0,  8, 0);
    drawLabel(good, "PLAY",    0,  9, 0);
    drawLabel(good, "FILTER",  0, 15, 0);
    drawLabel(good, "CUTOFF",  0, 16, 0);
    drawLabel(good, "RES",     0, 17, 0);
    const double m1 = layoutMatchRatio(good, Screen::INSTRUMENT, "SAMPLER");
    REQUIRE(m1 > 0.0);

    // The same labels, every one shifted by a single character cell -- which is
    // what a one-byte field-boundary slip does to the whole screen.
    ScreenGrid smeared;
    drawLabel(smeared, "TYPE",    0,  2, 240);
    drawLabel(smeared, "NAME",    0,  3, 240);
    drawLabel(smeared, "TRANSP.", 0,  4, 240);
    drawLabel(smeared, "SAMPLE",  0,  6, 240);
    drawLabel(smeared, "SLICE",   0,  8, 240);
    drawLabel(smeared, "PLAY",    0,  9, 240);
    drawLabel(smeared, "FILTER",  0, 15, 240);
    drawLabel(smeared, "CUTOFF",  0, 16, 240);
    drawLabel(smeared, "RES",     0, 17, 240);
    const double m2 = layoutMatchRatio(smeared, Screen::INSTRUMENT, "SAMPLER");

    CHECK(m2 < m1);
    CHECK(m2 < 0.6);        // below the threshold m8_nav warns at
    CHECK(m1 > m2 + 0.3);   // and the gap is wide, not marginal

    // A one- or two-column map error must NOT read as corruption. Two healthy
    // screens (MIXER, EFFECTS) scored 0% before the window existed, because
    // their maps sit 1-2 columns off what the device draws -- the same slack
    // moveCursorTo has needed since bug #10.
    ScreenGrid nudged;
    drawLabel(nudged, "TYPE",    0,  2, 16);
    drawLabel(nudged, "NAME",    0,  3, 16);
    drawLabel(nudged, "TRANSP.", 0,  4, 16);
    drawLabel(nudged, "SAMPLE",  0,  6, 16);
    drawLabel(nudged, "SLICE",   0,  8, 16);
    drawLabel(nudged, "PLAY",    0,  9, 16);
    drawLabel(nudged, "FILTER",  0, 15, 16);
    drawLabel(nudged, "CUTOFF",  0, 16, 16);
    drawLabel(nudged, "RES",     0, 17, 16);
    const double m3 = layoutMatchRatio(nudged, Screen::INSTRUMENT, "SAMPLER");
    CHECK(m3 == m1);        // two columns over is still a match

    // Grid screens carry no field map and must report "cannot judge", not 0 --
    // scoring them zero would warn on every PHRASE read forever.
    CHECK(layoutMatchRatio(good, Screen::PHRASE) < 0.0);
}