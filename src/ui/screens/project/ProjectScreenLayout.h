#pragma once
#include "../../ui_types.h"
#include <vector>
#include <unordered_map>
#include <string>

namespace m8 {
namespace ui {
namespace project {

inline std::vector<UI_GridCell> GetProjectStaticText() {
    return {
        {"PROJECT", 0, 0, "TITLE", "", "static", false, 0},
    };
}

inline std::vector<UI_GridCell> GetProjectDynamicTextDefaults() {
    return {
    };
}

inline std::unordered_map<std::string, std::vector<UI_GridCell>> GetProjectInteractiveFields() {
    return {
        {"TEMPO_INT", {
            {"TEMPO", 0, 2, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"140", 14, 2, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {"TEMPO_DEC", {
            {".", 17, 2, "LABEL_DIM", "LABEL_DIM", "static", false, 0},
            {"00", 18, 2, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {"TEMPO_NUDGE", {
            {"< >", 21, 2, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {"TRANSPOSE", {
            {"TRANSPOSE", 0, 3, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 14, 3, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {"GROOVE", {
            {"GROOVE", 0, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 14, 4, "VALUE", "LABEL_LITE", "value", true, 0},
            {"DEFAULT", 16, 4, "ACCENT", "LABEL_LITE", "accent", false, 0}
        }},
        {"SCALE", {
            {"SCALE", 0, 5, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 14, 5, "VALUE", "LABEL_LITE", "value", true, 0},
            {"C CHROMATIC", 16, 5, "ACCENT", "LABEL_LITE", "accent", false, 0}
        }},
        {"LIVE_QUANTIZE", {
            {"LIVE QUANTIZE", 0, 6, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 14, 6, "VALUE", "LABEL_LITE", "value", true, 0},
            {"CHAIN LEN", 16, 6, "ACCENT", "LABEL_LITE", "accent", false, 0}
        }},
        {"MIDI_SETTINGS", {
            {"MIDI", 0, 8, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"SETTINGS", 14, 8, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {"MIDI_MAPPINGS", {
            {"MAPPINGS", 23, 8, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {"NAME", {
            {"NAME", 0, 10, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"DEMO2-------", 14, 10, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {"PROJ_LOAD", {
            {"PROJECT", 0, 11, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"LOAD", 14, 11, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {"PROJ_SAVE", {
            {"SAVE", 19, 11, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {"PROJ_NEW", {
            {"NEW", 24, 11, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {"EXPORT_RENDER", {
            {"EXPORT/SHARE", 0, 12, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"RENDER", 14, 12, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {"EXPORT_BUNDLE", {
            {"BUNDLE", 21, 12, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {"CLEAR_PHRASES", {
            {"CLEAR UNUSED", 0, 13, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"PHRASES", 14, 13, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {"CLEAR_INST", {
            {"INST/TBL", 22, 13, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {"INST_POOL", {
            {"INST. POOL", 0, 14, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"VIEW INST.POOL", 14, 14, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {"TIME_STATS", {
            {"TIME STATS", 0, 16, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"VIEW TIME STATS", 14, 16, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {"SYSTEM_SETTINGS", {
            {"SYSTEM", 0, 17, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"SETTINGS", 14, 17, "VALUE", "LABEL_LITE", "value", true, 0}
        }}
    };
}

inline std::unordered_map<std::string, NavNode> GetProjectNavMap() {
    return {
        {"TEMPO_INT",       {/*U*/"",                /*D*/"TRANSPOSE",     /*L*/"",               /*R*/"TEMPO_DEC"}},
        {"TEMPO_DEC",       {/*U*/"",                /*D*/"TRANSPOSE",     /*L*/"TEMPO_INT",      /*R*/"TEMPO_NUDGE"}},
        {"TEMPO_NUDGE",     {/*U*/"",                /*D*/"TRANSPOSE",     /*L*/"TEMPO_DEC",      /*R*/""}},
        {"TRANSPOSE",       {/*U*/"TEMPO_INT",       /*D*/"GROOVE",        /*L*/"",               /*R*/""}},
        {"GROOVE",          {/*U*/"TRANSPOSE",       /*D*/"SCALE",         /*L*/"",               /*R*/""}},
        {"SCALE",           {/*U*/"GROOVE",          /*D*/"LIVE_QUANTIZE", /*L*/"",               /*R*/""}},
        {"LIVE_QUANTIZE",   {/*U*/"SCALE",           /*D*/"MIDI_SETTINGS", /*L*/"",               /*R*/""}},
        {"MIDI_SETTINGS",   {/*U*/"LIVE_QUANTIZE",   /*D*/"NAME",          /*L*/"",               /*R*/"MIDI_MAPPINGS"}},
        {"MIDI_MAPPINGS",   {/*U*/"LIVE_QUANTIZE",   /*D*/"NAME",          /*L*/"MIDI_SETTINGS",  /*R*/""}},
        {"NAME",            {/*U*/"MIDI_SETTINGS",   /*D*/"PROJ_LOAD",     /*L*/"",               /*R*/""}},
        {"PROJ_LOAD",       {/*U*/"NAME",            /*D*/"EXPORT_RENDER", /*L*/"",               /*R*/"PROJ_SAVE"}},
        {"PROJ_SAVE",       {/*U*/"NAME",            /*D*/"EXPORT_BUNDLE", /*L*/"PROJ_LOAD",      /*R*/"PROJ_NEW"}},
        {"PROJ_NEW",        {/*U*/"NAME",            /*D*/"EXPORT_BUNDLE", /*L*/"PROJ_SAVE",      /*R*/""}},
        {"EXPORT_RENDER",   {/*U*/"PROJ_LOAD",       /*D*/"CLEAR_PHRASES", /*L*/"",               /*R*/"EXPORT_BUNDLE"}},
        {"EXPORT_BUNDLE",   {/*U*/"PROJ_SAVE",       /*D*/"CLEAR_INST",    /*L*/"EXPORT_RENDER",  /*R*/""}},
        {"CLEAR_PHRASES",   {/*U*/"EXPORT_RENDER",   /*D*/"INST_POOL",     /*L*/"",               /*R*/"CLEAR_INST"}},
        {"CLEAR_INST",      {/*U*/"EXPORT_BUNDLE",   /*D*/"INST_POOL",     /*L*/"CLEAR_PHRASES",  /*R*/""}},
        {"INST_POOL",       {/*U*/"CLEAR_PHRASES",   /*D*/"TIME_STATS",    /*L*/"",               /*R*/""}},
        {"TIME_STATS",      {/*U*/"INST_POOL",       /*D*/"SYSTEM_SETTINGS",/*L*/"",              /*R*/""}},
        {"SYSTEM_SETTINGS", {/*U*/"TIME_STATS",      /*D*/"",              /*L*/"",               /*R*/""}}
    };
}

} // namespace project
} // namespace ui
} // namespace m8
