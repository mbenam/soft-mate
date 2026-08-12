#pragma once
#include "../../ui_types.h"
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>

namespace m8 {
namespace ui {
namespace scale {

enum class CursorId : uint8_t {
    NONE = 0,
    KEY, TUNE_INT, TUNE_DEC, NAME, CMD_LOAD, CMD_SAVE,
    NOTE_EN_0, NOTE_EN_1, NOTE_EN_2, NOTE_EN_3, NOTE_EN_4, NOTE_EN_5,
    NOTE_EN_6, NOTE_EN_7, NOTE_EN_8, NOTE_EN_9, NOTE_EN_10, NOTE_EN_11,
    NOTE_OFFSET_0, NOTE_OFFSET_1, NOTE_OFFSET_2, NOTE_OFFSET_3, NOTE_OFFSET_4, NOTE_OFFSET_5,
    NOTE_OFFSET_6, NOTE_OFFSET_7, NOTE_OFFSET_8, NOTE_OFFSET_9, NOTE_OFFSET_10, NOTE_OFFSET_11,
};

// NOTE_EN_0..11 and NOTE_OFFSET_0..11 are each contiguous in the enum, so the
// note index can be recovered by subtraction instead of parsing a string suffix.
inline int NoteEnIndexOf(CursorId id) {
    return static_cast<int>(id) - static_cast<int>(CursorId::NOTE_EN_0);
}
inline CursorId NoteEnCursor(int note) {
    return static_cast<CursorId>(static_cast<int>(CursorId::NOTE_EN_0) + note);
}
inline bool IsNoteEnCursor(CursorId id) {
    return id >= CursorId::NOTE_EN_0 && id <= CursorId::NOTE_EN_11;
}
inline int NoteOffsetIndexOf(CursorId id) {
    return static_cast<int>(id) - static_cast<int>(CursorId::NOTE_OFFSET_0);
}
inline CursorId NoteOffsetCursor(int note) {
    return static_cast<CursorId>(static_cast<int>(CursorId::NOTE_OFFSET_0) + note);
}
inline bool IsNoteOffsetCursor(CursorId id) {
    return id >= CursorId::NOTE_OFFSET_0 && id <= CursorId::NOTE_OFFSET_11;
}

inline std::vector<UI_GridCell> GetScaleStaticText() {
    return {
        {"SCALE", 0, 0, "TITLE", "", "static", false, 0},
        {"EN OFFSET", 6, 3, "LABEL_DIM", "", "static", false, 0},
    };
}

inline std::vector<UI_GridCell> GetScaleDynamicTextDefaults() {
    return {
        {"00", 6, 0, "TITLE", "", "dynamic_text", false, 0}
    };
}

inline std::unordered_map<CursorId, std::vector<UI_GridCell>> GetScaleInteractiveFields() {
    using C = CursorId;
    std::unordered_map<CursorId, std::vector<UI_GridCell>> fields = {
        {C::KEY, { {"KEY", 0, 2, "LABEL_LITE", "LABEL_LITE", "label", false, 0}, {"C", 7, 2, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::TUNE_INT, {
            {"TUNE", 0, 16, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"440",  7, 16, "VALUE",     "LABEL_LITE", "value", true,  0},
            {".",   10, 16, "LABEL_DIM", "LABEL_DIM",  "static", false, 0}
        }},
        {C::TUNE_DEC, {
            {"00",  11, 16, "VALUE",     "LABEL_LITE", "value", true,  0}
        }},
        {C::NAME, { {"NAME", 0, 17, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"CHROMATIC-------", 7, 17, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::CMD_LOAD, { {"LOAD", 7, 18, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {C::CMD_SAVE, { {"SAVE", 12, 18, "VALUE", "LABEL_LITE", "value", true, 0} }}
    };

    // Procedurally generate the 12 rows of notes
    for (int i = 0; i < 12; i++) {
        int r = i + 4;
        fields[NoteEnCursor(i)] = { {"ON", 6, r, "VALUE", "LABEL_LITE", "value", true, 0} };
        fields[NoteOffsetCursor(i)] = { {"00.00", 10, r, "VALUE", "LABEL_LITE", "value", true, 0} };
    }

    return fields;
}

inline std::unordered_map<CursorId, NavNode<CursorId>> GetScaleNavMap() {
    using C = CursorId;
    std::unordered_map<CursorId, NavNode<CursorId>> map = {
        {C::KEY,      {/*U*/C::NONE,            /*D*/C::NOTE_EN_0,  /*L*/C::NONE,       /*R*/C::NONE}},
        {C::TUNE_INT, {/*U*/C::NOTE_EN_11,      /*D*/C::NAME,       /*L*/C::NONE,       /*R*/C::TUNE_DEC}},
        {C::TUNE_DEC, {/*U*/C::NOTE_OFFSET_11,  /*D*/C::NAME,       /*L*/C::TUNE_INT,   /*R*/C::NONE}},
        {C::NAME,     {/*U*/C::TUNE_INT,        /*D*/C::CMD_LOAD,   /*L*/C::NONE,       /*R*/C::NONE}},
        {C::CMD_LOAD, {/*U*/C::NAME,            /*D*/C::NONE,       /*L*/C::NONE,       /*R*/C::CMD_SAVE}},
        {C::CMD_SAVE, {/*U*/C::NAME,            /*D*/C::NONE,       /*L*/C::CMD_LOAD,   /*R*/C::NONE}}
    };

    // Procedurally link the 12 grid rows
    for (int i = 0; i < 12; i++) {
        C en = NoteEnCursor(i);
        C off = NoteOffsetCursor(i);

        C up_en = (i == 0) ? C::KEY : NoteEnCursor(i - 1);
        C up_off = (i == 0) ? C::KEY : NoteOffsetCursor(i - 1);

        C down_en = (i == 11) ? C::TUNE_INT : NoteEnCursor(i + 1);
        C down_off = (i == 11) ? C::TUNE_DEC : NoteOffsetCursor(i + 1);

        map[en] = {up_en, down_en, C::NONE, off};
        map[off] = {up_off, down_off, en, C::NONE};
    }

    return map;
}

} // namespace scale
} // namespace ui
} // namespace m8
