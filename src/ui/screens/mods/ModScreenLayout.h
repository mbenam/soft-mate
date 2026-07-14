#pragma once
#include "../../ui_types.h"
#include "../../../engine/Engine.h"
#include <vector>
#include <unordered_map>
#include <string>

namespace m8 {
namespace ui {
namespace mods {

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
        {"13", 11, 0, "TITLE", "", "dynamic_text", false, 0},
        {"T>128", 34, 2, "LABEL_DIM", "", "dynamic_text", false, 0}
    };
}

// Dynamically builds the interactive elements for all 4 quadrants
inline std::unordered_map<std::string, std::vector<UI_GridCell>> GetModInteractiveFields(const engine::Instrument& inst) {
    std::unordered_map<std::string, std::vector<UI_GridCell>> fields;
    
    for (int q = 0; q < 4; ++q) {
        int x_off = (q >= 2) ? 17 : 0;     // Left vs Right
        int y_off = (q % 2 == 1) ? 10 : 2; // Top vs Bottom
        std::string qs = std::to_string(q);
        
        fields["MOD_TYPE_" + qs] = {
            {"MOD" + std::to_string(q+1), x_off, y_off, "LABEL_LITE", "LABEL_LITE", "label", false, 0},
            {"00", x_off+5, y_off, "VALUE", "LABEL_LITE", "value", true, 0},
            {"TYPE_STR", x_off+7, y_off, "ACCENT", "LABEL_LITE", "accent", false, 0}
        };
        
        fields["MOD_DEST_" + qs] = {
            {"DEST", x_off, y_off+1, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", x_off+5, y_off+1, "VALUE", "LABEL_LITE", "value", true, 0},
            {"DEST_STR", x_off+7, y_off+1, "ACCENT", "LABEL_LITE", "accent", false, 0}
        };
        
        fields["MOD_AMT_" + qs] = {
            {"AMT", x_off, y_off+2, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"FF", x_off+5, y_off+2, "VALUE", "LABEL_LITE", "value", true, 0},
            {"", x_off+7, y_off+2, "SLIDER_BG", "LABEL_LITE", "slider", false, 9}
        };
        
        int type = inst.mods[q].type;
        auto addParam = [&](int pIdx, const std::string& label, bool isSlider) {
            std::string id = "MOD_P" + std::to_string(pIdx) + "_" + qs;
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
inline std::unordered_map<std::string, NavNode> GetModNavMap(const engine::Instrument& inst) {
    std::unordered_map<std::string, NavNode> map;
    
    for (int q = 0; q < 4; ++q) {
        int numParams = GetNumParamsForModType(inst.mods[q].type);
        std::vector<std::string> qNodes = {
            "MOD_TYPE_" + std::to_string(q),
            "MOD_DEST_" + std::to_string(q),
            "MOD_AMT_"  + std::to_string(q)
        };
        for (int p = 1; p <= numParams; ++p) qNodes.push_back("MOD_P" + std::to_string(p) + "_" + std::to_string(q));
        
        auto getSafeHorizontalNode = [&](int targetQ, size_t rowIdx) {
            int tParams = GetNumParamsForModType(inst.mods[targetQ].type);
            size_t targetMax = 2 + tParams; 
            if (rowIdx > targetMax) rowIdx = targetMax;
            if (rowIdx == 0) return "MOD_TYPE_" + std::to_string(targetQ);
            if (rowIdx == 1) return "MOD_DEST_" + std::to_string(targetQ);
            if (rowIdx == 2) return "MOD_AMT_" + std::to_string(targetQ);
            return "MOD_P" + std::to_string(rowIdx - 2) + "_" + std::to_string(targetQ);
        };

        for (size_t i = 0; i < qNodes.size(); ++i) {
            std::string up = (i > 0) ? qNodes[i-1] : "";
            std::string down = (i < qNodes.size()-1) ? qNodes[i+1] : "";
            std::string left = "", right = "";
            
            if (q == 0) { // Top-Left
                if (i == qNodes.size()-1) down = "MOD_TYPE_1";
                right = getSafeHorizontalNode(2, i);
            } else if (q == 1) { // Bottom-Left
                if (i == 0) up = "MOD_P" + std::to_string(GetNumParamsForModType(inst.mods[0].type)) + "_0";
                right = getSafeHorizontalNode(3, i);
            } else if (q == 2) { // Top-Right
                if (i == qNodes.size()-1) down = "MOD_TYPE_3";
                left = getSafeHorizontalNode(0, i);
            } else if (q == 3) { // Bottom-Right
                if (i == 0) up = "MOD_P" + std::to_string(GetNumParamsForModType(inst.mods[2].type)) + "_2";
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
