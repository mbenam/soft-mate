#pragma once
#include "../../ui_types.h"
#include <vector>

namespace m8 {
namespace ui {
namespace groove {

inline void InitGrooveGrid(UI_GridCell GrooveGrid[16][1]) {
    for (int r = 0; r < 16; r++) {
        int row = r + 3; // Grid starts at Y: 3
        GrooveGrid[r][0] = {"--", 3, row, "LABEL_DIM", "LABEL_LITE", "value", true, 0};
    }
}

inline std::vector<UI_GridCell> GetStaticText() {
    return {
        {"GROOVE", 0, 0, "TITLE", "", "static", false, 0},
        {"TIC",    3, 2, "LABEL_DIM", "", "static", false, 0},
        {"PPQ",    8, 2, "LABEL_DIM", "", "static", false, 0},
        {"SWING", 12, 2, "LABEL_DIM", "", "static", false, 0},
    };
}

inline std::vector<UI_GridCell> GetDynamicTextDefaults() {
    return {
        {"00", 7, 0, "TITLE", "", "dynamic_text", false, 0},

        // Row Headers (0-F) at col 1
        {"0", 1, 3,  "LABEL_LITE", "", "dynamic_text", false, 0},
        {"1", 1, 4,  "LABEL_DIM",  "", "dynamic_text", false, 0},
        {"2", 1, 5,  "LABEL_DIM",  "", "dynamic_text", false, 0},
        {"3", 1, 6,  "LABEL_DIM",  "", "dynamic_text", false, 0},
        {"4", 1, 7,  "LABEL_DIM",  "", "dynamic_text", false, 0},
        {"5", 1, 8,  "LABEL_DIM",  "", "dynamic_text", false, 0},
        {"6", 1, 9,  "LABEL_DIM",  "", "dynamic_text", false, 0},
        {"7", 1, 10, "LABEL_DIM",  "", "dynamic_text", false, 0},
        {"8", 1, 11, "LABEL_DIM",  "", "dynamic_text", false, 0},
        {"9", 1, 12, "LABEL_DIM",  "", "dynamic_text", false, 0},
        {"A", 1, 13, "LABEL_DIM",  "", "dynamic_text", false, 0},
        {"B", 1, 14, "LABEL_DIM",  "", "dynamic_text", false, 0},
        {"C", 1, 15, "LABEL_DIM",  "", "dynamic_text", false, 0},
        {"D", 1, 16, "LABEL_DIM",  "", "dynamic_text", false, 0},
        {"E", 1, 17, "LABEL_DIM",  "", "dynamic_text", false, 0},
        {"F", 1, 18, "LABEL_DIM",  "", "dynamic_text", false, 0}
    };
}

} // namespace groove
} // namespace ui
} // namespace m8
