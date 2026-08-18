#pragma once
#include "../../ui_types.h"
#include <vector>

namespace m8 {
namespace ui {
namespace table {

inline void InitTableGrid(UI_GridCell TableGrid[16][8]) {
    for (int r = 0; r < 16; r++) {
        int row = r + 3;
        // Col 0: N, Col 1: V, Col 2: FX1 Cmd, Col 3: FX1 Val, Col 4: FX2 Cmd, Col 5: FX2 Val, Col 6: FX3 Cmd, Col 7: FX3 Val
        TableGrid[r][0] = {"00",  3, row, "VALUE",     "LABEL_LITE", "value", true,  2};
        TableGrid[r][1] = {"--",  6, row, "LABEL_DIM", "LABEL_LITE", "label", false, 2};
        TableGrid[r][2] = {"---", 9, row, "LABEL_DIM", "LABEL_LITE", "label", false, 3};
        TableGrid[r][3] = {"00", 13, row, "LABEL_DIM", "LABEL_LITE", "label", false, 2};
        TableGrid[r][4] = {"---", 16, row, "LABEL_DIM", "LABEL_LITE", "label", false, 3};
        TableGrid[r][5] = {"00", 20, row, "LABEL_DIM", "LABEL_LITE", "label", false, 2};
        TableGrid[r][6] = {"---", 23, row, "LABEL_DIM", "LABEL_LITE", "label", false, 3};
        TableGrid[r][7] = {"00", 27, row, "LABEL_DIM", "LABEL_LITE", "label", false, 2};
    }
}

inline std::vector<UI_GridCell> GetStaticText() {
    return {
        {"TABLE", 0, 0, "TITLE", "", "static", false, 5},
    };
}

inline std::vector<UI_GridCell> GetDynamicTextDefaults() {
    return {
        {"00", 6, 0, "TITLE", "", "dynamic_text", false, 2},
        // Column Headers
        {"N",    3, 2, "LABEL_LITE", "", "dynamic_text", false, 1},
        {"V",    6, 2, "LABEL_DIM",  "", "dynamic_text", false, 1},
        {"FX1",  9, 2, "LABEL_DIM",  "", "dynamic_text", false, 3},
        {"FX2", 16, 2, "LABEL_DIM",  "", "dynamic_text", false, 3},
        {"FX3", 23, 2, "LABEL_DIM",  "", "dynamic_text", false, 3},
        // Row Headers
        {"0", 1, 3,  "LABEL_LITE", "", "dynamic_text", false, 1},
        {"1", 1, 4,  "LABEL_DIM",  "", "dynamic_text", false, 1},
        {"2", 1, 5,  "LABEL_DIM",  "", "dynamic_text", false, 1},
        {"3", 1, 6,  "LABEL_DIM",  "", "dynamic_text", false, 1},
        {"4", 1, 7,  "LABEL_DIM",  "", "dynamic_text", false, 1},
        {"5", 1, 8,  "LABEL_DIM",  "", "dynamic_text", false, 1},
        {"6", 1, 9,  "LABEL_DIM",  "", "dynamic_text", false, 1},
        {"7", 1, 10, "LABEL_DIM",  "", "dynamic_text", false, 1},
        {"8", 1, 11, "LABEL_DIM",  "", "dynamic_text", false, 1},
        {"9", 1, 12, "LABEL_DIM",  "", "dynamic_text", false, 1},
        {"A", 1, 13, "LABEL_DIM",  "", "dynamic_text", false, 1},
        {"B", 1, 14, "LABEL_DIM",  "", "dynamic_text", false, 1},
        {"C", 1, 15, "LABEL_DIM",  "", "dynamic_text", false, 1},
        {"D", 1, 16, "LABEL_DIM",  "", "dynamic_text", false, 1},
        {"E", 1, 17, "LABEL_DIM",  "", "dynamic_text", false, 1},
        {"F", 1, 18, "LABEL_DIM",  "", "dynamic_text", false, 1}
    };
}

} // namespace table
} // namespace ui
} // namespace m8
