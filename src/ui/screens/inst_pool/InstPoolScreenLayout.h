#pragma once
#include "../../ui_types.h"
#include <vector>

namespace m8 {
namespace ui {
namespace inst_pool {

inline void InitInstPoolGrid(UI_GridCell Grid[16][6]) {
    for (int r = 0; r < 16; r++) {
        int row = r + 4; // Data rows start at Y: 4
        
        // Col 0: INST NAME (Width 12)
        Grid[r][0] = {"------------", 3, row, "VALUE", "LABEL_LITE", "value", true, 0};
        // Col 1: DRY (Width 2)
        Grid[r][1] = {"--", 16, row, "LABEL_DIM", "LABEL_LITE", "value", true, 0};
        // Col 2: CH (Width 2)
        Grid[r][2] = {"--", 19, row, "LABEL_DIM", "LABEL_LITE", "value", true, 0};
        // Col 3: DE (Width 2)
        Grid[r][3] = {"--", 22, row, "LABEL_DIM", "LABEL_LITE", "value", true, 0};
        // Col 4: RV (Width 2)
        Grid[r][4] = {"--", 25, row, "LABEL_DIM", "LABEL_LITE", "value", true, 0};
        // Col 5: EQ (Width 2)
        Grid[r][5] = {"--", 28, row, "LABEL_DIM", "LABEL_LITE", "value", true, 0};
    }
}

inline std::vector<UI_GridCell> GetStaticText() {
    return {
        {"INSTRUMENT POOL", 0, 0, "TITLE", "", "static", false, 0},
        
        // Navigator Map
    };
}

inline std::vector<UI_GridCell> GetDynamicTextDefaults() {
    return {
        // Column Headers
        {"INST.", 3, 2, "LABEL_LITE", "", "dynamic_text", false, 0},
        {"DRY", 15, 2, "LABEL_DIM", "", "dynamic_text", false, 0},
        {"CH", 19, 2, "LABEL_DIM", "", "dynamic_text", false, 0},
        {"DE", 22, 2, "LABEL_DIM", "", "dynamic_text", false, 0},
        {"RV", 25, 2, "LABEL_DIM", "", "dynamic_text", false, 0},
        {"EQ", 28, 2, "LABEL_DIM", "", "dynamic_text", false, 0},
        // Row Headers (Placeholder, populated dynamically in cpp)
        {"00", 0, 4, "LABEL_LITE", "", "dynamic_text", false, 0}
    };
}

} // namespace inst_pool
} // namespace ui
} // namespace m8
