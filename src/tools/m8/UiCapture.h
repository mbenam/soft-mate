#pragma once

// ===========================================================================
// UiCapture.h — normalized screen capture for device-vs-clone UI parity.
//
// The device gives pixel coords + literal RGB. The clone stores character
// coords + named styles. This is the common form: character coords, a
// style id derived by clustering the colours observed WITHIN this capture,
// and rects in character units with sub-cell offsets.
//
// Colours are deliberately NOT stored as absolute RGB in the comparable
// body -- the M8 has a user-selectable theme, so absolute values are not
// stable across projects. The observed palette is recorded in the header
// for reference; comparison uses style ids.
// ===========================================================================

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "M8Device.h"
#include "ScreenModel.h"
#include "Result.h"

namespace m8 {
namespace dev {

struct UiCell {
    int col = 0, row = 0;
    char ch = ' ';
    int  fgStyle = -1;   // index into the capture's palette
    int  bgStyle = -1;
};

struct UiRect {
    // Character-grid position, with sub-cell offsets in pixels so a bar that
    // fills part of a cell round-trips.
    int col = 0, row = 0;
    int offsetX = 0, offsetY = 0;
    int wPx = 0, hPx = 0;
    int style = -1;
};

struct UiCapture {
    // Header — recorded, not compared (except where noted).
    std::string screen;         // COMPARED
    std::string firmware;
    int  fontMode = -1;         // COMPARED — refuse to diff across mismatches
    int  pitchX = 0, pitchY = 0;
    bool settled = false;       // must be true for a valid golden
    std::string themeId;        // COMPARED — see C6

    // Palette: distinct colours observed, in canonical (sorted) order.
    // Style ids index this. Recorded for reference; not compared directly.
    std::vector<std::array<uint8_t,3>> palette;

    // Body — compared.
    std::vector<UiCell> cells;
    std::vector<UiRect> rects;
};

// Build the palette by collecting distinct fg/bg triples across all cells and
// rects, sorted canonically (by r, then g, then b).
void buildPalette(UiCapture& c);

// Build a normalized UiCapture from an M8 ScreenGrid.
UiCapture captureFromGrid(const ScreenGrid& grid, bool settled = true,
                          const std::string& screenName = "",
                          const std::string& firmware = "6.5.2", int fontMode = 0,
                          const std::string& themeId = "m8-default-6.5.2");

// Serialize / parse. Stable field order so a plain text diff is readable.
std::string toJson(const UiCapture& c);
bool fromJson(const std::string& text, UiCapture& out, std::string& err);

} // namespace dev
} // namespace m8
