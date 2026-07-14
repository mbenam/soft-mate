#pragma once
#include "../../ui_types.h"
#include <vector>
#include <unordered_map>
#include <string>

namespace m8 {
namespace ui {
namespace effects {

inline std::vector<UI_GridCell> GetEffectsStaticText() {
    return {
        {"EFFECT SETTINGS", 0, 0, "TITLE", "", "static", false, 0},
        
        {"CHORUS", 0, 2, "TITLE", "", "static", false, 0},
        {":", 23, 3, "LABEL_DIM", "", "static", false, 0},
        
        {"DELAY", 0, 8, "TITLE", "", "static", false, 0},
        {":", 23, 9, "LABEL_DIM", "", "static", false, 0},
        
        {"REVERB", 0, 15, "TITLE", "", "static", false, 0},
        {":", 23, 18, "LABEL_DIM", "", "static", false, 0},
    };
}

inline std::unordered_map<std::string, std::vector<UI_GridCell>> GetEffectsInteractiveFields() {
    return {
        // CHORUS
        {"CHO_EQ", { {"INPUT EQ", 7, 2, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"EQ", 21, 2, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"CHO_MOD_DEP", { {"MOD DEPTH:FRQ", 7, 3, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"50", 21, 3, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"CHO_MOD_FRQ", { {"80", 24, 3, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"CHO_WID", { {"STEREO WIDTH", 7, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"FF", 21, 4, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"CHO_REV", { {"REVERB SEND", 7, 5, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 21, 5, "VALUE", "LABEL_LITE", "value", true, 0} }},

        // DELAY
        {"DEL_EQ", { {"INPUT EQ", 7, 8, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"EQ", 21, 8, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"DEL_TIME_L", { {"TIME L:R", 7, 9, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"30", 21, 9, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"DEL_TIME_R", { {"30", 24, 9, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"DEL_FBK", { {"FEEDBACK", 7, 10, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"80", 21, 10, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"DEL_WID", { {"STEREO WIDTH", 7, 11, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"FF", 21, 11, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"DEL_REV", { {"REVERB SEND", 7, 12, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 21, 12, "VALUE", "LABEL_LITE", "value", true, 0} }},

        // REVERB
        {"REV_EQ", { {"INPUT EQ", 7, 15, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"EQ", 21, 15, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"REV_SIZE", { {"ROOM SIZE", 7, 16, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"FF", 21, 16, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"REV_DEC", { {"DECAY", 7, 17, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"C0", 21, 17, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"REV_MOD_DEP", { {"MOD DEPTH:FRQ", 7, 18, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"20", 21, 18, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"REV_MOD_FRQ", { {"FF", 24, 18, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"REV_WID", { {"STEREO WIDTH", 7, 19, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"FF", 21, 19, "VALUE", "LABEL_LITE", "value", true, 0} }},
    };
}

inline std::unordered_map<std::string, NavNode> GetEffectsNavMap() {
    return {
        // CHORUS
        {"CHO_EQ",      {/*U*/"",             /*D*/"CHO_MOD_DEP", /*L*/"",             /*R*/""}},
        {"CHO_MOD_DEP", {/*U*/"CHO_EQ",       /*D*/"CHO_WID",     /*L*/"",             /*R*/"CHO_MOD_FRQ"}},
        {"CHO_MOD_FRQ", {/*U*/"CHO_EQ",       /*D*/"CHO_WID",     /*L*/"CHO_MOD_DEP",  /*R*/""}},
        {"CHO_WID",     {/*U*/"CHO_MOD_DEP",  /*D*/"CHO_REV",     /*L*/"",             /*R*/""}},
        {"CHO_REV",     {/*U*/"CHO_WID",      /*D*/"DEL_EQ",      /*L*/"",             /*R*/""}},

        // DELAY
        {"DEL_EQ",      {/*U*/"CHO_REV",      /*D*/"DEL_TIME_L",  /*L*/"",             /*R*/""}},
        {"DEL_TIME_L",  {/*U*/"DEL_EQ",       /*D*/"DEL_FBK",     /*L*/"",             /*R*/"DEL_TIME_R"}},
        {"DEL_TIME_R",  {/*U*/"DEL_EQ",       /*D*/"DEL_FBK",     /*L*/"DEL_TIME_L",   /*R*/""}},
        {"DEL_FBK",     {/*U*/"DEL_TIME_L",   /*D*/"DEL_WID",     /*L*/"",             /*R*/""}},
        {"DEL_WID",     {/*U*/"DEL_FBK",      /*D*/"DEL_REV",     /*L*/"",             /*R*/""}},
        {"DEL_REV",     {/*U*/"DEL_WID",      /*D*/"REV_EQ",      /*L*/"",             /*R*/""}},

        // REVERB
        {"REV_EQ",      {/*U*/"DEL_REV",      /*D*/"REV_SIZE",    /*L*/"",             /*R*/""}},
        {"REV_SIZE",    {/*U*/"REV_EQ",       /*D*/"REV_DEC",     /*L*/"",             /*R*/""}},
        {"REV_DEC",     {/*U*/"REV_SIZE",     /*D*/"REV_MOD_DEP", /*L*/"",             /*R*/""}},
        {"REV_MOD_DEP", {/*U*/"REV_DEC",      /*D*/"REV_WID",     /*L*/"",             /*R*/"REV_MOD_FRQ"}},
        {"REV_MOD_FRQ", {/*U*/"REV_DEC",      /*D*/"REV_WID",     /*L*/"REV_MOD_DEP",  /*R*/""}},
        {"REV_WID",     {/*U*/"REV_MOD_DEP",  /*D*/"",            /*L*/"",             /*R*/""}}
    };
}

} // namespace effects
} // namespace ui
} // namespace m8
