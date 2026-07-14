#pragma once
#include "../../ui_types.h"
#include <vector>
#include <unordered_map>
#include <string>

namespace m8 {
namespace ui {
namespace instrument {

inline std::vector<UI_GridCell> GetSamplerStaticText() {
    return {
        {"INST.", 0, 0, "TITLE", "", "static", false, 0},
        {"SCP", 34, 26, "LABEL_DIM", "", "static", false, 0},
    };
}

inline std::vector<UI_GridCell> GetSamplerDynamicTextDefaults() {
    return {
        {"13", 6, 0, "TITLE", "", "dynamic_text", false, 0},
    };
}

inline std::unordered_map<std::string, std::vector<UI_GridCell>> GetSamplerInteractiveFields() {
    return {
        {"TYPE", {
            {"TYPE", 0, 2, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"SAMPLER ", 8, 2, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {"CMD_LOAD", { {"LOAD", 22, 2, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"CMD_SAVE", { {"SAVE", 27, 2, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"NAME", {
            {"NAME", 0, 3, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"------------", 8, 3, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {"TRANSP", {
            {"TRANSP.", 0, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"ON", 8, 4, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {"TBL_TIC", {
            {"TBL.TIC", 13, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"FF", 21, 4, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {"EQ", {
            {"EQ", 26, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"--", 29, 4, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {"SAMPLE_LOAD", {
            {"SAMPLE", 0, 6, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"LOAD", 8, 6, "ACCENT", "LABEL_LITE", "accent", false, 0}
        }},
        {"SAMPLE_REC", { {"REC.", 25, 6, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"SLICE", {
            {"SLICE", 0, 8, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 8, 8, "VALUE", "LABEL_LITE", "value", true, 0},
            {"OFF", 10, 8, "ACCENT", "LABEL_LITE", "accent", false, 0}
        }},
        {"AMP", {
            {"AMP", 17, 8, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 21, 8, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 24, 8, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {"PLAY", {
            {"PLAY", 0, 9, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 8, 9, "VALUE", "LABEL_LITE", "value", true, 0},
            {"FWD", 10, 9, "ACCENT", "LABEL_LITE", "accent", false, 0}
        }},
        {"LIM", {
            {"LIM", 17, 9, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 21, 9, "VALUE", "LABEL_LITE", "value", true, 0},
            {"CLIP", 24, 9, "ACCENT", "LABEL_LITE", "accent", false, 0}
        }},
        {"START", {
            {"START", 0, 10, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 8, 10, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 10, 10, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {"PAN", {
            {"PAN", 17, 10, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"80", 21, 10, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 24, 10, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {"LOOP_ST", {
            {"LOOP ST", 0, 11, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 8, 11, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 10, 11, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {"DRY", {
            {"DRY", 17, 11, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"C0", 21, 11, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 24, 11, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {"LENGTH", {
            {"LENGTH", 0, 12, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"FF", 8, 12, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 10, 12, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {"CHO", {
            {"CHO", 17, 12, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 21, 12, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 24, 12, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {"DETUNE", {
            {"DETUNE", 0, 13, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"80", 8, 13, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 10, 13, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {"DEL", {
            {"DEL", 17, 13, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 21, 13, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 24, 13, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {"DEGRADE", {
            {"DEGRADE", 0, 14, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 8, 14, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 10, 14, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {"REV", {
            {"REV", 17, 14, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 21, 14, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 24, 14, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {"FILTER", {
            {"FILTER", 0, 15, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 8, 15, "VALUE", "LABEL_LITE", "value", true, 0},
            {"OFF", 10, 15, "ACCENT", "LABEL_LITE", "accent", false, 0}
        }},
        {"CUTOFF", {
            {"CUTOFF", 0, 16, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"FF", 8, 16, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 10, 16, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {"RES", {
            {"RES", 0, 17, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 8, 17, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 10, 17, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }}
    };
}

// Map dictating logical jumps between non-grid UI fields
inline std::unordered_map<std::string, NavNode> GetSamplerNavMap() {
    return {
        {"TYPE",        {/*U*/"",             /*D*/"NAME",        /*L*/"",             /*R*/"CMD_LOAD"}},
        {"CMD_LOAD",    {/*U*/"",             /*D*/"NAME",        /*L*/"TYPE",         /*R*/"CMD_SAVE"}},
        {"CMD_SAVE",    {/*U*/"",             /*D*/"NAME",        /*L*/"CMD_LOAD",     /*R*/""}},
        {"NAME",        {/*U*/"TYPE",         /*D*/"TRANSP",      /*L*/"",             /*R*/""}},
        {"TRANSP",      {/*U*/"NAME",         /*D*/"SAMPLE_LOAD", /*L*/"",             /*R*/"TBL_TIC"}},
        {"TBL_TIC",     {/*U*/"NAME",         /*D*/"SAMPLE_REC",  /*L*/"TRANSP",       /*R*/"EQ"}},
        {"EQ",          {/*U*/"NAME",         /*D*/"SAMPLE_REC",  /*L*/"TBL_TIC",      /*R*/""}},
        {"SAMPLE_LOAD", {/*U*/"TRANSP",       /*D*/"SLICE",       /*L*/"",             /*R*/"SAMPLE_REC"}},
        {"SAMPLE_REC",  {/*U*/"EQ",           /*D*/"AMP",         /*L*/"SAMPLE_LOAD",  /*R*/""}},
        {"SLICE",       {/*U*/"SAMPLE_LOAD",  /*D*/"PLAY",        /*L*/"",             /*R*/"AMP"}},
        {"AMP",         {/*U*/"SAMPLE_REC",   /*D*/"LIM",         /*L*/"SLICE",        /*R*/""}},
        {"PLAY",        {/*U*/"SLICE",        /*D*/"START",       /*L*/"",             /*R*/"LIM"}},
        {"LIM",         {/*U*/"AMP",          /*D*/"PAN",         /*L*/"PLAY",         /*R*/""}},
        {"START",       {/*U*/"PLAY",         /*D*/"LOOP_ST",     /*L*/"",             /*R*/"PAN"}},
        {"PAN",         {/*U*/"LIM",          /*D*/"DRY",         /*L*/"START",        /*R*/""}},
        {"LOOP_ST",     {/*U*/"START",        /*D*/"LENGTH",      /*L*/"",             /*R*/"DRY"}},
        {"DRY",         {/*U*/"PAN",          /*D*/"CHO",         /*L*/"LOOP_ST",      /*R*/""}},
        {"LENGTH",      {/*U*/"LOOP_ST",      /*D*/"DETUNE",      /*L*/"",             /*R*/"CHO"}},
        {"CHO",         {/*U*/"DRY",          /*D*/"DEL",         /*L*/"LENGTH",       /*R*/""}},
        {"DETUNE",      {/*U*/"LENGTH",       /*D*/"DEGRADE",     /*L*/"",             /*R*/"DEL"}},
        {"DEL",         {/*U*/"CHO",          /*D*/"REV",         /*L*/"DETUNE",       /*R*/""}},
        {"DEGRADE",     {/*U*/"DETUNE",       /*D*/"FILTER",      /*L*/"",             /*R*/"REV"}},
        {"REV",         {/*U*/"DEL",          /*D*/"FILTER",      /*L*/"DEGRADE",      /*R*/""}},
        {"FILTER",      {/*U*/"DEGRADE",      /*D*/"CUTOFF",      /*L*/"",             /*R*/"REV"}},
        {"CUTOFF",      {/*U*/"FILTER",       /*D*/"RES",         /*L*/"",             /*R*/"REV"}},
        {"RES",         {/*U*/"CUTOFF",       /*D*/"",            /*L*/"",             /*R*/"REV"}}
    };
}

} // namespace instrument
} // namespace ui
} // namespace m8
