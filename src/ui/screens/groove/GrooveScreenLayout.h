#pragma once
#include "../../ui_types.h"
#include <vector>

namespace m8 {
namespace ui {
namespace groove {

inline void InitGrooveGrid(UI_GridCell GrooveGrid[16][1]) {
    for (int r = 0; r < 16; r++) {
        int row = r + 3; // Grid starts at Y: 3
        GrooveGrid[r][0] = {"--", 4, row, "LABEL_DIM", "LABEL_LITE", "value", true, 0};
    }
}

inline std::vector<UI_GridCell> GetStaticText() {
    return {
        {"GROOVE", 0, 0, "TITLE", "", "static", false, 0},

        // Navigator Map
    };
}

inline std::vector<UI_GridCell> GetDynamicTextDefaults() {
    return {
        {"00", 7, 0, "TITLE", "", "dynamic_text", false, 0},
        {"T>128", 34, 2, "LABEL_DIM", "", "dynamic_text", false, 0},
        
        // Row Headers (0-F)
        {"0", 0, 3, "LABEL_LITE", "", "dynamic_text", false, 0},
        {"1", 0, 4, "LABEL_DIM", "", "dynamic_text", false, 0},
        {"2", 0, 5, "LABEL_DIM", "", "dynamic_text", false, 0},
        {"3", 0, 6, "LABEL_DIM", "", "dynamic_text", false, 0},
        {"4", 0, 7, "LABEL_DIM", "", "dynamic_text", false, 0},
        {"5", 0, 8, "LABEL_DIM", "", "dynamic_text", false, 0},
        {"6", 0, 9, "LABEL_DIM", "", "dynamic_text", false, 0},
        {"7", 0, 10, "LABEL_DIM", "", "dynamic_text", false, 0},
        {"8", 0, 11, "LABEL_DIM", "", "dynamic_text", false, 0},
        {"9", 0, 12, "LABEL_DIM", "", "dynamic_text", false, 0},
        {"A", 0, 13, "LABEL_DIM", "", "dynamic_text", false, 0},
        {"B", 0, 14, "LABEL_DIM", "", "dynamic_text", false, 0},
        {"D", 0, 16, "LABEL_DIM", "", "dynamic_text", false, 0},
        {"E", 0, 17, "LABEL_DIM", "", "dynamic_text", false, 0},
        {"F", 0, 18, "LABEL_DIM", "", "dynamic_text", false, 0}
    };
}

} // namespace groove
} // namespace ui
} // namespace m8
