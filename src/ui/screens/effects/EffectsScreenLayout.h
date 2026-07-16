#pragma once
#include "../../ui_types.h"
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>

namespace m8 {
namespace ui {
namespace effects {

enum class CursorId : uint8_t {
    NONE = 0,
    CHO_EQ, CHO_MOD_DEP, CHO_MOD_FRQ, CHO_WID, CHO_REV,
    DEL_EQ, DEL_TIME_L, DEL_TIME_R, DEL_FBK, DEL_WID, DEL_REV,
    REV_EQ, REV_SIZE, REV_DEC, REV_MOD_DEP, REV_MOD_FRQ, REV_WID,
};

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

inline std::unordered_map<CursorId, std::vector<UI_GridCell>> GetEffectsInteractiveFields() {
    return {
        // CHORUS
        {CursorId::CHO_EQ, { {"INPUT EQ", 7, 2, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"EQ", 21, 2, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {CursorId::CHO_MOD_DEP, { {"MOD DEPTH:FRQ", 7, 3, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"50", 21, 3, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {CursorId::CHO_MOD_FRQ, { {"80", 24, 3, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {CursorId::CHO_WID, { {"STEREO WIDTH", 7, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"FF", 21, 4, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {CursorId::CHO_REV, { {"REVERB SEND", 7, 5, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 21, 5, "VALUE", "LABEL_LITE", "value", true, 0} }},

        // DELAY
        {CursorId::DEL_EQ, { {"INPUT EQ", 7, 8, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"EQ", 21, 8, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {CursorId::DEL_TIME_L, { {"TIME L:R", 7, 9, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"30", 21, 9, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {CursorId::DEL_TIME_R, { {"30", 24, 9, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {CursorId::DEL_FBK, { {"FEEDBACK", 7, 10, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"80", 21, 10, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {CursorId::DEL_WID, { {"STEREO WIDTH", 7, 11, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"FF", 21, 11, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {CursorId::DEL_REV, { {"REVERB SEND", 7, 12, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"00", 21, 12, "VALUE", "LABEL_LITE", "value", true, 0} }},

        // REVERB
        {CursorId::REV_EQ, { {"INPUT EQ", 7, 15, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"EQ", 21, 15, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {CursorId::REV_SIZE, { {"ROOM SIZE", 7, 16, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"FF", 21, 16, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {CursorId::REV_DEC, { {"DECAY", 7, 17, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"C0", 21, 17, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {CursorId::REV_MOD_DEP, { {"MOD DEPTH:FRQ", 7, 18, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"20", 21, 18, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {CursorId::REV_MOD_FRQ, { {"FF", 24, 18, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {CursorId::REV_WID, { {"STEREO WIDTH", 7, 19, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"FF", 21, 19, "VALUE", "LABEL_LITE", "value", true, 0} }},
    };
}

inline std::unordered_map<CursorId, NavNode<CursorId>> GetEffectsNavMap() {
    using C = CursorId;
    return {
        // CHORUS
        {C::CHO_EQ,      {/*U*/C::NONE,        /*D*/C::CHO_MOD_DEP, /*L*/C::NONE,        /*R*/C::NONE}},
        {C::CHO_MOD_DEP, {/*U*/C::CHO_EQ,      /*D*/C::CHO_WID,     /*L*/C::NONE,        /*R*/C::CHO_MOD_FRQ}},
        {C::CHO_MOD_FRQ, {/*U*/C::CHO_EQ,      /*D*/C::CHO_WID,     /*L*/C::CHO_MOD_DEP, /*R*/C::NONE}},
        {C::CHO_WID,     {/*U*/C::CHO_MOD_DEP, /*D*/C::CHO_REV,     /*L*/C::NONE,        /*R*/C::NONE}},
        {C::CHO_REV,     {/*U*/C::CHO_WID,     /*D*/C::DEL_EQ,      /*L*/C::NONE,        /*R*/C::NONE}},

        // DELAY
        {C::DEL_EQ,      {/*U*/C::CHO_REV,     /*D*/C::DEL_TIME_L,  /*L*/C::NONE,        /*R*/C::NONE}},
        {C::DEL_TIME_L,  {/*U*/C::DEL_EQ,      /*D*/C::DEL_FBK,     /*L*/C::NONE,        /*R*/C::DEL_TIME_R}},
        {C::DEL_TIME_R,  {/*U*/C::DEL_EQ,      /*D*/C::DEL_FBK,     /*L*/C::DEL_TIME_L,  /*R*/C::NONE}},
        {C::DEL_FBK,     {/*U*/C::DEL_TIME_L,  /*D*/C::DEL_WID,     /*L*/C::NONE,        /*R*/C::NONE}},
        {C::DEL_WID,     {/*U*/C::DEL_FBK,     /*D*/C::DEL_REV,     /*L*/C::NONE,        /*R*/C::NONE}},
        {C::DEL_REV,     {/*U*/C::DEL_WID,     /*D*/C::REV_EQ,      /*L*/C::NONE,        /*R*/C::NONE}},

        // REVERB
        {C::REV_EQ,      {/*U*/C::DEL_REV,     /*D*/C::REV_SIZE,    /*L*/C::NONE,        /*R*/C::NONE}},
        {C::REV_SIZE,    {/*U*/C::REV_EQ,      /*D*/C::REV_DEC,     /*L*/C::NONE,        /*R*/C::NONE}},
        {C::REV_DEC,     {/*U*/C::REV_SIZE,    /*D*/C::REV_MOD_DEP, /*L*/C::NONE,        /*R*/C::NONE}},
        {C::REV_MOD_DEP, {/*U*/C::REV_DEC,     /*D*/C::REV_WID,     /*L*/C::NONE,        /*R*/C::REV_MOD_FRQ}},
        {C::REV_MOD_FRQ, {/*U*/C::REV_DEC,     /*D*/C::REV_WID,     /*L*/C::REV_MOD_DEP, /*R*/C::NONE}},
        {C::REV_WID,     {/*U*/C::REV_MOD_DEP, /*D*/C::NONE,        /*L*/C::NONE,        /*R*/C::NONE}}
    };
}

} // namespace effects
} // namespace ui
} // namespace m8
