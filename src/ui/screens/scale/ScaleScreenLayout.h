#pragma once
#include "../../ui_types.h"
#include <vector>
#include <unordered_map>
#include <string>

namespace m8 {
namespace ui {
namespace scale {

inline std::vector<UI_GridCell> GetScaleStaticText() {
    std::vector<UI_GridCell> st = {
        {"SCALE", 0, 0, "TITLE", "", "static", false, 0},
        {"EN OFFSET", 3, 3, "LABEL_DIM", "", "static", false, 0},
        
        // Navigator Map
    };
    
    const char* notes[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    for (int i = 0; i < 12; i++) {
        st.push_back({notes[i], 0, i + 4, "VALUE", "", "static", false, 0});
    }
    return st;
}

inline std::vector<UI_GridCell> GetScaleDynamicTextDefaults() {
    return {
        {"00", 7, 0, "TITLE", "", "dynamic_text", false, 0},
        {"T>128", 34, 2, "LABEL_DIM", "", "dynamic_text", false, 0}
    };
}

inline std::unordered_map<std::string, std::vector<UI_GridCell>> GetScaleInteractiveFields() {
    std::unordered_map<std::string, std::vector<UI_GridCell>> fields = {
        {"TUNE", { {"TUNE", 0, 16, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"440.00", 7, 16, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"NAME", { {"NAME", 0, 17, "LABEL_DIM", "LABEL_LITE", "label", false, 0}, {"CHROMATIC-------", 7, 17, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"CMD_LOAD", { {"LOAD", 7, 18, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"CMD_SAVE", { {"SAVE", 12, 18, "VALUE", "LABEL_LITE", "value", true, 0} }}
    };
    
    // Procedurally generate the 12 rows of notes
    for (int i = 0; i < 12; i++) {
        int r = i + 4;
        fields["NOTE_EN_" + std::to_string(i)] = { {"ON", 3, r, "VALUE", "LABEL_LITE", "value", true, 0} };
        fields["NOTE_OFFSET_" + std::to_string(i)] = { {"00.00", 7, r, "VALUE", "LABEL_LITE", "value", true, 0} };
    }
    
    return fields;
}

inline std::unordered_map<std::string, NavNode> GetScaleNavMap() {
    std::unordered_map<std::string, NavNode> map = {
        {"KEY",      {/*U*/"",             /*D*/"NOTE_EN_0",   /*L*/"",           /*R*/""}},
        {"TUNE",     {/*U*/"NOTE_EN_11",   /*D*/"NAME",        /*L*/"",           /*R*/""}},
        {"NAME",     {/*U*/"TUNE",         /*D*/"CMD_LOAD",    /*L*/"",           /*R*/""}},
        {"CMD_LOAD", {/*U*/"NAME",         /*D*/"",            /*L*/"",           /*R*/"CMD_SAVE"}},
        {"CMD_SAVE", {/*U*/"NAME",         /*D*/"",            /*L*/"CMD_LOAD",   /*R*/""}}
    };
    
    // Procedurally link the 12 grid rows
    for(int i = 0; i < 12; i++) {
        std::string en = "NOTE_EN_" + std::to_string(i);
        std::string off = "NOTE_OFFSET_" + std::to_string(i);
        
        std::string up_en = (i == 0) ? "KEY" : "NOTE_EN_" + std::to_string(i-1);
        std::string up_off = (i == 0) ? "KEY" : "NOTE_OFFSET_" + std::to_string(i-1);
        
        std::string down_en = (i == 11) ? "TUNE" : "NOTE_EN_" + std::to_string(i+1);
        std::string down_off = (i == 11) ? "TUNE" : "NOTE_OFFSET_" + std::to_string(i+1);
        
        map[en] = {up_en, down_en, "", off};
        map[off] = {up_off, down_off, en, ""};
    }
    
    return map;
}

} // namespace scale
} // namespace ui
} // namespace m8
