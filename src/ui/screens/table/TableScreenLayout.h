#pragma once
#include "../../ui_types.h"
#include <vector>

namespace m8 {
namespace ui {
namespace table {

inline void InitTableGrid(UI_GridCell TableGrid[16][5]) {
    for (int r = 0; r < 16; r++) {
        int row = r + 3;
        // Col 0: N, Col 1: V, Col 2: FX1, Col 3: FX2, Col 4: FX3
        TableGrid[r][0] = {"00", 2, row, "VALUE", "LABEL_LITE", "value", true, 0};
        TableGrid[r][1] = {"--", 5, row, "LABEL_DIM", "LABEL_LITE", "label", false, 0};
        TableGrid[r][2] = {"---00", 8, row, "LABEL_DIM", "LABEL_LITE", "label", false, 0};
        TableGrid[r][3] = {"---00", 14, row, "LABEL_DIM", "LABEL_LITE", "label", false, 0};
        TableGrid[r][4] = {"---00", 20, row, "LABEL_DIM", "LABEL_LITE", "label", false, 0};
    }
}

inline std::vector<UI_GridCell> GetStaticText() {
    return {
        {"TABLE", 0, 0, "TITLE", "", "static", false, 0},
    };
}

inline std::vector<UI_GridCell> GetDynamicTextDefaults() {
    return {
        {"13", 6, 0, "TITLE", "", "dynamic_text", false, 0},
        // Column Headers
        {"N", 2, 2, "LABEL_LITE", "", "dynamic_text", false, 0},
        {"FX1", 8, 2, "LABEL_DIM", "", "dynamic_text", false, 0},
        {"FX2", 14, 2, "LABEL_DIM", "", "dynamic_text", false, 0},
        {"FX3", 20, 2, "LABEL_DIM", "", "dynamic_text", false, 0},
        // Row Headers
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

} // namespace table
} // namespace ui
} // namespace m8
