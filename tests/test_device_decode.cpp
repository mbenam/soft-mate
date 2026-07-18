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
#include "m8/DeviceScriptRunner.h"

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
    // Default cursor color is (0, 252, 248).
    auto cursorFrame = makeCharFrame('X', 50, 30, 0, 252, 248, 0, 0, 0);
    auto normalFrame = makeCharFrame('Y', 60, 30, 255, 255, 255, 0, 0, 0);
    grid.handleFrame(cursorFrame);
    grid.handleFrame(normalFrame);

    CHECK(grid.isCursor(grid.cells[{30, 50}]));
    CHECK_FALSE(grid.isCursor(grid.cells[{30, 60}]));
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
