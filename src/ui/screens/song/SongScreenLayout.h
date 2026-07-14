#pragma once
#include "../../ui_types.h"
#include <vector>

namespace m8 {
namespace ui {
namespace song {

inline void InitSongGrid(UI_GridCell SongGrid[16][8]) {
    SongGrid[0][0] = {"0A", 3, 3, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[0][1] = {"0E", 6, 3, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[0][2] = {"0B", 9, 3, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[0][3] = {"03", 12, 3, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[0][4] = {"0A", 15, 3, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[0][5] = {"0A", 18, 3, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[0][6] = {"0A", 21, 3, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[0][7] = {"0A", 24, 3, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[1][0] = {"0C", 3, 4, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[1][1] = {"15", 6, 4, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[1][2] = {"23", 9, 4, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[1][3] = {"16", 12, 4, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[1][4] = {"17", 15, 4, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[1][5] = {"18", 18, 4, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[1][6] = {"19", 21, 4, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[1][7] = {"10", 24, 4, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[2][0] = {"0C", 3, 5, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[2][1] = {"1B", 6, 5, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[2][2] = {"13", 9, 5, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[2][3] = {"0D", 12, 5, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[2][4] = {"04", 15, 5, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[2][5] = {"05", 18, 5, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[2][6] = {"1C", 21, 5, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[2][7] = {"22", 24, 5, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[3][0] = {"0C", 3, 6, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[3][1] = {"01", 6, 6, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[3][2] = {"07", 9, 6, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[3][3] = {"0D", 12, 6, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[3][4] = {"04", 15, 6, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[3][5] = {"05", 18, 6, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[3][6] = {"10", 21, 6, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[3][7] = {"FE", 24, 6, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[4][0] = {"1F", 3, 7, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[4][1] = {"20", 6, 7, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[4][2] = {"20", 9, 7, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[4][3] = {"1E", 12, 7, "VALUE", "LABEL_LITE", "value", true};
    SongGrid[4][4] = {"20", 15, 7, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[4][5] = {"20", 18, 7, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[4][6] = {"20", 21, 7, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[4][7] = {"20", 24, 7, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[5][0] = {"--", 3, 8, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[5][1] = {"--", 6, 8, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[5][2] = {"--", 9, 8, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[5][3] = {"--", 12, 8, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[5][4] = {"--", 15, 8, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[5][5] = {"--", 18, 8, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[5][6] = {"--", 21, 8, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[5][7] = {"--", 24, 8, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[6][0] = {"--", 3, 9, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[6][1] = {"--", 6, 9, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[6][2] = {"--", 9, 9, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[6][3] = {"--", 12, 9, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[6][4] = {"--", 15, 9, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[6][5] = {"--", 18, 9, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[6][6] = {"--", 21, 9, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[6][7] = {"--", 24, 9, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[7][0] = {"--", 3, 10, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[7][1] = {"--", 6, 10, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[7][2] = {"--", 9, 10, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[7][3] = {"--", 12, 10, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[7][4] = {"--", 15, 10, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[7][5] = {"--", 18, 10, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[7][6] = {"--", 21, 10, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[7][7] = {"--", 24, 10, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[8][0] = {"--", 3, 11, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[8][1] = {"--", 6, 11, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[8][2] = {"--", 9, 11, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[8][3] = {"--", 12, 11, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[8][4] = {"--", 15, 11, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[8][5] = {"--", 18, 11, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[8][6] = {"--", 21, 11, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[8][7] = {"--", 24, 11, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[9][0] = {"--", 3, 12, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[9][1] = {"--", 6, 12, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[9][2] = {"--", 9, 12, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[9][3] = {"--", 12, 12, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[9][4] = {"--", 15, 12, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[9][5] = {"--", 18, 12, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[9][6] = {"--", 21, 12, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[9][7] = {"--", 24, 12, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[10][0] = {"--", 3, 13, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[10][1] = {"--", 6, 13, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[10][2] = {"--", 9, 13, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[10][3] = {"--", 12, 13, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[10][4] = {"--", 15, 13, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[10][5] = {"--", 18, 13, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[10][6] = {"--", 21, 13, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[10][7] = {"--", 24, 13, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[11][0] = {"--", 3, 14, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[11][1] = {"--", 6, 14, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[11][2] = {"--", 9, 14, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[11][3] = {"--", 12, 14, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[11][4] = {"--", 15, 14, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[11][5] = {"--", 18, 14, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[11][6] = {"--", 21, 14, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[11][7] = {"--", 24, 14, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[12][0] = {"--", 3, 15, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[12][1] = {"--", 6, 15, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[12][2] = {"--", 9, 15, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[12][3] = {"--", 12, 15, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[12][4] = {"--", 15, 15, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[12][5] = {"--", 18, 15, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[12][6] = {"--", 21, 15, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[12][7] = {"--", 24, 15, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[13][0] = {"--", 3, 16, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[13][1] = {"--", 6, 16, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[13][2] = {"--", 9, 16, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[13][3] = {"--", 12, 16, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[13][4] = {"--", 15, 16, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[13][5] = {"--", 18, 16, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[13][6] = {"--", 21, 16, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[13][7] = {"--", 24, 16, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[14][0] = {"--", 3, 17, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[14][1] = {"--", 6, 17, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[14][2] = {"--", 9, 17, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[14][3] = {"--", 12, 17, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[14][4] = {"--", 15, 17, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[14][5] = {"--", 18, 17, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[14][6] = {"--", 21, 17, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[14][7] = {"--", 24, 17, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[15][0] = {"--", 3, 18, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[15][1] = {"--", 6, 18, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[15][2] = {"--", 9, 18, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[15][3] = {"--", 12, 18, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[15][4] = {"--", 15, 18, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[15][5] = {"--", 18, 18, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[15][6] = {"--", 21, 18, "LABEL_DIM", "LABEL_LITE", "label", false};
    SongGrid[15][7] = {"--", 24, 18, "LABEL_DIM", "LABEL_LITE", "label", false};
}

inline std::vector<UI_GridCell> GetStaticText() {
    return {
    };
}

inline std::vector<UI_GridCell> GetDynamicTextDefaults() {
    return {
        {"SONG", 0, 0, "TITLE", "", "dynamic_text", false},
        {"1", 4, 2, "LABEL_DIM", "", "dynamic_text", false},
        {"2", 7, 2, "LABEL_DIM", "", "dynamic_text", false},
        {"3", 10, 2, "LABEL_DIM", "", "dynamic_text", false},
        {"4", 13, 2, "LABEL_DIM", "", "dynamic_text", false},
        {"5", 16, 2, "LABEL_DIM", "", "dynamic_text", false},
        {"6", 19, 2, "LABEL_DIM", "", "dynamic_text", false},
        {"7", 22, 2, "LABEL_DIM", "", "dynamic_text", false},
        {"8", 25, 2, "LABEL_DIM", "", "dynamic_text", false},
        {"00", 0, 3, "LABEL_DIM", "", "dynamic_text", false},
        {"01", 0, 4, "LABEL_DIM", "", "dynamic_text", false},
        {"02", 0, 5, "LABEL_DIM", "", "dynamic_text", false},
        {"03", 0, 6, "LABEL_DIM", "", "dynamic_text", false},
        {"04", 0, 7, "LABEL_DIM", "", "dynamic_text", false},
        {"05", 0, 8, "LABEL_DIM", "", "dynamic_text", false},
        {"06", 0, 9, "LABEL_DIM", "", "dynamic_text", false},
        {"07", 0, 10, "LABEL_DIM", "", "dynamic_text", false},
        {"08", 0, 11, "LABEL_DIM", "", "dynamic_text", false},
        {"09", 0, 12, "LABEL_DIM", "", "dynamic_text", false},
        {"0A", 0, 13, "LABEL_DIM", "", "dynamic_text", false},
        {"0B", 0, 14, "LABEL_DIM", "", "dynamic_text", false},
        {"0C", 0, 15, "LABEL_DIM", "", "dynamic_text", false},
        {"0D", 0, 16, "LABEL_DIM", "", "dynamic_text", false},
        {"0E", 0, 17, "LABEL_DIM", "", "dynamic_text", false},
        {"0F", 0, 18, "LABEL_DIM", "", "dynamic_text", false},
    };
}

} // namespace song
} // namespace ui
} // namespace m8
