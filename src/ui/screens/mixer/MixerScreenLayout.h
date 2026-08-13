#pragma once
#include "../../ui_types.h"
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>

// MIXER layout — rebuilt per MIXER_SPEC.md.
//
// Gone from the old version: INPUT and USB (soft-mate has no inputs), and the
// invented DJF "RES"/"TYP" rows. The master column is MIX / LIM / DJF / OTT,
// which is what the device actually shows.
//
// The bars are not in these tables. Meters are drawn cell by cell from live
// levels (MixerScreen.cpp), since their height and colour change every frame;
// the tables carry the labels and the hex readouts only.

namespace m8 {
namespace ui {
namespace mixer {

enum class CursorId : uint8_t {
    NONE = 0,
    SPEAKER_VOL,
    TRK_VOL_0, TRK_VOL_1, TRK_VOL_2, TRK_VOL_3, TRK_VOL_4, TRK_VOL_5, TRK_VOL_6, TRK_VOL_7,
    MST_CHO, MST_DEL, MST_REV,
    MIX_VOL, LIM_VAL, DJF, OTT,
};

// TRK_VOL_0..TRK_VOL_7 are contiguous, so the track index is subtraction rather
// than string parsing.
inline int TrackIndexOf(CursorId id) {
    return static_cast<int>(id) - static_cast<int>(CursorId::TRK_VOL_0);
}
inline CursorId TrackVolCursor(int track) {
    return static_cast<CursorId>(static_cast<int>(CursorId::TRK_VOL_0) + track);
}
inline bool IsTrackVolCursor(CursorId id) {
    return id >= CursorId::TRK_VOL_0 && id <= CursorId::TRK_VOL_7;
}

// ---- Geometry ---------------------------------------------------------------
// Track strip i occupies columns kTrackCol(i) and +1 (left and right meters),
// with the hex value written at kTrackCol(i). Four columns apart leaves a clear
// gap between neighbouring tracks.
inline constexpr int kTrackCol(int i)   { return i * 4; }
inline constexpr int kMeterTop          = 6;    // topmost meter cell row
inline constexpr int kMeterBottom       = 16;   // bottom-most meter cell row
inline constexpr int kTrackValueRow     = 17;

// Send returns, below the tracks.
inline constexpr int kSendCol(int i)    { return i * 4; }
inline constexpr int kSendMeterTop      = 20;
inline constexpr int kSendMeterBottom   = 23;
inline constexpr int kSendValueRow      = 24;
inline constexpr int kSendLabelRow      = 25;

// Master strip on the right.
inline constexpr int kMasterMeterCol    = 34;   // L at 34, R at 35
inline constexpr int kMasterLabelCol    = 30;
inline constexpr int kMasterValueCol    = 35;
inline constexpr int kMasterFirstRow    = 20;   // MIX, then LIM, DJF, OTT

inline std::vector<UI_GridCell> GetMixerStaticText() {
    return {
        {"MIXER", 0, 0, "TITLE", "", "static", false, 0},
        {"SPEAKER VOL", 0, 3, "LABEL_LITE", "", "static", false, 0},

        // Send return labels, under their meters.
        {"MX", kSendCol(0), kSendLabelRow, "LABEL_DIM", "", "static", false, 0},
        {"DE", kSendCol(1), kSendLabelRow, "LABEL_DIM", "", "static", false, 0},
        {"RE", kSendCol(2), kSendLabelRow, "LABEL_DIM", "", "static", false, 0},

        // Master strip. EQ is a label only: the EQ itself and its editor view
        // are not built, so it is deliberately not a cursor stop -- a control
        // that does nothing when you press it is worse than no control.
        {"EQ", kMasterMeterCol, kTrackValueRow, "LABEL_DIM", "", "static", false, 0},
        {"MIX", kMasterLabelCol, kMasterFirstRow + 0, "LABEL_DIM", "", "static", false, 0},
        {"LIM", kMasterLabelCol, kMasterFirstRow + 1, "LABEL_DIM", "", "static", false, 0},
        {"DJF", kMasterLabelCol, kMasterFirstRow + 2, "LABEL_DIM", "", "static", false, 0},
        {"OTT", kMasterLabelCol, kMasterFirstRow + 3, "LABEL_DIM", "", "static", false, 0},
    };
}

inline std::unordered_map<CursorId, std::vector<UI_GridCell>> GetMixerInteractiveFields() {
    using C = CursorId;
    std::unordered_map<CursorId, std::vector<UI_GridCell>> fields = {
        {C::SPEAKER_VOL, { {"FF", 13, 3, "VALUE", "LABEL_LITE", "value", true, 0} }},

        {C::MST_CHO, { {"E0", kSendCol(0), kSendValueRow, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::MST_DEL, { {"E0", kSendCol(1), kSendValueRow, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::MST_REV, { {"E0", kSendCol(2), kSendValueRow, "VALUE", "LABEL_LITE", "value", true, 0} }},

        {C::MIX_VOL, { {"E0", kMasterValueCol, kMasterFirstRow + 0, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::LIM_VAL, { {"40", kMasterValueCol, kMasterFirstRow + 1, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::DJF,     { {"80", kMasterValueCol, kMasterFirstRow + 2, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::OTT,     { {"80", kMasterValueCol, kMasterFirstRow + 3, "VALUE", "LABEL_LITE", "value", true, 0} }},
    };

    for (int i = 0; i < 8; i++) {
        fields[TrackVolCursor(i)] =
            { {"00", kTrackCol(i), kTrackValueRow, "VALUE", "LABEL_LITE", "value", true, 0} };
    }
    return fields;
}

inline std::unordered_map<CursorId, NavNode<CursorId>> GetMixerNavMap() {
    using C = CursorId;
    return {
        {C::SPEAKER_VOL, {/*U*/C::NONE,       /*D*/C::TRK_VOL_0, /*L*/C::NONE,      /*R*/C::NONE}},

        // Tracks 0-2 drop onto the send returns beneath them; 3-7 have nothing
        // below, so they fall through to the master strip.
        {C::TRK_VOL_0, {/*U*/C::SPEAKER_VOL, /*D*/C::MST_CHO,   /*L*/C::NONE,      /*R*/C::TRK_VOL_1}},
        {C::TRK_VOL_1, {/*U*/C::SPEAKER_VOL, /*D*/C::MST_DEL,   /*L*/C::TRK_VOL_0, /*R*/C::TRK_VOL_2}},
        {C::TRK_VOL_2, {/*U*/C::SPEAKER_VOL, /*D*/C::MST_REV,   /*L*/C::TRK_VOL_1, /*R*/C::TRK_VOL_3}},
        {C::TRK_VOL_3, {/*U*/C::SPEAKER_VOL, /*D*/C::MIX_VOL,   /*L*/C::TRK_VOL_2, /*R*/C::TRK_VOL_4}},
        {C::TRK_VOL_4, {/*U*/C::SPEAKER_VOL, /*D*/C::MIX_VOL,   /*L*/C::TRK_VOL_3, /*R*/C::TRK_VOL_5}},
        {C::TRK_VOL_5, {/*U*/C::SPEAKER_VOL, /*D*/C::MIX_VOL,   /*L*/C::TRK_VOL_4, /*R*/C::TRK_VOL_6}},
        {C::TRK_VOL_6, {/*U*/C::SPEAKER_VOL, /*D*/C::MIX_VOL,   /*L*/C::TRK_VOL_5, /*R*/C::TRK_VOL_7}},
        {C::TRK_VOL_7, {/*U*/C::SPEAKER_VOL, /*D*/C::MIX_VOL,   /*L*/C::TRK_VOL_6, /*R*/C::MIX_VOL}},

        {C::MST_CHO,   {/*U*/C::TRK_VOL_0,   /*D*/C::NONE,      /*L*/C::NONE,      /*R*/C::MST_DEL}},
        {C::MST_DEL,   {/*U*/C::TRK_VOL_1,   /*D*/C::NONE,      /*L*/C::MST_CHO,   /*R*/C::MST_REV}},
        {C::MST_REV,   {/*U*/C::TRK_VOL_2,   /*D*/C::NONE,      /*L*/C::MST_DEL,   /*R*/C::MIX_VOL}},

        {C::MIX_VOL,   {/*U*/C::TRK_VOL_7,   /*D*/C::LIM_VAL,   /*L*/C::MST_REV,   /*R*/C::NONE}},
        {C::LIM_VAL,   {/*U*/C::MIX_VOL,     /*D*/C::DJF,       /*L*/C::MST_REV,   /*R*/C::NONE}},
        {C::DJF,       {/*U*/C::LIM_VAL,     /*D*/C::OTT,       /*L*/C::MST_REV,   /*R*/C::NONE}},
        {C::OTT,       {/*U*/C::DJF,         /*D*/C::NONE,      /*L*/C::MST_REV,   /*R*/C::NONE}},
    };
}

} // namespace mixer
} // namespace ui
} // namespace m8
