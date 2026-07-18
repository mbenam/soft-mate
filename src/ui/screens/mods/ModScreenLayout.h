#pragma once
#include "../../ui_types.h"
#include "../../../engine/Engine.h"
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>

namespace m8 {
namespace ui {
namespace mods {

// Every field on this screen is parameterized by a quadrant (0-3, one of the
// instrument's 4 modulator slots) and, for MOD_P fields, also a param-slot
// (1-4, meaning depends on the modulator type -- see GetNumParamsForModType).
// The enum enumerates every (quadrant [, param-slot]) combination as a flat
// value rather than encoding two indices into a string.
enum class CursorId : uint8_t {
    NONE = 0,
    MOD_TYPE_0, MOD_TYPE_1, MOD_TYPE_2, MOD_TYPE_3,
    MOD_DEST_0, MOD_DEST_1, MOD_DEST_2, MOD_DEST_3,
    MOD_AMT_0, MOD_AMT_1, MOD_AMT_2, MOD_AMT_3,
    MOD_P1_0, MOD_P1_1, MOD_P1_2, MOD_P1_3,
    MOD_P2_0, MOD_P2_1, MOD_P2_2, MOD_P2_3,
    MOD_P3_0, MOD_P3_1, MOD_P3_2, MOD_P3_3,
    MOD_P4_0, MOD_P4_1, MOD_P4_2, MOD_P4_3,
};

inline bool IsTypeCursor(CursorId id) { return id >= CursorId::MOD_TYPE_0 && id <= CursorId::MOD_TYPE_3; }
inline CursorId TypeCursor(int q) { return static_cast<CursorId>(static_cast<int>(CursorId::MOD_TYPE_0) + q); }

inline bool IsDestCursor(CursorId id) { return id >= CursorId::MOD_DEST_0 && id <= CursorId::MOD_DEST_3; }
inline CursorId DestCursor(int q) { return static_cast<CursorId>(static_cast<int>(CursorId::MOD_DEST_0) + q); }

inline bool IsAmtCursor(CursorId id) { return id >= CursorId::MOD_AMT_0 && id <= CursorId::MOD_AMT_3; }
inline CursorId AmtCursor(int q) { return static_cast<CursorId>(static_cast<int>(CursorId::MOD_AMT_0) + q); }

inline bool IsParamCursor(CursorId id) { return id >= CursorId::MOD_P1_0 && id <= CursorId::MOD_P4_3; }
// pIdx in 1..4, q in 0..3 -- both baked into one contiguous block of 16 enumerators.
inline CursorId ParamCursor(int pIdx, int q) { return static_cast<CursorId>(static_cast<int>(CursorId::MOD_P1_0) + (pIdx - 1) * 4 + q); }
inline int ParamSlotOf(CursorId id) { return 1 + (static_cast<int>(id) - static_cast<int>(CursorId::MOD_P1_0)) / 4; }

// Quadrant recovery across any of the 4 field kinds on this screen (replaces
// the old `fieldId.back() - '0'` single-digit trick).
inline int QuadrantOf(CursorId id) {
    if (IsTypeCursor(id)) return static_cast<int>(id) - static_cast<int>(CursorId::MOD_TYPE_0);
    if (IsDestCursor(id)) return static_cast<int>(id) - static_cast<int>(CursorId::MOD_DEST_0);
    if (IsAmtCursor(id)) return static_cast<int>(id) - static_cast<int>(CursorId::MOD_AMT_0);
    if (IsParamCursor(id)) return (static_cast<int>(id) - static_cast<int>(CursorId::MOD_P1_0)) % 4;
    return 0;
}

// rowIdx: 0=TYPE, 1=DEST, 2=AMT, >=3 => MOD_P(rowIdx-2)
inline CursorId NodeAt(size_t rowIdx, int q) {
    if (rowIdx == 0) return TypeCursor(q);
    if (rowIdx == 1) return DestCursor(q);
    if (rowIdx == 2) return AmtCursor(q);
    return ParamCursor(static_cast<int>(rowIdx - 2), q);
}

inline int GetNumParamsForModType(int type) {
    switch(type) {
        case 0: return 3; // AHD: atk, hold, dec
        case 1: return 4; // ADSR: atk, dec, sus, rel
        case 2: return 3; // DRUM: peak, body, dec
        case 3: return 3; // LFO: osc, trig, freq
        case 4: return 4; // TRIG: atk, hold, dec, src
        case 5: return 3; // TRACK: src, lval, hval
        default: return 0;
    }
}

inline std::vector<UI_GridCell> GetModStaticText() {
    return {
        {"INST. MODS", 0, 0, "TITLE", "", "static", false, 0},
    };
}

inline std::vector<UI_GridCell> GetModDynamicTextDefaults() {
    return {
        {"13", 11, 0, "TITLE", "", "dynamic_text", false, 0}
    };
}

// Dynamically builds the interactive elements for all 4 quadrants
inline std::unordered_map<CursorId, std::vector<UI_GridCell>> GetModInteractiveFields(const engine::Instrument& inst) {
    std::unordered_map<CursorId, std::vector<UI_GridCell>> fields;

    for (int q = 0; q < 4; ++q) {
        int x_off = (q >= 2) ? 17 : 0;     // Left vs Right
        int y_off = (q % 2 == 1) ? 10 : 2; // Top vs Bottom

        fields[TypeCursor(q)] = {
            {"MOD" + std::to_string(q+1), x_off, y_off, "LABEL_LITE", "LABEL_LITE", "label", false, 0},
            {"00", x_off+5, y_off, "VALUE", "LABEL_LITE", "value", true, 0},
            {"TYPE_STR", x_off+7, y_off, "ACCENT", "LABEL_LITE", "accent", false, 0}
        };

        fields[DestCursor(q)] = {
            {"DEST", x_off, y_off+1, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", x_off+5, y_off+1, "VALUE", "LABEL_LITE", "value", true, 0},
            {"DEST_STR", x_off+7, y_off+1, "ACCENT", "LABEL_LITE", "accent", false, 0}
        };

        fields[AmtCursor(q)] = {
            {"AMT", x_off, y_off+2, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"FF", x_off+5, y_off+2, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", x_off+7, y_off+2, "SLIDER_BG", "LABEL_LITE", "slider", false, 9}
        };

        int type = inst.mods[q].type;
        auto addParam = [&](int pIdx, const std::string& label, bool isSlider) {
            CursorId id = ParamCursor(pIdx, q);
            int r = y_off + 2 + pIdx;
            fields[id] = {
                {label, x_off, r, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
                {"00", x_off+5, r, "VALUE", "LABEL_LITE", "value", true, 0}
            };
            if (isSlider) fields[id].push_back({"", x_off+7, r, "SLIDER_BG", "LABEL_LITE", "slider", false, 9});
            else fields[id].push_back({"ACCENT", x_off+7, r, "ACCENT", "LABEL_LITE", "accent", false, 0});
        };

        if (type == 0) { // AHD
            addParam(1, "ATK", true); addParam(2, "HOLD", true); addParam(3, "DEC", true);
        } else if (type == 1) { // ADSR
            addParam(1, "ATK", true); addParam(2, "DEC", true); addParam(3, "SUS", true); addParam(4, "REL", true);
        } else if (type == 2) { // DRUM
            addParam(1, "PEAK", true); addParam(2, "BODY", true); addParam(3, "DEC", true);
        } else if (type == 3) { // LFO
            addParam(1, "OSC", false); addParam(2, "TRIG", false); addParam(3, "FREQ", true);
        } else if (type == 4) { // TRIG
            addParam(1, "ATK", true); addParam(2, "HOLD", true); addParam(3, "DEC", true); addParam(4, "SRC", false);
        } else if (type == 5) { // TRACKING
            addParam(1, "SRC", false); addParam(2, "LVAL", true); addParam(3, "HVAL", true);
        }
    }
    return fields;
}

// Dynamically bridges the Navigation Graph so pressing UP/DOWN never points to a null node
inline std::unordered_map<CursorId, NavNode<CursorId>> GetModNavMap(const engine::Instrument& inst) {
    using C = CursorId;
    std::unordered_map<C, NavNode<C>> map;

    for (int q = 0; q < 4; ++q) {
        int numParams = GetNumParamsForModType(inst.mods[q].type);
        std::vector<C> qNodes = { TypeCursor(q), DestCursor(q), AmtCursor(q) };
        for (int p = 1; p <= numParams; ++p) qNodes.push_back(ParamCursor(p, q));

        auto getSafeHorizontalNode = [&](int targetQ, size_t rowIdx) {
            int tParams = GetNumParamsForModType(inst.mods[targetQ].type);
            size_t targetMax = 2 + tParams;
            if (rowIdx > targetMax) rowIdx = targetMax;
            return NodeAt(rowIdx, targetQ);
        };

        for (size_t i = 0; i < qNodes.size(); ++i) {
            C up = (i > 0) ? qNodes[i-1] : C::NONE;
            C down = (i < qNodes.size()-1) ? qNodes[i+1] : C::NONE;
            C left = C::NONE, right = C::NONE;

            if (q == 0) { // Top-Left
                if (i == qNodes.size()-1) down = TypeCursor(1);
                right = getSafeHorizontalNode(2, i);
            } else if (q == 1) { // Bottom-Left
                if (i == 0) up = ParamCursor(GetNumParamsForModType(inst.mods[0].type), 0);
                right = getSafeHorizontalNode(3, i);
            } else if (q == 2) { // Top-Right
                if (i == qNodes.size()-1) down = TypeCursor(3);
                left = getSafeHorizontalNode(0, i);
            } else if (q == 3) { // Bottom-Right
                if (i == 0) up = ParamCursor(GetNumParamsForModType(inst.mods[2].type), 2);
                left = getSafeHorizontalNode(1, i);
            }
            map[qNodes[i]] = {up, down, left, right};
        }
    }
    return map;
}

} // namespace mods
} // namespace ui
} // namespace m8
