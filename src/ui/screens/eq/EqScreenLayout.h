#pragma once
#include "../../ui_types.h"
#include <vector>
#include <cstdint>

// EQ editor layout (EQ_SPEC.md §6).
//
// The parameter table is a plain 5x3 grid -- five parameters down, three bands
// across -- so it uses row/col ints rather than a CursorId enum and NavNode
// map. There is nothing irregular to navigate around.
//
// The curve above it is not in these tables: it is computed and drawn per
// column from the live bank (EqScreen.cpp).

namespace m8 {
namespace ui {
namespace eq {

// Cursor rows, in screen order.
enum Param : int { P_GAIN = 0, P_FREQ, P_Q, P_TYPE, P_MODE, P_COUNT };

// ---- Geometry ---------------------------------------------------------------
inline constexpr int kAxisRow      = 2;    // frequency labels
inline constexpr int kCurveTop     = 3;    // first curve cell row
inline constexpr int kCurveRows    = 12;   // 12 cells x 7 pixel rows = 84 steps
inline constexpr int kCurveBottom  = kCurveTop + kCurveRows - 1;
inline constexpr int kTableTop     = 17;   // band headers; params start below
inline constexpr int kLabelCol     = 0;
inline constexpr int kBandCol[3]   = { 9, 19, 29 };

// The curve spans 20 Hz .. 20 kHz across the full 40-column grid, three decades
// mapped logarithmically. Vertically it covers +/-18 dB.
inline constexpr float kFreqMin    = 20.0f;
inline constexpr float kDecades    = 3.0f;      // 20 Hz -> 20 kHz
inline constexpr int   kGridCols   = 40;
inline constexpr float kDbRange    = 18.0f;     // +/- this, top to bottom

inline float ColumnToFreq(int col) {
    return kFreqMin * std::pow(10.0f, kDecades * (float(col) + 0.5f) / float(kGridCols));
}

inline std::vector<UI_GridCell> GetEqStaticText() {
    return {
        // Frequency axis. Positions are where each label's frequency actually
        // falls on the log scale, rounded to a cell.
        {"50",   5, kAxisRow, "LABEL_DIM", "", "static", false, 0},
        {"100",  9, kAxisRow, "LABEL_DIM", "", "static", false, 0},
        {"200", 13, kAxisRow, "LABEL_DIM", "", "static", false, 0},
        {"500", 18, kAxisRow, "LABEL_DIM", "", "static", false, 0},
        {"1K",  23, kAxisRow, "LABEL_DIM", "", "static", false, 0},
        {"2K",  27, kAxisRow, "LABEL_DIM", "", "static", false, 0},
        {"5K",  32, kAxisRow, "LABEL_DIM", "", "static", false, 0},
        {"10K", 36, kAxisRow, "LABEL_DIM", "", "static", false, 0},

        {"LOW",  kBandCol[0], kTableTop, "LABEL_DIM", "", "static", false, 0},
        {"MID",  kBandCol[1], kTableTop, "LABEL_DIM", "", "static", false, 0},
        {"HIGH", kBandCol[2], kTableTop, "LABEL_DIM", "", "static", false, 0},

        {"GAIN", kLabelCol, kTableTop + 1, "LABEL_DIM", "", "static", false, 0},
        {"FREQ", kLabelCol, kTableTop + 2, "LABEL_DIM", "", "static", false, 0},
        {"Q",    kLabelCol, kTableTop + 3, "LABEL_DIM", "", "static", false, 0},
        {"TYPE", kLabelCol, kTableTop + 4, "LABEL_DIM", "", "static", false, 0},
        {"MODE", kLabelCol, kTableTop + 5, "LABEL_DIM", "", "static", false, 0},
    };
}

inline const char* TypeName(int t) {
    switch (t) {
    case 0: return "LOWCUT";
    case 1: return "LOWSHELF";
    case 2: return "BELL";
    case 3: return "BANDPASS";
    case 4: return "HI.SHELF";
    case 5: return "HI.CUT";
    case 6: return "ALLPASS";
    default: return "?";
    }
}

inline const char* ModeName(int m) {
    switch (m) {
    case 0: return "STEREO";
    case 1: return "MID";
    case 2: return "SIDE";
    case 3: return "LEFT";
    case 4: return "RIGHT";
    default: return "?";
    }
}

} // namespace eq
} // namespace ui
} // namespace m8
