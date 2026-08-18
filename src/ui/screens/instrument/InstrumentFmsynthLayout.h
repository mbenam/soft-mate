#pragma once
#include "../../ui_types.h"
#include "InstrumentCursorId.h"
#include <vector>
#include <unordered_map>
#include <string>

namespace m8 {
namespace ui {
namespace instrument {

inline std::vector<UI_GridCell> GetFmsynthStaticText() {
    return {
        {"INST.", 0, 0, "TITLE", "", "static", false, 0},
        {"/", 10, 9, "LABEL_DIM", "", "static", false, 0},
        {"/", 16, 9, "LABEL_DIM", "", "static", false, 0},
        {"/", 22, 9, "LABEL_DIM", "", "static", false, 0},
        {"/", 28, 9, "LABEL_DIM", "", "static", false, 0},
    };
}

inline std::vector<UI_GridCell> GetFmsynthDynamicTextDefaults() {
    return {
        {"13", 6, 0, "TITLE", "", "dynamic_text", false, 0},
    };
}

inline std::unordered_map<CursorId, std::vector<UI_GridCell>> GetFmsynthInteractiveFields() {
    using C = CursorId;
    return {
        {C::TYPE, { {"TYPE", 0, 2, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"FMSYNTH", 8, 2, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::CMD_LOAD, { {"LOAD", 22, 2, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::CMD_SAVE, { {"SAVE", 27, 2, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::NAME, { {"NAME", 0, 3, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"------------", 8, 3, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::TRANSP, { {"TRANSP.", 0, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"ON", 8, 4, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::TBL_TIC, { {"TBL.TIC", 13, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"01", 21, 4, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::EQ, { {"EQ", 26, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"--", 29, 4, "VALUE", "LABEL_LITE", "value", true, 0} }},

        // FM Synth Middle Section
        {C::FM_ALGO, { {"ALGO", 0, 6, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 8, 6, "VALUE", "LABEL_LITE", "value", true, 0}, {"A > B > C > D", 11, 6, "ACCENT", "LABEL_LITE", "accent", false, 0} }},

        // Operator Shapes (Row 7)
        {C::FM_OP_A_SHAPE, { {"A", 8, 7, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"SIN", 10, 7, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::FM_OP_B_SHAPE, { {"B", 14, 7, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"SIN", 16, 7, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::FM_OP_C_SHAPE, { {"C", 20, 7, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"SIN", 22, 7, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::FM_OP_D_SHAPE, { {"D", 26, 7, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"SIN", 28, 7, "VALUE", "LABEL_LITE", "value", true, 0} }},

        // Ratio (Row 8)
        {C::FM_OP_A_RATIO, { {"RATIO", 0, 8, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"01.00", 8, 8, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::FM_OP_B_RATIO, { {"01.00", 14, 8, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::FM_OP_C_RATIO, { {"01.00", 20, 8, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::FM_OP_D_RATIO, { {"01.00", 26, 8, "VALUE", "LABEL_LITE", "value", true, 0} }},

        // Level / Feedback (Row 9)
        {C::FM_OP_A_LEV, { {"LEV/FB", 0, 9, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"80", 8, 9, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::FM_OP_A_FB,  { {"00", 11, 9, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::FM_OP_B_LEV, { {"80", 14, 9, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::FM_OP_B_FB,  { {"00", 17, 9, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::FM_OP_C_LEV, { {"80", 20, 9, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::FM_OP_C_FB,  { {"00", 23, 9, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::FM_OP_D_LEV, { {"80", 26, 9, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::FM_OP_D_FB,  { {"00", 29, 9, "VALUE", "LABEL_LITE", "value", true, 0} }},

        // Mod Slot 1 (Row 10)
        {C::FM_OP_A_MOD1, { {"MOD", 0, 10, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"-----", 8, 10, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::FM_OP_B_MOD1, { {"-----", 14, 10, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::FM_OP_C_MOD1, { {"-----", 20, 10, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::FM_OP_D_MOD1, { {"-----", 26, 10, "VALUE", "LABEL_LITE", "value", true, 0} }},

        // Mod Slot 2 (Row 11)
        {C::FM_OP_A_MOD2, { {"-----", 8, 11, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::FM_OP_B_MOD2, { {"-----", 14, 11, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::FM_OP_C_MOD2, { {"-----", 20, 11, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::FM_OP_D_MOD2, { {"-----", 26, 11, "VALUE", "LABEL_LITE", "value", true, 0} }},

        // Bottom Left Column
        {C::FM_MOD1, { {"MOD1", 0, 13, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 8, 13, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::FM_MOD2, { {"MOD2", 0, 14, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 8, 14, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::FM_MOD3, { {"MOD3", 0, 15, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 8, 15, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::FM_MOD4, { {"MOD4", 0, 16, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 8, 16, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::FILTER,  { {"FILTER", 0, 17, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 8, 17, "VALUE", "LABEL_LITE", "value", true, 0}, {"OFF", 10, 17, "ACCENT", "LABEL_LITE", "accent", false, 0} }},
        {C::CUTOFF,  { {"CUTOFF", 0, 18, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"FF", 8, 18, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 18, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {C::RES,     { {"RES", 0, 19, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 8, 19, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 19, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},

        // Bottom Right Column
        {C::AMP, { {"AMP", 17, 13, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 21, 13, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 13, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {C::LIM, { {"LIM", 17, 14, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 21, 14, "VALUE", "LABEL_LITE", "value", true, 0}, {"CLIP", 23, 14, "ACCENT", "LABEL_LITE", "accent", false, 0} }},
        {C::PAN, { {"PAN", 17, 15, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"80", 21, 15, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 15, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {C::DRY, { {"DRY", 17, 16, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"C0", 21, 16, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 16, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {C::CHO, { {"MFX", 17, 17, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 21, 17, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 24, 17, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {C::DEL, { {"DEL", 17, 18, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 21, 18, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 18, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }},
        {C::REV, { {"REV", 17, 19, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 21, 19, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 19, "SLIDER_BG", "LABEL_LITE", "slider", false, 6} }}
    };
}

inline std::unordered_map<CursorId, NavNode<CursorId>> GetFmsynthNavMap() {
    using C = CursorId;
    return {
        {C::TYPE,     {/*U*/C::NONE,          /*D*/C::NAME,          /*L*/C::NONE,          /*R*/C::CMD_LOAD}},
        {C::CMD_LOAD, {/*U*/C::NONE,          /*D*/C::NAME,          /*L*/C::TYPE,          /*R*/C::CMD_SAVE}},
        {C::CMD_SAVE, {/*U*/C::NONE,          /*D*/C::NAME,          /*L*/C::CMD_LOAD,      /*R*/C::NONE}},
        {C::NAME,     {/*U*/C::TYPE,          /*D*/C::TRANSP,        /*L*/C::NONE,          /*R*/C::NONE}},
        {C::TRANSP,   {/*U*/C::NAME,          /*D*/C::FM_ALGO,       /*L*/C::NONE,          /*R*/C::TBL_TIC}},
        {C::TBL_TIC,  {/*U*/C::NAME,          /*D*/C::FM_ALGO,       /*L*/C::TRANSP,        /*R*/C::EQ}},
        {C::EQ,       {/*U*/C::NAME,          /*D*/C::FM_ALGO,       /*L*/C::TBL_TIC,       /*R*/C::NONE}},

        // FM Algo
        {C::FM_ALGO,  {/*U*/C::TRANSP,        /*D*/C::FM_OP_A_SHAPE, /*L*/C::NONE,          /*R*/C::NONE}},

        // Op Shapes
        {C::FM_OP_A_SHAPE, {/*U*/C::FM_ALGO,  /*D*/C::FM_OP_A_RATIO, /*L*/C::NONE,          /*R*/C::FM_OP_B_SHAPE}},
        {C::FM_OP_B_SHAPE, {/*U*/C::FM_ALGO,  /*D*/C::FM_OP_B_RATIO, /*L*/C::FM_OP_A_SHAPE, /*R*/C::FM_OP_C_SHAPE}},
        {C::FM_OP_C_SHAPE, {/*U*/C::FM_ALGO,  /*D*/C::FM_OP_C_RATIO, /*L*/C::FM_OP_B_SHAPE, /*R*/C::FM_OP_D_SHAPE}},
        {C::FM_OP_D_SHAPE, {/*U*/C::FM_ALGO,  /*D*/C::FM_OP_D_RATIO, /*L*/C::FM_OP_C_SHAPE, /*R*/C::NONE}},

        // Op Ratios
        {C::FM_OP_A_RATIO, {/*U*/C::FM_OP_A_SHAPE, /*D*/C::FM_OP_A_LEV,   /*L*/C::NONE,          /*R*/C::FM_OP_B_RATIO}},
        {C::FM_OP_B_RATIO, {/*U*/C::FM_OP_B_SHAPE, /*D*/C::FM_OP_B_LEV,   /*L*/C::FM_OP_A_RATIO, /*R*/C::FM_OP_C_RATIO}},
        {C::FM_OP_C_RATIO, {/*U*/C::FM_OP_C_SHAPE, /*D*/C::FM_OP_C_LEV,   /*L*/C::FM_OP_B_RATIO, /*R*/C::FM_OP_D_RATIO}},
        {C::FM_OP_D_RATIO, {/*U*/C::FM_OP_D_SHAPE, /*D*/C::FM_OP_D_LEV,   /*L*/C::FM_OP_C_RATIO, /*R*/C::NONE}},

        // Op Lev / FB
        {C::FM_OP_A_LEV,   {/*U*/C::FM_OP_A_RATIO, /*D*/C::FM_OP_A_MOD1,  /*L*/C::NONE,          /*R*/C::FM_OP_A_FB}},
        {C::FM_OP_A_FB,    {/*U*/C::FM_OP_A_RATIO, /*D*/C::FM_OP_A_MOD1,  /*L*/C::FM_OP_A_LEV,   /*R*/C::FM_OP_B_LEV}},
        {C::FM_OP_B_LEV,   {/*U*/C::FM_OP_B_RATIO, /*D*/C::FM_OP_B_MOD1,  /*L*/C::FM_OP_A_FB,    /*R*/C::FM_OP_B_FB}},
        {C::FM_OP_B_FB,    {/*U*/C::FM_OP_B_RATIO, /*D*/C::FM_OP_B_MOD1,  /*L*/C::FM_OP_B_LEV,   /*R*/C::FM_OP_C_LEV}},
        {C::FM_OP_C_LEV,   {/*U*/C::FM_OP_C_RATIO, /*D*/C::FM_OP_C_MOD1,  /*L*/C::FM_OP_B_FB,    /*R*/C::FM_OP_C_FB}},
        {C::FM_OP_C_FB,    {/*U*/C::FM_OP_C_RATIO, /*D*/C::FM_OP_C_MOD1,  /*L*/C::FM_OP_C_LEV,   /*R*/C::FM_OP_D_LEV}},
        {C::FM_OP_D_LEV,   {/*U*/C::FM_OP_D_RATIO, /*D*/C::FM_OP_D_MOD1,  /*L*/C::FM_OP_C_FB,    /*R*/C::FM_OP_D_FB}},
        {C::FM_OP_D_FB,    {/*U*/C::FM_OP_D_RATIO, /*D*/C::FM_OP_D_MOD1,  /*L*/C::FM_OP_D_LEV,   /*R*/C::NONE}},

        // Op Mod 1
        {C::FM_OP_A_MOD1,  {/*U*/C::FM_OP_A_LEV,   /*D*/C::FM_OP_A_MOD2,  /*L*/C::NONE,          /*R*/C::FM_OP_B_MOD1}},
        {C::FM_OP_B_MOD1,  {/*U*/C::FM_OP_B_LEV,   /*D*/C::FM_OP_B_MOD2,  /*L*/C::FM_OP_A_MOD1,  /*R*/C::FM_OP_C_MOD1}},
        {C::FM_OP_C_MOD1,  {/*U*/C::FM_OP_C_LEV,   /*D*/C::FM_OP_C_MOD2,  /*L*/C::FM_OP_B_MOD1,  /*R*/C::FM_OP_D_MOD1}},
        {C::FM_OP_D_MOD1,  {/*U*/C::FM_OP_D_LEV,   /*D*/C::FM_OP_D_MOD2,  /*L*/C::FM_OP_C_MOD1,  /*R*/C::NONE}},

        // Op Mod 2
        {C::FM_OP_A_MOD2,  {/*U*/C::FM_OP_A_MOD1,  /*D*/C::FM_MOD1,       /*L*/C::NONE,          /*R*/C::FM_OP_B_MOD2}},
        {C::FM_OP_B_MOD2,  {/*U*/C::FM_OP_B_MOD1,  /*D*/C::FM_MOD1,       /*L*/C::FM_OP_A_MOD2,  /*R*/C::FM_OP_C_MOD2}},
        {C::FM_OP_C_MOD2,  {/*U*/C::FM_OP_C_MOD1,  /*D*/C::AMP,           /*L*/C::FM_OP_B_MOD2,  /*R*/C::FM_OP_D_MOD2}},
        {C::FM_OP_D_MOD2,  {/*U*/C::FM_OP_D_MOD1,  /*D*/C::AMP,           /*L*/C::FM_OP_C_MOD2,  /*R*/C::NONE}},

        // Bottom Left / Right
        {C::FM_MOD1, {/*U*/C::FM_OP_A_MOD2,  /*D*/C::FM_MOD2,       /*L*/C::NONE,          /*R*/C::AMP}},
        {C::AMP,     {/*U*/C::FM_OP_D_MOD2,  /*D*/C::LIM,           /*L*/C::FM_MOD1,       /*R*/C::NONE}},
        {C::FM_MOD2, {/*U*/C::FM_MOD1,       /*D*/C::FM_MOD3,       /*L*/C::NONE,          /*R*/C::LIM}},
        {C::LIM,     {/*U*/C::AMP,           /*D*/C::PAN,           /*L*/C::FM_MOD2,       /*R*/C::NONE}},
        {C::FM_MOD3, {/*U*/C::FM_MOD2,       /*D*/C::FM_MOD4,       /*L*/C::NONE,          /*R*/C::PAN}},
        {C::PAN,     {/*U*/C::LIM,           /*D*/C::DRY,           /*L*/C::FM_MOD3,       /*R*/C::NONE}},
        {C::FM_MOD4, {/*U*/C::FM_MOD3,       /*D*/C::FILTER,        /*L*/C::NONE,          /*R*/C::DRY}},
        {C::DRY,     {/*U*/C::PAN,           /*D*/C::CHO,           /*L*/C::FM_MOD4,       /*R*/C::NONE}},
        {C::FILTER,  {/*U*/C::FM_MOD4,       /*D*/C::CUTOFF,        /*L*/C::NONE,          /*R*/C::CHO}},
        {C::CHO,     {/*U*/C::DRY,           /*D*/C::DEL,           /*L*/C::FILTER,        /*R*/C::NONE}},
        {C::CUTOFF,  {/*U*/C::FILTER,        /*D*/C::RES,           /*L*/C::NONE,          /*R*/C::DEL}},
        {C::DEL,     {/*U*/C::CHO,           /*D*/C::REV,           /*L*/C::CUTOFF,        /*R*/C::NONE}},
        {C::RES,     {/*U*/C::CUTOFF,        /*D*/C::NONE,          /*L*/C::NONE,          /*R*/C::REV}},
        {C::REV,     {/*U*/C::DEL,           /*D*/C::NONE,          /*L*/C::RES,           /*R*/C::NONE}}
    };
}

} // namespace instrument
} // namespace ui
} // namespace m8
