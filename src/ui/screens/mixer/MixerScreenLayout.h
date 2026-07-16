#pragma once
#include "../../ui_types.h"
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>

namespace m8 {
namespace ui {
namespace mixer {

enum class CursorId : uint8_t {
    NONE = 0,
    OUT_VOL,
    TRK_VOL_0, TRK_VOL_1, TRK_VOL_2, TRK_VOL_3, TRK_VOL_4, TRK_VOL_5, TRK_VOL_6, TRK_VOL_7,
    MST_CHO, MST_DEL, MST_REV,
    IN_VOL, IN_CHO, IN_DEL, IN_REV,
    USB_VOL, USB_CHO, USB_DEL, USB_REV,
    MIX_VOL, LIM_VAL, DJF_FREQ, DJF_RES, DJF_TYP,
};

// TRK_VOL_0..TRK_VOL_7 are contiguous in the enum, so the track index can be
// recovered by simple subtraction instead of parsing a string suffix.
inline int TrackIndexOf(CursorId id) {
    return static_cast<int>(id) - static_cast<int>(CursorId::TRK_VOL_0);
}
inline CursorId TrackVolCursor(int track) {
    return static_cast<CursorId>(static_cast<int>(CursorId::TRK_VOL_0) + track);
}
inline bool IsTrackVolCursor(CursorId id) {
    return id >= CursorId::TRK_VOL_0 && id <= CursorId::TRK_VOL_7;
}

inline std::vector<UI_GridCell> GetMixerStaticText() {
    return {
        {"MIXER", 0, 0, "TITLE", "", "static", false, 0},
        {"OUTPUT VOL", 0, 3, "LABEL_LITE", "", "static", false, 0},

        // Master Sends
        {"CH", 0, 20, "LABEL_DIM", "", "static", false, 0},
        {"DE", 4, 20, "LABEL_DIM", "", "static", false, 0},
        {"RE", 8, 20, "LABEL_DIM", "", "static", false, 0},

        // Inputs
        {"--", 16, 19, "LABEL_DIM", "", "static", false, 0},
        {"INPUT", 12, 20, "LABEL_DIM", "", "static", false, 0},
        {"USB", 18, 20, "LABEL_DIM", "", "static", false, 0},
        {"CH", 9, 21, "LABEL_DIM", "", "static", false, 0},
        {"DE", 9, 22, "LABEL_DIM", "", "static", false, 0},
        {"RE", 9, 23, "LABEL_DIM", "", "static", false, 0},

        // Master FX
        {"EQ", 27, 18, "LABEL_LITE", "", "static", false, 0},
        {"MIX", 23, 19, "LABEL_DIM", "", "static", false, 0},
        {"LIM", 23, 20, "LABEL_DIM", "", "static", false, 0},
        {"DJF", 23, 21, "LABEL_DIM", "", "static", false, 0},
        {"RES", 23, 22, "LABEL_DIM", "", "static", false, 0},
        {"TYP", 23, 23, "LABEL_DIM", "", "static", false, 0}
    };
}

inline std::unordered_map<CursorId, std::vector<UI_GridCell>> GetMixerInteractiveFields() {
    using C = CursorId;
    std::unordered_map<CursorId, std::vector<UI_GridCell>> fields = {
        {C::OUT_VOL, { {"00", 12, 3, "VALUE", "LABEL_LITE", "value", true, 0} }},

        {C::MST_CHO, { {"E0", 0, 19, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::MST_DEL, { {"E0", 4, 19, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::MST_REV, { {"E0", 8, 19, "VALUE", "LABEL_LITE", "value", true, 0} }},

        {C::IN_VOL,  { {"00", 12, 19, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::IN_CHO,  { {"00", 12, 21, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::IN_DEL,  { {"00", 12, 22, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::IN_REV,  { {"00", 12, 23, "VALUE", "LABEL_LITE", "value", true, 0} }},

        {C::USB_VOL, { {"00", 18, 19, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::USB_CHO, { {"00", 18, 21, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::USB_DEL, { {"00", 18, 22, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::USB_REV, { {"00", 18, 23, "VALUE", "LABEL_LITE", "value", true, 0} }},

        {C::MIX_VOL, { {"DC", 27, 19, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::LIM_VAL, { {"40", 27, 20, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::DJF_FREQ, { {"80", 27, 21, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::DJF_RES, { {"80", 27, 22, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::DJF_TYP, { {"00", 27, 23, "VALUE", "LABEL_LITE", "value", true, 0} }}
    };

    // Tracks 1-8 (0-7 indexed)
    for (int i = 0; i < 8; i++) {
        fields[TrackVolCursor(i)] = { {"00", i * 3, 18, "VALUE", "LABEL_LITE", "value", true, 0} };
    }
    return fields;
}

inline std::unordered_map<CursorId, NavNode<CursorId>> GetMixerNavMap() {
    using C = CursorId;
    return {
        {C::OUT_VOL,   {/*U*/C::NONE,       /*D*/C::TRK_VOL_4, /*L*/C::NONE,      /*R*/C::NONE}},
        {C::TRK_VOL_0, {/*U*/C::OUT_VOL,    /*D*/C::MST_CHO,   /*L*/C::NONE,      /*R*/C::TRK_VOL_1}},
        {C::TRK_VOL_1, {/*U*/C::OUT_VOL,    /*D*/C::MST_DEL,   /*L*/C::TRK_VOL_0, /*R*/C::TRK_VOL_2}},
        {C::TRK_VOL_2, {/*U*/C::OUT_VOL,    /*D*/C::MST_REV,   /*L*/C::TRK_VOL_1, /*R*/C::TRK_VOL_3}},
        {C::TRK_VOL_3, {/*U*/C::OUT_VOL,    /*D*/C::IN_VOL,    /*L*/C::TRK_VOL_2, /*R*/C::TRK_VOL_4}},
        {C::TRK_VOL_4, {/*U*/C::OUT_VOL,    /*D*/C::IN_VOL,    /*L*/C::TRK_VOL_3, /*R*/C::TRK_VOL_5}},
        {C::TRK_VOL_5, {/*U*/C::OUT_VOL,    /*D*/C::USB_VOL,   /*L*/C::TRK_VOL_4, /*R*/C::TRK_VOL_6}},
        {C::TRK_VOL_6, {/*U*/C::OUT_VOL,    /*D*/C::USB_VOL,   /*L*/C::TRK_VOL_5, /*R*/C::TRK_VOL_7}},
        {C::TRK_VOL_7, {/*U*/C::OUT_VOL,    /*D*/C::MIX_VOL,   /*L*/C::TRK_VOL_6, /*R*/C::NONE}},

        {C::MST_CHO,   {/*U*/C::TRK_VOL_0,  /*D*/C::NONE,      /*L*/C::NONE,      /*R*/C::MST_DEL}},
        {C::MST_DEL,   {/*U*/C::TRK_VOL_1,  /*D*/C::NONE,      /*L*/C::MST_CHO,   /*R*/C::MST_REV}},
        {C::MST_REV,   {/*U*/C::TRK_VOL_2,  /*D*/C::NONE,      /*L*/C::MST_DEL,   /*R*/C::IN_VOL}},

        {C::IN_VOL,    {/*U*/C::TRK_VOL_4,  /*D*/C::IN_CHO,    /*L*/C::MST_REV,   /*R*/C::USB_VOL}},
        {C::IN_CHO,    {/*U*/C::IN_VOL,     /*D*/C::IN_DEL,    /*L*/C::MST_REV,   /*R*/C::USB_CHO}},
        {C::IN_DEL,    {/*U*/C::IN_CHO,     /*D*/C::IN_REV,    /*L*/C::MST_REV,   /*R*/C::USB_DEL}},
        {C::IN_REV,    {/*U*/C::IN_DEL,     /*D*/C::NONE,      /*L*/C::MST_REV,   /*R*/C::USB_REV}},

        {C::USB_VOL,   {/*U*/C::TRK_VOL_6,  /*D*/C::USB_CHO,   /*L*/C::IN_VOL,    /*R*/C::MIX_VOL}},
        {C::USB_CHO,   {/*U*/C::USB_VOL,    /*D*/C::USB_DEL,   /*L*/C::IN_CHO,    /*R*/C::LIM_VAL}},
        {C::USB_DEL,   {/*U*/C::USB_CHO,    /*D*/C::USB_REV,   /*L*/C::IN_DEL,    /*R*/C::DJF_FREQ}},
        {C::USB_REV,   {/*U*/C::USB_DEL,    /*D*/C::NONE,      /*L*/C::IN_REV,    /*R*/C::DJF_RES}},

        {C::MIX_VOL,   {/*U*/C::TRK_VOL_7,  /*D*/C::LIM_VAL,   /*L*/C::USB_VOL,   /*R*/C::NONE}},
        {C::LIM_VAL,   {/*U*/C::MIX_VOL,    /*D*/C::DJF_FREQ,  /*L*/C::USB_CHO,   /*R*/C::NONE}},
        {C::DJF_FREQ,  {/*U*/C::LIM_VAL,    /*D*/C::DJF_RES,   /*L*/C::USB_DEL,   /*R*/C::NONE}},
        {C::DJF_RES,   {/*U*/C::DJF_FREQ,   /*D*/C::DJF_TYP,   /*L*/C::USB_REV,   /*R*/C::NONE}},
        {C::DJF_TYP,   {/*U*/C::DJF_RES,    /*D*/C::NONE,      /*L*/C::USB_REV,   /*R*/C::NONE}}
    };
}

} // namespace mixer
} // namespace ui
} // namespace m8
