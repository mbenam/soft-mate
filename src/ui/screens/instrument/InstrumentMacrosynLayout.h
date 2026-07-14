#pragma once
#include "../../ui_types.h"
#include <vector>
#include <unordered_map>
#include <string>

namespace m8 {
namespace ui {
namespace instrument {

inline std::vector<UI_GridCell> GetMacrosynStaticText() {
    return {
        {"INST.", 0, 0, "TITLE", "", "static", false, 0},
        // Navigator
        {"SCP", 34, 26, "LABEL_DIM", "", "static", false, 0},
    };
}

inline std::vector<UI_GridCell> GetMacrosynDynamicTextDefaults() {
    return {
        {"13", 6, 0, "TITLE", "", "dynamic_text", false, 0},
    };
}

inline std::unordered_map<std::string, std::vector<UI_GridCell>> GetMacrosynInteractiveFields() {
    return {
        {"TYPE", { {"TYPE", 0, 2, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"MACROSYN", 8, 2, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"CMD_LOAD", { {"LOAD", 22, 2, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"CMD_SAVE", { {"SAVE", 27, 2, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"NAME", { {"NAME", 0, 3, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"------------", 8, 3, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"TRANSP", { {"TRANSP.", 0, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"ON", 8, 4, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"TBL_TIC", { {"TBL.TIC", 13, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"FF", 21, 4, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"EQ", { {"EQ", 26, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"--", 29, 4, "VALUE", "LABEL_LITE", "value", true, 0} }},
        
        // Macrosyn Left Column
        {"SHAPE", { {"SHAPE", 0, 6, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 8, 6, "VALUE", "LABEL_LITE", "value", true, 0}, {"CSAW", 10, 6, "ACCENT", "LABEL_LITE", "accent", false, 0} }},
        {"TIMBRE", { {"TIMBRE", 0, 8, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"80", 8, 8, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 8, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {"COLOR", { {"COLOR", 0, 9, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"80", 8, 9, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 9, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {"DEGRADE", { {"DEGRADE", 0, 10, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 8, 10, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 10, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {"REDUX", { {"REDUX", 0, 11, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 8, 11, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 11, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {"FILTER", { {"FILTER", 0, 12, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 8, 12, "VALUE", "LABEL_LITE", "value", true, 0}, {"OFF", 10, 12, "ACCENT", "LABEL_LITE", "accent", false, 0} }},
        {"CUTOFF", { {"CUTOFF", 0, 13, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"FF", 8, 13, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 13, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {"RES", { {"RES", 0, 14, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 8, 14, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 14, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        
        // Right Column
        {"AMP", { {"AMP", 17, 8, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 21, 8, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 8, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {"LIM", { {"LIM", 17, 9, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 21, 9, "VALUE", "LABEL_LITE", "value", true, 0}, {"CLIP", 23, 9, "ACCENT", "LABEL_LITE", "accent", false, 0} }},
        {"PAN", { {"PAN", 17, 10, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"80", 21, 10, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 10, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {"DRY", { {"DRY", 17, 11, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"C0", 21, 11, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 11, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {"CHO", { {"CHO", 17, 12, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 21, 12, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 12, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {"DEL", { {"DEL", 17, 13, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 21, 13, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 13, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {"REV", { {"REV", 17, 14, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 21, 14, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 14, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }}
    };
}

inline std::unordered_map<std::string, NavNode> GetMacrosynNavMap() {
    return {
        {"TYPE",        {/*U*/"",             /*D*/"NAME",        /*L*/"",             /*R*/"CMD_LOAD"}},
        {"CMD_LOAD",    {/*U*/"",             /*D*/"NAME",        /*L*/"TYPE",         /*R*/"CMD_SAVE"}},
        {"CMD_SAVE",    {/*U*/"",             /*D*/"NAME",        /*L*/"CMD_LOAD",     /*R*/""}},
        {"NAME",        {/*U*/"TYPE",         /*D*/"TRANSP",      /*L*/"",             /*R*/""}},
        {"TRANSP",      {/*U*/"NAME",         /*D*/"SHAPE",       /*L*/"",             /*R*/"TBL_TIC"}},
        {"TBL_TIC",     {/*U*/"NAME",         /*D*/"SHAPE",       /*L*/"TRANSP",       /*R*/"EQ"}},
        {"EQ",          {/*U*/"NAME",         /*D*/"SHAPE",       /*L*/"TBL_TIC",      /*R*/""}},
        {"SHAPE",       {/*U*/"TRANSP",       /*D*/"TIMBRE",      /*L*/"",             /*R*/"AMP"}},
        {"TIMBRE",      {/*U*/"SHAPE",        /*D*/"COLOR",       /*L*/"",             /*R*/"AMP"}},
        {"AMP",         {/*U*/"SHAPE",        /*D*/"LIM",         /*L*/"TIMBRE",       /*R*/""}},
        {"COLOR",       {/*U*/"TIMBRE",       /*D*/"DEGRADE",     /*L*/"",             /*R*/"LIM"}},
        {"LIM",         {/*U*/"AMP",          /*D*/"PAN",         /*L*/"COLOR",        /*R*/""}},
        {"DEGRADE",     {/*U*/"COLOR",        /*D*/"REDUX",       /*L*/"",             /*R*/"PAN"}},
        {"PAN",         {/*U*/"LIM",          /*D*/"DRY",         /*L*/"DEGRADE",      /*R*/""}},
        {"REDUX",       {/*U*/"DEGRADE",      /*D*/"FILTER",      /*L*/"",             /*R*/"DRY"}},
        {"DRY",         {/*U*/"PAN",          /*D*/"CHO",         /*L*/"REDUX",        /*R*/""}},
        {"FILTER",      {/*U*/"REDUX",        /*D*/"CUTOFF",      /*L*/"",             /*R*/"CHO"}},
        {"CHO",         {/*U*/"DRY",          /*D*/"DEL",         /*L*/"FILTER",       /*R*/""}},
        {"CUTOFF",      {/*U*/"FILTER",       /*D*/"RES",         /*L*/"",             /*R*/"DEL"}},
        {"DEL",         {/*U*/"CHO",          /*D*/"REV",         /*L*/"CUTOFF",       /*R*/""}},
        {"RES",         {/*U*/"CUTOFF",       /*D*/"",            /*L*/"",             /*R*/"REV"}},
        {"REV",         {/*U*/"DEL",          /*D*/"",            /*L*/"RES",          /*R*/""}}
    };
}

} // namespace instrument
} // namespace ui
} // namespace m8
