#pragma once
#include "../../ui_types.h"
#include "InstrumentCursorId.h"
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

inline std::unordered_map<CursorId, std::vector<UI_GridCell>> GetSamplerInteractiveFields() {
    using C = CursorId;
    return {
        {C::TYPE, {
            {"TYPE", 0, 2, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"SAMPLER ", 8, 2, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::CMD_LOAD, { {"LOAD", 22, 2, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::CMD_SAVE, { {"SAVE", 27, 2, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::NAME, {
            {"NAME", 0, 3, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"------------", 8, 3, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::TRANSP, {
            {"TRANSP.", 0, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"ON", 8, 4, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::TBL_TIC, {
            {"TBL.TIC", 13, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"FF", 21, 4, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::EQ, {
            {"EQ", 26, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"--", 29, 4, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::SAMPLE_LOAD, {
            {"SAMPLE", 0, 6, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"LOAD", 8, 6, "ACCENT", "LABEL_LITE", "accent", false, 0}
        }},
        {C::SAMPLE_REC, { {"REC.", 25, 6, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::SLICE, {
            {"SLICE", 0, 8, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 8, 8, "VALUE", "LABEL_LITE", "value", true, 0},
            {"OFF", 10, 8, "ACCENT", "LABEL_LITE", "accent", false, 0}
        }},
        {C::AMP, {
            {"AMP", 17, 8, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 21, 8, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 24, 8, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {C::PLAY, {
            {"PLAY", 0, 9, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 8, 9, "VALUE", "LABEL_LITE", "value", true, 0},
            {"FWD", 10, 9, "ACCENT", "LABEL_LITE", "accent", false, 0}
        }},
        {C::LIM, {
            {"LIM", 17, 9, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 21, 9, "VALUE", "LABEL_LITE", "value", true, 0},
            {"CLIP", 24, 9, "ACCENT", "LABEL_LITE", "accent", false, 0}
        }},
        {C::START, {
            {"START", 0, 10, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 8, 10, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 10, 10, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {C::PAN, {
            {"PAN", 17, 10, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"80", 21, 10, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 24, 10, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {C::LOOP_ST, {
            {"LOOP ST", 0, 11, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 8, 11, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 10, 11, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {C::DRY, {
            {"DRY", 17, 11, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"C0", 21, 11, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 24, 11, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {C::LENGTH, {
            {"LENGTH", 0, 12, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"FF", 8, 12, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 10, 12, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {C::CHO, {
            {"MFX", 17, 12, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 21, 12, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 24, 12, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {C::DETUNE, {
            {"DETUNE", 0, 13, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"80", 8, 13, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 10, 13, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {C::DEL, {
            {"DEL", 17, 13, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 21, 13, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 24, 13, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {C::DEGRADE, {
            {"DEGRADE", 0, 14, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 8, 14, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 10, 14, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {C::REV, {
            {"REV", 17, 14, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 21, 14, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 24, 14, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {C::FILTER, {
            {"FILTER", 0, 15, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 8, 15, "VALUE", "LABEL_LITE", "value", true, 0},
            {"OFF", 10, 15, "ACCENT", "LABEL_LITE", "accent", false, 0}
        }},
        {C::CUTOFF, {
            {"CUTOFF", 0, 16, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"FF", 8, 16, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 10, 16, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }},
        {C::RES, {
            {"RES", 0, 17, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 8, 17, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", 10, 17, "SLIDER_BG", "LABEL_LITE", "slider", false, 6}
        }}
    };
}

// Map dictating logical jumps between non-grid UI fields
inline std::unordered_map<CursorId, NavNode<CursorId>> GetSamplerNavMap() {
    using C = CursorId;
    return {
        {C::TYPE,        {/*U*/C::NONE,        /*D*/C::NAME,        /*L*/C::NONE,        /*R*/C::CMD_LOAD}},
        {C::CMD_LOAD,    {/*U*/C::NONE,        /*D*/C::NAME,        /*L*/C::TYPE,        /*R*/C::CMD_SAVE}},
        {C::CMD_SAVE,    {/*U*/C::NONE,        /*D*/C::NAME,        /*L*/C::CMD_LOAD,    /*R*/C::NONE}},
        {C::NAME,        {/*U*/C::TYPE,        /*D*/C::TRANSP,      /*L*/C::NONE,        /*R*/C::NONE}},
        {C::TRANSP,      {/*U*/C::NAME,        /*D*/C::SAMPLE_LOAD, /*L*/C::NONE,        /*R*/C::TBL_TIC}},
        {C::TBL_TIC,     {/*U*/C::NAME,        /*D*/C::SAMPLE_REC,  /*L*/C::TRANSP,      /*R*/C::EQ}},
        {C::EQ,          {/*U*/C::NAME,        /*D*/C::SAMPLE_REC,  /*L*/C::TBL_TIC,     /*R*/C::NONE}},
        {C::SAMPLE_LOAD, {/*U*/C::TRANSP,      /*D*/C::SLICE,       /*L*/C::NONE,        /*R*/C::SAMPLE_REC}},
        {C::SAMPLE_REC,  {/*U*/C::EQ,          /*D*/C::AMP,         /*L*/C::SAMPLE_LOAD, /*R*/C::NONE}},
        {C::SLICE,       {/*U*/C::SAMPLE_LOAD, /*D*/C::PLAY,        /*L*/C::NONE,        /*R*/C::AMP}},
        {C::AMP,         {/*U*/C::SAMPLE_REC,  /*D*/C::LIM,         /*L*/C::SLICE,       /*R*/C::NONE}},
        {C::PLAY,        {/*U*/C::SLICE,       /*D*/C::START,       /*L*/C::NONE,        /*R*/C::LIM}},
        {C::LIM,         {/*U*/C::AMP,         /*D*/C::PAN,         /*L*/C::PLAY,        /*R*/C::NONE}},
        {C::START,       {/*U*/C::PLAY,        /*D*/C::LOOP_ST,     /*L*/C::NONE,        /*R*/C::PAN}},
        {C::PAN,         {/*U*/C::LIM,         /*D*/C::DRY,         /*L*/C::START,       /*R*/C::NONE}},
        {C::LOOP_ST,     {/*U*/C::START,       /*D*/C::LENGTH,      /*L*/C::NONE,        /*R*/C::DRY}},
        {C::DRY,         {/*U*/C::PAN,         /*D*/C::CHO,         /*L*/C::LOOP_ST,     /*R*/C::NONE}},
        {C::LENGTH,      {/*U*/C::LOOP_ST,     /*D*/C::DETUNE,      /*L*/C::NONE,        /*R*/C::CHO}},
        {C::CHO,         {/*U*/C::DRY,         /*D*/C::DEL,         /*L*/C::LENGTH,      /*R*/C::NONE}},
        {C::DETUNE,      {/*U*/C::LENGTH,      /*D*/C::DEGRADE,     /*L*/C::NONE,        /*R*/C::DEL}},
        {C::DEL,         {/*U*/C::CHO,         /*D*/C::REV,         /*L*/C::DETUNE,      /*R*/C::NONE}},
        {C::DEGRADE,     {/*U*/C::DETUNE,      /*D*/C::FILTER,      /*L*/C::NONE,        /*R*/C::REV}},
        {C::REV,         {/*U*/C::DEL,         /*D*/C::FILTER,      /*L*/C::DEGRADE,     /*R*/C::NONE}},
        {C::FILTER,      {/*U*/C::DEGRADE,     /*D*/C::CUTOFF,      /*L*/C::NONE,        /*R*/C::REV}},
        {C::CUTOFF,      {/*U*/C::FILTER,      /*D*/C::RES,         /*L*/C::NONE,        /*R*/C::REV}},
        {C::RES,         {/*U*/C::CUTOFF,      /*D*/C::NONE,        /*L*/C::NONE,        /*R*/C::REV}}
    };
}

} // namespace instrument
} // namespace ui
} // namespace m8
