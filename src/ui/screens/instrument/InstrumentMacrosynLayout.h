#pragma once
#include "../../ui_types.h"
#include "InstrumentCursorId.h"
#include <vector>
#include <unordered_map>
#include <string>

namespace m8 {
namespace ui {
namespace instrument {

inline std::vector<UI_GridCell> GetMacrosynStaticText() {
    return {
        {"INST.", 0, 0, "TITLE", "", "static", false, 0},
    };
}

inline std::vector<UI_GridCell> GetMacrosynDynamicTextDefaults() {
    return {
        {"13", 6, 0, "TITLE", "", "dynamic_text", false, 0},
    };
}

inline std::unordered_map<CursorId, std::vector<UI_GridCell>> GetMacrosynInteractiveFields() {
    using C = CursorId;
    return {
        {C::TYPE, { {"TYPE", 0, 2, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"MACROSYN", 8, 2, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::CMD_LOAD, { {"LOAD", 22, 2, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::CMD_SAVE, { {"SAVE", 27, 2, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::NAME, { {"NAME", 0, 3, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"------------", 8, 3, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::TRANSP, { {"TRANSP.", 0, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"ON", 8, 4, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::TBL_TIC, { {"TBL.TIC", 13, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"FF", 21, 4, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::EQ, { {"EQ", 26, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"--", 29, 4, "VALUE", "LABEL_LITE", "value", true, 0} }},

        // Macrosyn Left Column
        {C::SHAPE, { {"SHAPE", 0, 6, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 8, 6, "VALUE", "LABEL_LITE", "value", true, 0}, {"CSAW", 10, 6, "ACCENT", "LABEL_LITE", "accent", false, 0} }},
        {C::TIMBRE, { {"TIMBRE", 0, 8, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"80", 8, 8, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 8, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {C::COLOR, { {"COLOR", 0, 9, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"80", 8, 9, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 9, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {C::DEGRADE, { {"DEGRADE", 0, 10, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 8, 10, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 10, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {C::REDUX, { {"REDUX", 0, 11, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 8, 11, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 11, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {C::FILTER, { {"FILTER", 0, 12, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 8, 12, "VALUE", "LABEL_LITE", "value", true, 0}, {"OFF", 10, 12, "ACCENT", "LABEL_LITE", "accent", false, 0} }},
        {C::CUTOFF, { {"CUTOFF", 0, 13, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"FF", 8, 13, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 13, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {C::RES, { {"RES", 0, 14, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 8, 14, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 14, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},

        // Right Column
        {C::AMP, { {"AMP", 17, 8, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 21, 8, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 8, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {C::LIM, { {"LIM", 17, 9, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 21, 9, "VALUE", "LABEL_LITE", "value", true, 0}, {"CLIP", 23, 9, "ACCENT", "LABEL_LITE", "accent", false, 0} }},
        {C::PAN, { {"PAN", 17, 10, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"80", 21, 10, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 10, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {C::DRY, { {"DRY", 17, 11, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"C0", 21, 11, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 11, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {C::CHO, { {"CHO", 17, 12, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 21, 12, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 12, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {C::DEL, { {"DEL", 17, 13, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 21, 13, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 13, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {C::REV, { {"REV", 17, 14, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 21, 14, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 14, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }}
    };
}

inline std::unordered_map<CursorId, NavNode<CursorId>> GetMacrosynNavMap() {
    using C = CursorId;
    return {
        {C::TYPE,     {/*U*/C::NONE,     /*D*/C::NAME,     /*L*/C::NONE,     /*R*/C::CMD_LOAD}},
        {C::CMD_LOAD, {/*U*/C::NONE,     /*D*/C::NAME,     /*L*/C::TYPE,     /*R*/C::CMD_SAVE}},
        {C::CMD_SAVE, {/*U*/C::NONE,     /*D*/C::NAME,     /*L*/C::CMD_LOAD, /*R*/C::NONE}},
        {C::NAME,     {/*U*/C::TYPE,     /*D*/C::TRANSP,   /*L*/C::NONE,     /*R*/C::NONE}},
        {C::TRANSP,   {/*U*/C::NAME,     /*D*/C::SHAPE,    /*L*/C::NONE,     /*R*/C::TBL_TIC}},
        {C::TBL_TIC,  {/*U*/C::NAME,     /*D*/C::SHAPE,    /*L*/C::TRANSP,   /*R*/C::EQ}},
        {C::EQ,       {/*U*/C::NAME,     /*D*/C::SHAPE,    /*L*/C::TBL_TIC,  /*R*/C::NONE}},
        {C::SHAPE,    {/*U*/C::TRANSP,   /*D*/C::TIMBRE,   /*L*/C::NONE,     /*R*/C::AMP}},
        {C::TIMBRE,   {/*U*/C::SHAPE,    /*D*/C::COLOR,    /*L*/C::NONE,     /*R*/C::AMP}},
        {C::AMP,      {/*U*/C::SHAPE,    /*D*/C::LIM,      /*L*/C::TIMBRE,   /*R*/C::NONE}},
        {C::COLOR,    {/*U*/C::TIMBRE,   /*D*/C::DEGRADE,  /*L*/C::NONE,     /*R*/C::LIM}},
        {C::LIM,      {/*U*/C::AMP,      /*D*/C::PAN,      /*L*/C::COLOR,    /*R*/C::NONE}},
        {C::DEGRADE,  {/*U*/C::COLOR,    /*D*/C::REDUX,    /*L*/C::NONE,     /*R*/C::PAN}},
        {C::PAN,      {/*U*/C::LIM,      /*D*/C::DRY,      /*L*/C::DEGRADE, /*R*/C::NONE}},
        {C::REDUX,    {/*U*/C::DEGRADE,  /*D*/C::FILTER,   /*L*/C::NONE,     /*R*/C::DRY}},
        {C::DRY,      {/*U*/C::PAN,      /*D*/C::CHO,      /*L*/C::REDUX,    /*R*/C::NONE}},
        {C::FILTER,   {/*U*/C::REDUX,    /*D*/C::CUTOFF,   /*L*/C::NONE,     /*R*/C::CHO}},
        {C::CHO,      {/*U*/C::DRY,      /*D*/C::DEL,      /*L*/C::FILTER,   /*R*/C::NONE}},
        {C::CUTOFF,   {/*U*/C::FILTER,   /*D*/C::RES,      /*L*/C::NONE,     /*R*/C::DEL}},
        {C::DEL,      {/*U*/C::CHO,      /*D*/C::REV,      /*L*/C::CUTOFF,   /*R*/C::NONE}},
        {C::RES,      {/*U*/C::CUTOFF,   /*D*/C::NONE,     /*L*/C::NONE,     /*R*/C::REV}},
        {C::REV,      {/*U*/C::DEL,      /*D*/C::NONE,     /*L*/C::RES,      /*R*/C::NONE}}
    };
}

} // namespace instrument
} // namespace ui
} // namespace m8
