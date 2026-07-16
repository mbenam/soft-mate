#pragma once
#include "../../ui_types.h"
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>

namespace m8 {
namespace ui {
namespace project {

enum class CursorId : uint8_t {
    NONE = 0,
    TEMPO_INT, TEMPO_DEC, TEMPO_NUDGE, TRANSPOSE, GROOVE, SCALE, LIVE_QUANTIZE,
    MIDI_SETTINGS, MIDI_MAPPINGS, NAME, PROJ_LOAD, PROJ_SAVE, PROJ_NEW,
    EXPORT_RENDER, EXPORT_BUNDLE, CLEAR_PHRASES, CLEAR_INST, INST_POOL,
    TIME_STATS, SYSTEM_SETTINGS, SAMPLE_ROOT,
};

inline std::vector<UI_GridCell> GetProjectStaticText() {
    return {
        {"PROJECT", 0, 0, "TITLE", "", "static", false, 0},
    };
}

inline std::vector<UI_GridCell> GetProjectDynamicTextDefaults() {
    return {
    };
}

inline std::unordered_map<CursorId, std::vector<UI_GridCell>> GetProjectInteractiveFields() {
    using C = CursorId;
    return {
        {C::TEMPO_INT, {
            {"TEMPO", 0, 2, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"140", 14, 2, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::TEMPO_DEC, {
            {".", 17, 2, "LABEL_DIM", "LABEL_DIM", "static", false, 0},
            {"00", 18, 2, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::TEMPO_NUDGE, {
            {"< >", 21, 2, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::TRANSPOSE, {
            {"TRANSPOSE", 0, 3, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 14, 3, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::GROOVE, {
            {"GROOVE", 0, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 14, 4, "VALUE", "LABEL_LITE", "value", true, 0},
            {"DEFAULT", 16, 4, "ACCENT", "LABEL_LITE", "accent", false, 0}
        }},
        {C::SCALE, {
            {"SCALE", 0, 5, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 14, 5, "VALUE", "LABEL_LITE", "value", true, 0},
            {"C CHROMATIC", 16, 5, "ACCENT", "LABEL_LITE", "accent", false, 0}
        }},
        {C::LIVE_QUANTIZE, {
            {"LIVE QUANTIZE", 0, 6, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 14, 6, "VALUE", "LABEL_LITE", "value", true, 0},
            {"CHAIN LEN", 16, 6, "ACCENT", "LABEL_LITE", "accent", false, 0}
        }},
        {C::MIDI_SETTINGS, {
            {"MIDI", 0, 8, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"SETTINGS", 14, 8, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::MIDI_MAPPINGS, {
            {"MAPPINGS", 23, 8, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::NAME, {
            {"NAME", 0, 10, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"DEMO2-------", 14, 10, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::PROJ_LOAD, {
            {"PROJECT", 0, 11, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"LOAD", 14, 11, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::PROJ_SAVE, {
            {"SAVE", 19, 11, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::PROJ_NEW, {
            {"NEW", 24, 11, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::EXPORT_RENDER, {
            {"EXPORT/SHARE", 0, 12, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"RENDER", 14, 12, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::EXPORT_BUNDLE, {
            {"BUNDLE", 21, 12, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::CLEAR_PHRASES, {
            {"CLEAR UNUSED", 0, 13, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"PHRASES", 14, 13, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::CLEAR_INST, {
            {"INST/TBL", 22, 13, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::INST_POOL, {
            {"INST. POOL", 0, 14, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"VIEW INST.POOL", 14, 14, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::TIME_STATS, {
            {"TIME STATS", 0, 16, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"VIEW TIME STATS", 14, 16, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::SYSTEM_SETTINGS, {
            {"SYSTEM", 0, 17, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"SETTINGS", 14, 17, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::SAMPLE_ROOT, {
            {"SAMPLE ROOT", 0, 18, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"Samples", 14, 18, "VALUE", "LABEL_LITE", "value", true, 0}
        }}
    };
}

inline std::unordered_map<CursorId, NavNode<CursorId>> GetProjectNavMap() {
    using C = CursorId;
    return {
        {C::TEMPO_INT,       {/*U*/C::NONE,            /*D*/C::TRANSPOSE,      /*L*/C::NONE,           /*R*/C::TEMPO_DEC}},
        {C::TEMPO_DEC,       {/*U*/C::NONE,            /*D*/C::TRANSPOSE,      /*L*/C::TEMPO_INT,      /*R*/C::TEMPO_NUDGE}},
        {C::TEMPO_NUDGE,     {/*U*/C::NONE,            /*D*/C::TRANSPOSE,      /*L*/C::TEMPO_DEC,      /*R*/C::NONE}},
        {C::TRANSPOSE,       {/*U*/C::TEMPO_INT,       /*D*/C::GROOVE,         /*L*/C::NONE,           /*R*/C::NONE}},
        {C::GROOVE,          {/*U*/C::TRANSPOSE,       /*D*/C::SCALE,          /*L*/C::NONE,           /*R*/C::NONE}},
        {C::SCALE,           {/*U*/C::GROOVE,          /*D*/C::LIVE_QUANTIZE,  /*L*/C::NONE,           /*R*/C::NONE}},
        {C::LIVE_QUANTIZE,   {/*U*/C::SCALE,           /*D*/C::MIDI_SETTINGS,  /*L*/C::NONE,           /*R*/C::NONE}},
        {C::MIDI_SETTINGS,   {/*U*/C::LIVE_QUANTIZE,   /*D*/C::NAME,           /*L*/C::NONE,           /*R*/C::MIDI_MAPPINGS}},
        {C::MIDI_MAPPINGS,   {/*U*/C::LIVE_QUANTIZE,   /*D*/C::NAME,           /*L*/C::MIDI_SETTINGS,  /*R*/C::NONE}},
        {C::NAME,            {/*U*/C::MIDI_SETTINGS,   /*D*/C::PROJ_LOAD,      /*L*/C::NONE,           /*R*/C::NONE}},
        {C::PROJ_LOAD,       {/*U*/C::NAME,            /*D*/C::EXPORT_RENDER,  /*L*/C::NONE,           /*R*/C::PROJ_SAVE}},
        {C::PROJ_SAVE,       {/*U*/C::NAME,            /*D*/C::EXPORT_BUNDLE,  /*L*/C::PROJ_LOAD,      /*R*/C::PROJ_NEW}},
        {C::PROJ_NEW,        {/*U*/C::NAME,            /*D*/C::EXPORT_BUNDLE,  /*L*/C::PROJ_SAVE,      /*R*/C::NONE}},
        {C::EXPORT_RENDER,   {/*U*/C::PROJ_LOAD,       /*D*/C::CLEAR_PHRASES,  /*L*/C::NONE,           /*R*/C::EXPORT_BUNDLE}},
        {C::EXPORT_BUNDLE,   {/*U*/C::PROJ_SAVE,       /*D*/C::CLEAR_INST,     /*L*/C::EXPORT_RENDER,  /*R*/C::NONE}},
        {C::CLEAR_PHRASES,   {/*U*/C::EXPORT_RENDER,   /*D*/C::INST_POOL,      /*L*/C::NONE,           /*R*/C::CLEAR_INST}},
        {C::CLEAR_INST,      {/*U*/C::EXPORT_BUNDLE,   /*D*/C::INST_POOL,      /*L*/C::CLEAR_PHRASES,  /*R*/C::NONE}},
        {C::INST_POOL,       {/*U*/C::CLEAR_PHRASES,   /*D*/C::TIME_STATS,     /*L*/C::NONE,           /*R*/C::NONE}},
        {C::TIME_STATS,      {/*U*/C::INST_POOL,       /*D*/C::SYSTEM_SETTINGS,/*L*/C::NONE,           /*R*/C::NONE}},
        {C::SYSTEM_SETTINGS, {/*U*/C::TIME_STATS,      /*D*/C::SAMPLE_ROOT,    /*L*/C::NONE,           /*R*/C::NONE}},
        {C::SAMPLE_ROOT,     {/*U*/C::SYSTEM_SETTINGS, /*D*/C::NONE,           /*L*/C::NONE,           /*R*/C::NONE}}
    };
}

} // namespace project
} // namespace ui
} // namespace m8
