#pragma once
#include "../../ui_types.h"
#include <vector>
#include <string>
#include <cstdint>

namespace m8 {
namespace ui {
namespace sample_editor {

enum class CursorRow : int {
    RECORD = 0,
    SELECT,
    LOOP_REGION,
    SLICE_MARKER,
    PROCESS,
    NAME,
    SAVE,
    COUNT
};

inline const char* const kRecordSources[16] = {
    "L&R", "MIC", "USB", "INL", "INR", "U.L", "U.R", "ALL",
    "T1",  "T2",  "T3",  "T4",  "T5",  "T6",  "T7",  "T8"
};

inline const char* const kProcessNames[18] = {
    "CROP", "DELETE", "DUPLICATE", "NORMALIZE", "SILENCE",
    "REVERSE", "INVERT", "FADE IN", "FADE OUT", "XFADE LOOP",
    "SQUISH(OTT)", "MONO:MIX", "MONO:LEFT", "MONO:RIGHT",
    "DOWNSAMPLE", "8-BIT", "SLICE:AUTO", "SLICE:SILEN"
};

inline std::string GetProcessString(int index) {
    if (index >= 0 && index < 18) {
        return kProcessNames[index];
    }
    if (index >= 18 && index <= 144) {
        int sliceCount = index - 18 + 2; // 2..128
        char buf[16];
        std::snprintf(buf, sizeof(buf), "SLICE:%03d", sliceCount);
        return buf;
    }
    return "UNKNOWN";
}

inline constexpr int kWaveTopRow = 3;
inline constexpr int kWaveBottomRow = 10;
inline constexpr int kWaveCanvasHeight = 8;
inline constexpr int kGridCols = 40;

} // namespace sample_editor
} // namespace ui
} // namespace m8
