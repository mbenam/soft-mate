#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include "../../ui_types.h"
#include "InstrumentCursorId.h"

namespace m8 {
namespace ui {
namespace instrument {

inline std::vector<UI_GridCell> GetHypersynStaticText() {
    return {
        {"INST.", 0, 0, "TITLE", "TITLE", "title", false, 0},
        {"TYPE", 0, 2, "LABEL_LITE", "LABEL_LITE", "label", false, 0},
        {"NAME", 0, 3, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"TRANSP.", 0, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"TBL.TIC", 13, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"EQ", 26, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0},

        // Scale & Chord (Rows 6-7)
        {"SCALE", 0, 6, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"CHORD", 0, 7, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {">", 10, 7, "ACCENT", "LABEL_LITE", "label", false, 0},

        // Left Column (Rows 10-16)
        {"SHIFT", 0, 10, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"SWARM", 0, 11, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"WIDTH", 0, 12, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"SUBOSC", 0, 13, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"FILTER", 0, 14, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"CUTOFF", 0, 15, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"RES", 0, 16, "LABEL_DIM", "LABEL_LITE", "label", false, 0},

        // Right Column (Rows 10-16)
        {"AMP", 17, 10, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"LIM", 17, 11, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"PAN", 17, 12, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"DRY", 17, 13, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"MFX", 17, 14, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"DEL", 17, 15, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"REV", 17, 16, "LABEL_DIM", "LABEL_LITE", "label", false, 0}
    };
}

inline std::vector<UI_GridCell> GetHypersynDynamicTextDefaults() {
    return {
        {"13", 6, 0, "TITLE", "TITLE", "inst_num", false, 0},
        {"T>128", 29, 2, "LABEL_LITE", "LABEL_LITE", "tempo", false, 0}
    };
}

inline std::unordered_map<CursorId, std::vector<UI_GridCell>> GetHypersynInteractiveFields() {
    using C = CursorId;
    return {
        // Header
        {C::TYPE, { {"HYPERSYN", 8, 2, "LABEL_LITE", "LABEL_LITE", "value", true, 0} }},
        {C::CMD_LOAD, { {"LOAD", 22, 2, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::CMD_SAVE, { {"SAVE", 27, 2, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::NAME, { {"------------", 8, 3, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::TRANSP, { {"ON", 8, 4, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::TBL_TIC, { {"01", 21, 4, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::EQ, { {"--", 29, 4, "VALUE", "LABEL_LITE", "value", true, 0} }},

        // Scale & Chord (Rows 6-7)
        {C::HYP_SCALE, { {"00", 8, 6, "VALUE", "LABEL_LITE", "value", true, 0}, {"DEFAULT", 10, 6, "ACCENT", "LABEL_LITE", "accent", false, 0} }},
        {C::HYP_CHORD_BANK, { {"00", 8, 7, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::HYP_CHORD_N1, { {"00", 12, 7, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::HYP_CHORD_N2, { {"00", 15, 7, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::HYP_CHORD_N3, { {"00", 18, 7, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::HYP_CHORD_N4, { {"00", 21, 7, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::HYP_CHORD_N5, { {"00", 24, 7, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::HYP_CHORD_N6, { {"00", 27, 7, "VALUE", "LABEL_LITE", "value", true, 0} }},

        // Left Column (Rows 10-16)
        {C::HYP_SHIFT,  { {"80", 8, 10, "VALUE", "LABEL_LITE", "value", true, 0}, {"|", 13, 10, "LABEL_DIM", "LABEL_LITE", "center_tick", false, 0} }},
        {C::HYP_SWARM,  { {"00", 8, 11, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 11, "SLIDER", "SLIDER", "slider", false, 6} }},
        {C::HYP_WIDTH,  { {"00", 8, 12, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 12, "SLIDER", "SLIDER", "slider", false, 6} }},
        {C::HYP_SUBOSC, { {"80", 8, 13, "VALUE", "LABEL_LITE", "value", true, 0}, {"|", 13, 13, "LABEL_DIM", "LABEL_LITE", "center_tick", false, 0} }},
        {C::FILTER,     { {"00", 8, 14, "VALUE", "LABEL_LITE", "value", true, 0}, {"OFF", 10, 14, "ACCENT", "LABEL_LITE", "accent", false, 0} }},
        {C::CUTOFF,     { {"FF", 8, 15, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 15, "SLIDER", "SLIDER", "slider", false, 6} }},
        {C::RES,        { {"00", 8, 16, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 16, "SLIDER", "SLIDER", "slider", false, 6} }},

        // Right Column (Rows 10-16)
        {C::AMP, { {"00", 21, 10, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 10, "SLIDER", "SLIDER", "slider", false, 6} }},
        {C::LIM, { {"00", 21, 11, "VALUE", "LABEL_LITE", "value", true, 0}, {"CLIP", 23, 11, "ACCENT", "LABEL_LITE", "accent", false, 0} }},
        {C::PAN, { {"80", 21, 12, "VALUE", "LABEL_LITE", "value", true, 0}, {"|", 26, 12, "LABEL_DIM", "LABEL_LITE", "center_tick", false, 0} }},
        {C::DRY, { {"C0", 21, 13, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 13, "SLIDER", "SLIDER", "slider", false, 6} }},
        {C::CHO, { {"00", 21, 14, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 14, "SLIDER", "SLIDER", "slider", false, 6} }},
        {C::DEL, { {"00", 21, 15, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 15, "SLIDER", "SLIDER", "slider", false, 6} }},
        {C::REV, { {"00", 21, 16, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 16, "SLIDER", "SLIDER", "slider", false, 6} }}
    };
}

inline std::unordered_map<CursorId, NavNode<CursorId>> GetHypersynNavMap() {
    using C = CursorId;
    return {
        // Header
        {C::TYPE,     {C::NONE, C::NAME, C::NONE, C::CMD_LOAD}},
        {C::CMD_LOAD, {C::NONE, C::NAME, C::TYPE, C::CMD_SAVE}},
        {C::CMD_SAVE, {C::NONE, C::NAME, C::CMD_LOAD, C::NONE}},
        {C::NAME,     {C::TYPE, C::TRANSP, C::NONE, C::NONE}},
        {C::TRANSP,   {C::NAME, C::HYP_SCALE, C::NONE, C::TBL_TIC}},
        {C::TBL_TIC,  {C::NAME, C::HYP_SCALE, C::TRANSP, C::EQ}},
        {C::EQ,       {C::NAME, C::HYP_SCALE, C::TBL_TIC, C::NONE}},

        // Scale & Chord (Rows 6-7)
        {C::HYP_SCALE,      {C::TRANSP, C::HYP_CHORD_BANK, C::NONE, C::NONE}},
        {C::HYP_CHORD_BANK, {C::HYP_SCALE, C::HYP_SHIFT, C::NONE, C::HYP_CHORD_N1}},
        {C::HYP_CHORD_N1,   {C::HYP_SCALE, C::HYP_SHIFT, C::HYP_CHORD_BANK, C::HYP_CHORD_N2}},
        {C::HYP_CHORD_N2,   {C::HYP_SCALE, C::HYP_SHIFT, C::HYP_CHORD_N1, C::HYP_CHORD_N3}},
        {C::HYP_CHORD_N3,   {C::HYP_SCALE, C::HYP_SHIFT, C::HYP_CHORD_N2, C::HYP_CHORD_N4}},
        {C::HYP_CHORD_N4,   {C::HYP_SCALE, C::AMP, C::HYP_CHORD_N3, C::HYP_CHORD_N5}},
        {C::HYP_CHORD_N5,   {C::HYP_SCALE, C::AMP, C::HYP_CHORD_N4, C::HYP_CHORD_N6}},
        {C::HYP_CHORD_N6,   {C::HYP_SCALE, C::AMP, C::HYP_CHORD_N5, C::NONE}},

        // Left Column (Rows 10-16)
        {C::HYP_SHIFT,  {C::HYP_CHORD_BANK, C::HYP_SWARM, C::NONE, C::AMP}},
        {C::HYP_SWARM,  {C::HYP_SHIFT, C::HYP_WIDTH, C::NONE, C::LIM}},
        {C::HYP_WIDTH,  {C::HYP_SWARM, C::HYP_SUBOSC, C::NONE, C::PAN}},
        {C::HYP_SUBOSC, {C::HYP_WIDTH, C::FILTER, C::NONE, C::DRY}},
        {C::FILTER,     {C::HYP_SUBOSC, C::CUTOFF, C::NONE, C::CHO}},
        {C::CUTOFF,     {C::FILTER, C::RES, C::NONE, C::DEL}},
        {C::RES,        {C::CUTOFF, C::NONE, C::NONE, C::REV}},

        // Right Column (Rows 10-16)
        {C::AMP, {C::HYP_CHORD_N4, C::LIM, C::HYP_SHIFT, C::NONE}},
        {C::LIM, {C::AMP, C::PAN, C::HYP_SWARM, C::NONE}},
        {C::PAN, {C::LIM, C::DRY, C::HYP_WIDTH, C::NONE}},
        {C::DRY, {C::PAN, C::CHO, C::HYP_SUBOSC, C::NONE}},
        {C::CHO, {C::DRY, C::DEL, C::FILTER, C::NONE}},
        {C::DEL, {C::CHO, C::REV, C::CUTOFF, C::NONE}},
        {C::REV, {C::DEL, C::NONE, C::RES, C::NONE}}
    };
}

} // namespace instrument
} // namespace ui
} // namespace m8
