#pragma once
#include "../../ui_types.h"
#include <vector>

namespace m8 {
namespace ui {
namespace phrase {

inline void InitPhraseGrid(UI_GridCell PhraseGrid[16][9]) {
    PhraseGrid[0][0] = {"F#5", 2, 3, "LABEL_LITE", "", "", false};
    PhraseGrid[0][1] = {"64", 6, 3, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[0][2] = {"13", 9, 3, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[0][3] = {"CUT", 12, 3, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[0][4] = {"90", 15, 3, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[0][5] = {"EA1", 18, 3, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[0][6] = {"80", 21, 3, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[0][7] = {"---", 24, 3, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[0][8] = {"00", 27, 3, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[1][0] = {"---", 2, 4, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[1][1] = {"--", 6, 4, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[1][2] = {"--", 9, 4, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[1][3] = {"REP", 12, 4, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[1][4] = {"08", 15, 4, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[1][5] = {"REP", 18, 4, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[1][6] = {"08", 21, 4, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[1][7] = {"---", 24, 4, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[1][8] = {"00", 27, 4, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[2][0] = {"F#5", 2, 5, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[2][1] = {"44", 6, 5, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[2][2] = {"13", 9, 5, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[2][3] = {"---", 12, 5, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[2][4] = {"00", 15, 5, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[2][5] = {"---", 18, 5, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[2][6] = {"00", 21, 5, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[2][7] = {"---", 24, 5, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[2][8] = {"00", 27, 5, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[3][0] = {"F#5", 2, 6, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[3][1] = {"34", 6, 6, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[3][2] = {"13", 9, 6, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[3][3] = {"---", 12, 6, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[3][4] = {"00", 15, 6, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[3][5] = {"---", 18, 6, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[3][6] = {"00", 21, 6, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[3][7] = {"---", 24, 6, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[3][8] = {"00", 27, 6, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[4][0] = {"F#5", 2, 7, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[4][1] = {"64", 6, 7, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[4][2] = {"13", 9, 7, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[4][3] = {"---", 12, 7, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[4][4] = {"00", 15, 7, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[4][5] = {"---", 18, 7, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[4][6] = {"00", 21, 7, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[4][7] = {"---", 24, 7, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[4][8] = {"00", 27, 7, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[5][0] = {"---", 2, 8, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[5][1] = {"--", 6, 8, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[5][2] = {"--", 9, 8, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[5][3] = {"---", 12, 8, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[5][4] = {"00", 15, 8, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[5][5] = {"---", 18, 8, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[5][6] = {"00", 21, 8, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[5][7] = {"---", 24, 8, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[5][8] = {"00", 27, 8, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[6][0] = {"F#5", 2, 9, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[6][1] = {"34", 6, 9, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[6][2] = {"13", 9, 9, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[6][3] = {"---", 12, 9, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[6][4] = {"00", 15, 9, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[6][5] = {"---", 18, 9, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[6][6] = {"00", 21, 9, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[6][7] = {"---", 24, 9, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[6][8] = {"00", 27, 9, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[7][0] = {"---", 2, 10, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[7][1] = {"--", 6, 10, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[7][2] = {"--", 9, 10, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[7][3] = {"---", 12, 10, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[7][4] = {"00", 15, 10, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[7][5] = {"---", 18, 10, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[7][6] = {"00", 21, 10, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[7][7] = {"---", 24, 10, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[7][8] = {"00", 27, 10, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[8][0] = {"F#5", 2, 11, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[8][1] = {"64", 6, 11, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[8][2] = {"13", 9, 11, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[8][3] = {"---", 12, 11, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[8][4] = {"00", 15, 11, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[8][5] = {"---", 18, 11, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[8][6] = {"00", 21, 11, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[8][7] = {"---", 24, 11, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[8][8] = {"00", 27, 11, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[9][0] = {"---", 2, 12, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[9][1] = {"--", 6, 12, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[9][2] = {"--", 9, 12, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[9][3] = {"---", 12, 12, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[9][4] = {"00", 15, 12, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[9][5] = {"---", 18, 12, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[9][6] = {"00", 21, 12, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[9][7] = {"---", 24, 12, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[9][8] = {"00", 27, 12, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[10][0] = {"F#5", 2, 13, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[10][1] = {"34", 6, 13, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[10][2] = {"13", 9, 13, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[10][3] = {"---", 12, 13, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[10][4] = {"00", 15, 13, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[10][5] = {"---", 18, 13, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[10][6] = {"00", 21, 13, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[10][7] = {"---", 24, 13, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[10][8] = {"00", 27, 13, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[11][0] = {"F#5", 2, 14, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[11][1] = {"44", 6, 14, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[11][2] = {"13", 9, 14, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[11][3] = {"---", 12, 14, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[11][4] = {"00", 15, 14, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[11][5] = {"---", 18, 14, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[11][6] = {"00", 21, 14, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[11][7] = {"---", 24, 14, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[11][8] = {"00", 27, 14, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[12][0] = {"F#5", 2, 15, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[12][1] = {"64", 6, 15, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[12][2] = {"13", 9, 15, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[12][3] = {"---", 12, 15, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[12][4] = {"00", 15, 15, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[12][5] = {"---", 18, 15, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[12][6] = {"00", 21, 15, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[12][7] = {"---", 24, 15, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[12][8] = {"00", 27, 15, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[13][0] = {"---", 2, 16, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[13][1] = {"--", 6, 16, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[13][2] = {"--", 9, 16, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[13][3] = {"---", 12, 16, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[13][4] = {"00", 15, 16, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[13][5] = {"---", 18, 16, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[13][6] = {"00", 21, 16, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[13][7] = {"---", 24, 16, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[13][8] = {"00", 27, 16, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[14][0] = {"F#5", 2, 17, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[14][1] = {"44", 6, 17, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[14][2] = {"13", 9, 17, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[14][3] = {"---", 12, 17, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[14][4] = {"00", 15, 17, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[14][5] = {"---", 18, 17, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[14][6] = {"00", 21, 17, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[14][7] = {"---", 24, 17, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[14][8] = {"00", 27, 17, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[15][0] = {"F#5", 2, 18, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[15][1] = {"54", 6, 18, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[15][2] = {"13", 9, 18, "VALUE", "LABEL_LITE", "value", true};
    PhraseGrid[15][3] = {"---", 12, 18, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[15][4] = {"00", 15, 18, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[15][5] = {"---", 18, 18, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[15][6] = {"00", 21, 18, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[15][7] = {"---", 24, 18, "LABEL_DIM", "LABEL_LITE", "label", false};
    PhraseGrid[15][8] = {"00", 27, 18, "LABEL_DIM", "LABEL_LITE", "label", false};
}

inline std::vector<UI_GridCell> GetStaticText() {
    return {
    };
}

inline std::vector<UI_GridCell> GetDynamicTextDefaults() {
    return {
        {"PHRASE", 0, 0, "TITLE", "", "dynamic_text", false},
        {"12", 7, 0, "TITLE", "", "dynamic_text", false},
        {"N", 2, 2, "LABEL_LITE", "", "dynamic_text", false},
        {"FX1", 12, 2, "LABEL_DIM", "", "dynamic_text", false},
        {"FX2", 18, 2, "LABEL_DIM", "", "dynamic_text", false},
        {"FX3", 24, 2, "LABEL_DIM", "", "dynamic_text", false},
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

} // namespace phrase
} // namespace ui
} // namespace m8
