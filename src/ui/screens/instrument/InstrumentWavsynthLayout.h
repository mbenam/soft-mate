#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include "../../ui_types.h"
#include "InstrumentCursorId.h"

namespace m8 {
namespace ui {
namespace instrument {

// All 70 SHAPE names as fw 6.5.2 draws them (Appendix B). Shapes >= 0x09 do not
// sound until Phase 3 -- they alias to sine in the engine -- but a loaded .m8s
// can carry one, and showing "SINE" for WT-EFX:CYBERNET would be wrong on
// screen. Padded to a constant width so a shorter name cannot leave characters
// of a longer one behind.
inline const char* WavShapeName(int shape) {
    static const char* const kNames[70] = {
        "PULSE 12%      ", "PULSE 25%      ", "PULSE 50%      ", "PULSE 75%      ",
        "SAW            ", "TRIANGLE       ", "SINE           ", "NOISE PITCHED  ",
        "NOISE          ", "WT-OSC:CRUSH   ", "WT-OSC:FOLDING ", "WT-OSC:FREQ    ",
        "WT-OSC:FUZZY   ", "WT-OSC:GHOST   ", "WT-OSC:GRAPHIC ", "WT-OSC:LFOPLAY ",
        "WT-OSC:LIQUID  ", "WT-OSC:MORPHING", "WT-OSC:MYSTIC  ", "WT-OSC:STICKY  ",
        "WT-OSC:TIDAL   ", "WT-OSC:TIDY    ", "WT-OSC:TUBE    ", "WT-OSC:UMBRELLA",
        "WT-OSC:UNWIND  ", "WT-OSC:VIRAL   ", "WT-OSC:WAVES   ", "WT-BNK:DRIP    ",
        "WT-BNK:FROGGY  ", "WT-BNK:INSONIC ", "WT-BNK:RADIUS  ", "WT-BNK:SCRATCH ",
        "WT-BNK:SMOOTH  ", "WT-BNK:WOBBLE  ", "WT-HRM:ASYMMTRY", "WT-HRM:BLEEN   ",
        "WT-HRM:FRACTAL ", "WT-HRM:GENTLE  ", "WT-HRM:HARMONIC", "WT-HRM:HYPNOTIC",
        "WT-HRM:ITERATIV", "WT-HRM:MICROWAV", "WT-HRM:PLAITS01", "WT-HRM:PLAITS02",
        "WT-HRM:RISEFALL", "WT-HRM:TONAL   ", "WT-HRM:TWINE   ", "WT-EFX:ALIEN   ",
        "WT-EFX:CYBERNET", "WT-EFX:DISORDR ", "WT-EFX:FORMANT ", "WT-EFX:HYPER   ",
        "WT-EFX:JAGGED  ", "WT-EFX:MIXED   ", "WT-EFX:MULTIPLY", "WT-EFX:NOWHERE ",
        "WT-EFX:PINBALL ", "WT-EFX:RINGS   ", "WT-EFX:SHIMMER ", "WT-EFX:SPECTRAL",
        "WT-EFX:SPOOKY  ", "WT-EFX:TRANSFRM", "WT-EFX:TWISTED ", "WT-EFX:VOCAL   ",
        "WT-EFX:WASHED  ", "WT-EFX:WONDER  ", "WT-EFX:WOWEE   ", "WT-EFX:ZAP     ",
        "WT-VOX:BRAIDS  ", "WT-VOX:VOXSYNTH"
    };
    if (shape < 0 || shape >= 70) return "UNKNOWN        ";
    return kNames[shape];
}

inline std::vector<UI_GridCell> GetWavsynthStaticText() {
    return {
        {"INST.", 0, 0, "TITLE", "TITLE", "title", false, 0},
        {"TYPE", 0, 2, "LABEL_LITE", "LABEL_LITE", "label", false, 0},
        {"NAME", 0, 3, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"TRANSP.", 0, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"TBL.TIC", 13, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"EQ", 26, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0},

        // Left Column (Rows 6, 8-14)
        {"SHAPE", 0, 6, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"SIZE", 0, 8, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"MULT", 0, 9, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"WARP", 0, 10, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"SCAN", 0, 11, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"FILTER", 0, 12, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"CUTOFF", 0, 13, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"RES", 0, 14, "LABEL_DIM", "LABEL_LITE", "label", false, 0},

        // Right Column (Rows 8-14)
        {"AMP", 17, 8, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"LIM", 17, 9, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"PAN", 17, 10, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"DRY", 17, 11, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"MFX", 17, 12, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"DEL", 17, 13, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
        {"REV", 17, 14, "LABEL_DIM", "LABEL_LITE", "label", false, 0}
    };
}

inline std::vector<UI_GridCell> GetWavsynthDynamicTextDefaults() {
    return {
        {"13", 6, 0, "TITLE", "TITLE", "inst_num", false, 0},
        {"T>128", 29, 2, "LABEL_LITE", "LABEL_LITE", "tempo", false, 0}
    };
}

inline std::unordered_map<CursorId, std::vector<UI_GridCell>> GetWavsynthInteractiveFields() {
    using C = CursorId;
    return {
        // Header
        {C::TYPE, { {"WAVSYNTH", 8, 2, "LABEL_LITE", "LABEL_LITE", "value", true, 0} }},
        {C::CMD_LOAD, { {"LOAD", 22, 2, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::CMD_SAVE, { {"SAVE", 27, 2, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::NAME, { {"------------", 8, 3, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::TRANSP, { {"ON", 8, 4, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::TBL_TIC, { {"01", 21, 4, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::EQ, { {"--", 29, 4, "VALUE", "LABEL_LITE", "value", true, 0} }},

        // Left Column (Rows 6, 8-14)
        {C::SHAPE,    { {"00", 8, 6, "VALUE", "LABEL_LITE", "value", true, 0}, {"PULSE 12%", 10, 6, "ACCENT", "LABEL_LITE", "accent", false, 0} }},
        {C::WAV_SIZE, { {"20", 8, 8, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 8, "SLIDER", "SLIDER", "slider", false, 6} }},
        {C::WAV_MULT, { {"00", 8, 9, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 9, "SLIDER", "SLIDER", "slider", false, 6} }},
        {C::WAV_WARP, { {"00", 8, 10, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 10, "SLIDER", "SLIDER", "slider", false, 6} }},
        {C::WAV_SCAN, { {"00", 8, 11, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 11, "SLIDER", "SLIDER", "slider", false, 6} }},
        {C::FILTER,   { {"00", 8, 12, "VALUE", "LABEL_LITE", "value", true, 0}, {"OFF", 10, 12, "ACCENT", "LABEL_LITE", "accent", false, 0} }},
        {C::CUTOFF,   { {"FF", 8, 13, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 13, "SLIDER", "SLIDER", "slider", false, 6} }},
        {C::RES,      { {"00", 8, 14, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 10, 14, "SLIDER", "SLIDER", "slider", false, 6} }},

        // Right Column (Rows 8-14)
        {C::AMP, { {"00", 21, 8, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 8, "SLIDER", "SLIDER", "slider", false, 6} }},
        {C::LIM, { {"00", 21, 9, "VALUE", "LABEL_LITE", "value", true, 0}, {"CLIP", 23, 9, "ACCENT", "LABEL_LITE", "accent", false, 0} }},
        {C::PAN, { {"80", 21, 10, "VALUE", "LABEL_LITE", "value", true, 0}, {"|", 26, 10, "LABEL_DIM", "LABEL_LITE", "center_tick", false, 0} }},
        {C::DRY, { {"C0", 21, 11, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 11, "SLIDER", "SLIDER", "slider", false, 6} }},
        {C::CHO, { {"00", 21, 12, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 12, "SLIDER", "SLIDER", "slider", false, 6} }},
        {C::DEL, { {"00", 21, 13, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 13, "SLIDER", "SLIDER", "slider", false, 6} }},
        {C::REV, { {"00", 21, 14, "VALUE", "LABEL_LITE", "value", true, 0}, {"", 23, 14, "SLIDER", "SLIDER", "slider", false, 6} }}
    };
}

inline std::unordered_map<CursorId, NavNode<CursorId>> GetWavsynthNavMap() {
    using C = CursorId;
    return {
        // Header
        {C::TYPE,     {C::NONE, C::NAME, C::NONE, C::CMD_LOAD}},
        {C::CMD_LOAD, {C::NONE, C::NAME, C::TYPE, C::CMD_SAVE}},
        {C::CMD_SAVE, {C::NONE, C::NAME, C::CMD_LOAD, C::NONE}},
        {C::NAME,     {C::TYPE, C::TRANSP, C::NONE, C::NONE}},
        {C::TRANSP,   {C::NAME, C::SHAPE, C::NONE, C::TBL_TIC}},
        {C::TBL_TIC,  {C::NAME, C::SHAPE, C::TRANSP, C::EQ}},
        {C::EQ,       {C::NAME, C::SHAPE, C::TBL_TIC, C::NONE}},

        // Left Column (Rows 6, 8-14)
        {C::SHAPE,    {C::TRANSP, C::WAV_SIZE, C::NONE, C::AMP}},
        {C::WAV_SIZE, {C::SHAPE, C::WAV_MULT, C::NONE, C::AMP}},
        {C::WAV_MULT, {C::WAV_SIZE, C::WAV_WARP, C::NONE, C::LIM}},
        {C::WAV_WARP, {C::WAV_MULT, C::WAV_SCAN, C::NONE, C::PAN}},
        {C::WAV_SCAN, {C::WAV_WARP, C::FILTER, C::NONE, C::DRY}},
        {C::FILTER,   {C::WAV_SCAN, C::CUTOFF, C::NONE, C::CHO}},
        {C::CUTOFF,   {C::FILTER, C::RES, C::NONE, C::DEL}},
        {C::RES,      {C::CUTOFF, C::NONE, C::NONE, C::REV}},

        // Right Column (Rows 8-14)
        {C::AMP, {C::TRANSP, C::LIM, C::WAV_SIZE, C::NONE}},
        {C::LIM, {C::AMP, C::PAN, C::WAV_MULT, C::NONE}},
        {C::PAN, {C::LIM, C::DRY, C::WAV_WARP, C::NONE}},
        {C::DRY, {C::PAN, C::CHO, C::WAV_SCAN, C::NONE}},
        {C::CHO, {C::DRY, C::DEL, C::FILTER, C::NONE}},
        {C::DEL, {C::CHO, C::REV, C::CUTOFF, C::NONE}},
        {C::REV, {C::DEL, C::NONE, C::RES, C::NONE}}
    };
}

} // namespace instrument
} // namespace ui
} // namespace m8
