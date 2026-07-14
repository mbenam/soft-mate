#pragma once
#include "../../ui_types.h"
#include <vector>
#include <unordered_map>
#include <string>

namespace m8 {
namespace ui {
namespace mixer {

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

inline std::unordered_map<std::string, std::vector<UI_GridCell>> GetMixerInteractiveFields() {
    std::unordered_map<std::string, std::vector<UI_GridCell>> fields = {
        {"OUT_VOL", { {"00", 12, 3, "VALUE", "LABEL_LITE", "value", true, 0} }},
        
        {"MST_CHO", { {"E0", 0, 19, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"MST_DEL", { {"E0", 4, 19, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"MST_REV", { {"E0", 8, 19, "VALUE", "LABEL_LITE", "value", true, 0} }},
        
        {"IN_VOL",  { {"00", 12, 19, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"IN_CHO",  { {"00", 12, 21, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"IN_DEL",  { {"00", 12, 22, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"IN_REV",  { {"00", 12, 23, "VALUE", "LABEL_LITE", "value", true, 0} }},

        {"USB_VOL", { {"00", 18, 19, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"USB_CHO", { {"00", 18, 21, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"USB_DEL", { {"00", 18, 22, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"USB_REV", { {"00", 18, 23, "VALUE", "LABEL_LITE", "value", true, 0} }},
        
        {"MIX_VOL", { {"DC", 27, 19, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"LIM_VAL", { {"40", 27, 20, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"DJF_FREQ", { {"80", 27, 21, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"DJF_RES", { {"80", 27, 22, "VALUE", "LABEL_LITE", "value", true, 0} }},
        {"DJF_TYP", { {"00", 27, 23, "VALUE", "LABEL_LITE", "value", true, 0} }}
    };

    // Tracks 1-8 (0-7 indexed)
    for (int i = 0; i < 8; i++) {
        fields["TRK_VOL_" + std::to_string(i)] = { {"00", i * 3, 18, "VALUE", "LABEL_LITE", "value", true, 0} };
    }
    return fields;
}

inline std::unordered_map<std::string, NavNode> GetMixerNavMap() {
    return {
        {"OUT_VOL",   {/*U*/"",             /*D*/"TRK_VOL_4", /*L*/"",            /*R*/""}},
        {"TRK_VOL_0", {/*U*/"OUT_VOL",      /*D*/"MST_CHO",   /*L*/"",            /*R*/"TRK_VOL_1"}},
        {"TRK_VOL_1", {/*U*/"OUT_VOL",      /*D*/"MST_DEL",   /*L*/"TRK_VOL_0",   /*R*/"TRK_VOL_2"}},
        {"TRK_VOL_2", {/*U*/"OUT_VOL",      /*D*/"MST_REV",   /*L*/"TRK_VOL_1",   /*R*/"TRK_VOL_3"}},
        {"TRK_VOL_3", {/*U*/"OUT_VOL",      /*D*/"IN_VOL",    /*L*/"TRK_VOL_2",   /*R*/"TRK_VOL_4"}},
        {"TRK_VOL_4", {/*U*/"OUT_VOL",      /*D*/"IN_VOL",    /*L*/"TRK_VOL_3",   /*R*/"TRK_VOL_5"}},
        {"TRK_VOL_5", {/*U*/"OUT_VOL",      /*D*/"USB_VOL",   /*L*/"TRK_VOL_4",   /*R*/"TRK_VOL_6"}},
        {"TRK_VOL_6", {/*U*/"OUT_VOL",      /*D*/"USB_VOL",   /*L*/"TRK_VOL_5",   /*R*/"TRK_VOL_7"}},
        {"TRK_VOL_7", {/*U*/"OUT_VOL",      /*D*/"MIX_VOL",   /*L*/"TRK_VOL_6",   /*R*/""}},
        
        {"MST_CHO",   {/*U*/"TRK_VOL_0",    /*D*/"",          /*L*/"",            /*R*/"MST_DEL"}},
        {"MST_DEL",   {/*U*/"TRK_VOL_1",    /*D*/"",          /*L*/"MST_CHO",     /*R*/"MST_REV"}},
        {"MST_REV",   {/*U*/"TRK_VOL_2",    /*D*/"",          /*L*/"MST_DEL",     /*R*/"IN_VOL"}},
        
        {"IN_VOL",    {/*U*/"TRK_VOL_4",    /*D*/"IN_CHO",    /*L*/"MST_REV",     /*R*/"USB_VOL"}},
        {"IN_CHO",    {/*U*/"IN_VOL",       /*D*/"IN_DEL",    /*L*/"MST_REV",     /*R*/"USB_CHO"}},
        {"IN_DEL",    {/*U*/"IN_CHO",       /*D*/"IN_REV",    /*L*/"MST_REV",     /*R*/"USB_DEL"}},
        {"IN_REV",    {/*U*/"IN_DEL",       /*D*/"",          /*L*/"MST_REV",     /*R*/"USB_REV"}},

        {"USB_VOL",   {/*U*/"TRK_VOL_6",    /*D*/"USB_CHO",   /*L*/"IN_VOL",      /*R*/"MIX_VOL"}},
        {"USB_CHO",   {/*U*/"USB_VOL",      /*D*/"USB_DEL",   /*L*/"IN_CHO",      /*R*/"LIM_VAL"}},
        {"USB_DEL",   {/*U*/"USB_CHO",      /*D*/"USB_REV",   /*L*/"IN_DEL",      /*R*/"DJF_FREQ"}},
        {"USB_REV",   {/*U*/"USB_DEL",      /*D*/"",          /*L*/"IN_REV",      /*R*/"DJF_RES"}},

        {"MIX_VOL",   {/*U*/"TRK_VOL_7",    /*D*/"LIM_VAL",   /*L*/"USB_VOL",     /*R*/""}},
        {"LIM_VAL",   {/*U*/"MIX_VOL",      /*D*/"DJF_FREQ",  /*L*/"USB_CHO",     /*R*/""}},
        {"DJF_FREQ",  {/*U*/"LIM_VAL",      /*D*/"DJF_RES",   /*L*/"USB_DEL",     /*R*/""}},
        {"DJF_RES",   {/*U*/"DJF_FREQ",     /*D*/"DJF_TYP",   /*L*/"USB_REV",     /*R*/""}},
        {"DJF_TYP",   {/*U*/"DJF_RES",      /*D*/"",          /*L*/"USB_REV",     /*R*/""}}
    };
}

} // namespace mixer
} // namespace ui
} // namespace m8
