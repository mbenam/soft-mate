#pragma once
#include "../../ui_types.h"
#include <vector>

namespace m8 {
namespace ui {
namespace chain {

// The `.text`/`.normal_color` literals below are layout-design placeholders.
// ChainScreen.cpp's interactive-grid loop only reads `.col`/`.row`/
// `.selected_color` from this table; the displayed value always comes from
// the live `Sequencer`/cursor state. Do not "fix" these strings expecting
// them to affect what's on screen.
inline void InitChainGrid(UI_GridCell ChainGrid[16][2]) {
    ChainGrid[0][0] = {"27", 3, 3, "VALUE", "LABEL_LITE", "value", true};
    ChainGrid[0][1] = {"00", 6, 3, "LABEL_DIM", "LABEL_LITE", "label", false};
    ChainGrid[1][0] = {"27", 3, 4, "VALUE", "LABEL_LITE", "value", true};
    ChainGrid[1][1] = {"00", 6, 4, "LABEL_DIM", "LABEL_LITE", "label", false};
    ChainGrid[2][0] = {"27", 3, 5, "VALUE", "LABEL_LITE", "value", true};
    ChainGrid[2][1] = {"00", 6, 5, "LABEL_DIM", "LABEL_LITE", "label", false};
    ChainGrid[3][0] = {"27", 3, 6, "VALUE", "LABEL_LITE", "value", true};
    ChainGrid[3][1] = {"00", 6, 6, "LABEL_DIM", "LABEL_LITE", "label", false};
    ChainGrid[4][0] = {"--", 3, 7, "LABEL_DIM", "LABEL_LITE", "label", false};
    ChainGrid[4][1] = {"00", 6, 7, "LABEL_DIM", "LABEL_LITE", "label", false};
    ChainGrid[5][0] = {"27", 3, 8, "VALUE", "LABEL_LITE", "value", true};
    ChainGrid[5][1] = {"00", 6, 8, "LABEL_DIM", "LABEL_LITE", "label", false};
    ChainGrid[6][0] = {"27", 3, 9, "VALUE", "LABEL_LITE", "value", true};
    ChainGrid[6][1] = {"00", 6, 9, "LABEL_DIM", "LABEL_LITE", "label", false};
    ChainGrid[7][0] = {"27", 3, 10, "VALUE", "LABEL_LITE", "value", true};
    ChainGrid[7][1] = {"00", 6, 10, "LABEL_DIM", "LABEL_LITE", "label", false};
    ChainGrid[8][0] = {"27", 3, 11, "VALUE", "LABEL_LITE", "value", true};
    ChainGrid[8][1] = {"00", 6, 11, "LABEL_DIM", "LABEL_LITE", "label", false};
    ChainGrid[9][0] = {"27", 3, 12, "VALUE", "LABEL_LITE", "value", true};
    ChainGrid[9][1] = {"00", 6, 12, "LABEL_DIM", "LABEL_LITE", "label", false};
    ChainGrid[10][0] = {"27", 3, 13, "VALUE", "LABEL_LITE", "value", true};
    ChainGrid[10][1] = {"00", 6, 13, "LABEL_DIM", "LABEL_LITE", "label", false};
    ChainGrid[11][0] = {"27", 3, 14, "VALUE", "LABEL_LITE", "value", true};
    ChainGrid[11][1] = {"00", 6, 14, "LABEL_DIM", "LABEL_LITE", "label", false};
    ChainGrid[12][0] = {"27", 3, 15, "VALUE", "LABEL_LITE", "value", true};
    ChainGrid[12][1] = {"00", 6, 15, "LABEL_DIM", "LABEL_LITE", "label", false};
    ChainGrid[13][0] = {"27", 3, 16, "VALUE", "LABEL_LITE", "value", true};
    ChainGrid[13][1] = {"00", 6, 16, "LABEL_DIM", "LABEL_LITE", "label", false};
    ChainGrid[14][0] = {"27", 3, 17, "VALUE", "LABEL_LITE", "value", true};
    ChainGrid[14][1] = {"00", 6, 17, "LABEL_DIM", "LABEL_LITE", "label", false};
    ChainGrid[15][0] = {"27", 3, 18, "VALUE", "LABEL_LITE", "value", true};
    ChainGrid[15][1] = {"00", 6, 18, "LABEL_DIM", "LABEL_LITE", "label", false};
}

inline std::vector<UI_GridCell> GetStaticText() {
    return {
        {"CHAIN", 0, 0, "TITLE", "", "static", false},
    };
}

inline std::vector<UI_GridCell> GetDynamicTextDefaults() {
    return {
        {"0A*", 6, 0, "TITLE", "", "dynamic_text", false},
        {"PH", 3, 2, "LABEL_LITE", "", "dynamic_text", false},
        {"TSP", 6, 2, "LABEL_DIM", "", "dynamic_text", false},
        {"0", 0, 3, "LABEL_LITE", "", "dynamic_text", false},
        {"1", 0, 4, "LABEL_DIM", "", "dynamic_text", false},
        {"2", 0, 5, "LABEL_DIM", "", "dynamic_text", false},
        {"3", 0, 6, "LABEL_DIM", "", "dynamic_text", false},
        {"4", 0, 7, "LABEL_DIM", "", "dynamic_text", false},
        {"5", 0, 8, "LABEL_DIM", "", "dynamic_text", false},
        {"6", 0, 9, "LABEL_DIM", "", "dynamic_text", false},
        {"7", 0, 10, "LABEL_DIM", "", "dynamic_text", false},
        {"8", 0, 11, "LABEL_DIM", "", "dynamic_text", false},
        {"9", 0, 12, "LABEL_DIM", "", "dynamic_text", false},
        {"A", 0, 13, "LABEL_DIM", "", "dynamic_text", false},
        {"B", 0, 14, "LABEL_DIM", "", "dynamic_text", false},
        {"D", 0, 16, "LABEL_DIM", "", "dynamic_text", false},
        {"E", 0, 17, "LABEL_DIM", "", "dynamic_text", false},
        {"F", 0, 18, "LABEL_DIM", "", "dynamic_text", false},
    };
}

} // namespace chain
} // namespace ui
} // namespace m8
