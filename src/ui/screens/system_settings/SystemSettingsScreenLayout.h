#pragma once
#include "../../ui_types.h"
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>

namespace m8 {
namespace ui {
namespace system_settings {

enum class CursorId : uint8_t {
    NONE = 0,
    BACKLIGHT,
    FONT_OPTIONS,
    THEME_EDIT,
    NOTE_PREVIEW,
    REC_METRONOME,
    METRONOME_VOL,
    SONG_TEMPLATE_SAVE,
    SONG_TEMPLATE_CLEAR,
    USB_AUDIO_MODE,
    USB_MAIN_OUT,
    LINE_IN_GATE,
    SPLASH_SCREEN,
    HP_PROTECTION,
    KEY_DELAY_REP,
    BATT_STATUS
};

inline std::vector<UI_GridCell> GetSystemSettingsStaticText() {
    return {
        {"SYSTEM SETTINGS", 0, 0, "TITLE", "", "static", false, 0},
    };
}

inline std::unordered_map<CursorId, std::vector<UI_GridCell>> GetSystemSettingsInteractiveFields() {
    using C = CursorId;
    return {
        {C::BACKLIGHT, {
            {"BACKLIGHT", 0, 2, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"C0", 15, 2, "LABEL_DIM", "LABEL_LITE", "value", true, 0}
        }},
        {C::FONT_OPTIONS, {
            {"FONT OPTIONS", 0, 3, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"UPPERCASE", 15, 3, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::THEME_EDIT, {
            {"THEME", 0, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"EDIT THEME", 15, 4, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::NOTE_PREVIEW, {
            {"NOTE PREVIEW", 0, 6, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"ON", 15, 6, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::REC_METRONOME, {
            {"REC.METRONOME", 0, 7, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00OFF", 15, 7, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::METRONOME_VOL, {
            {"METRONOME VOL", 0, 8, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"E0", 15, 8, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::SONG_TEMPLATE_SAVE, {
            {"SONG TEMPLATE", 0, 9, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"SAVE", 15, 9, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::SONG_TEMPLATE_CLEAR, {
            {"CLEAR", 20, 9, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::USB_AUDIO_MODE, {
            {"USB AUDIO MODE", 0, 11, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"STEREO OUT", 15, 11, "LABEL_DIM", "LABEL_LITE", "value", true, 0}
        }},
        {C::USB_MAIN_OUT, {
            {"USB MAIN OUT", 0, 12, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"MIX", 15, 12, "LABEL_DIM", "LABEL_LITE", "value", true, 0}
        }},
        {C::LINE_IN_GATE, {
            {"LINE-IN GATE", 0, 13, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"OFF", 15, 13, "LABEL_DIM", "LABEL_LITE", "value", true, 0}
        }},
        {C::SPLASH_SCREEN, {
            {"SPLASH SCREEN", 0, 15, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"ON", 15, 15, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::HP_PROTECTION, {
            {"HP PROTECTION", 0, 16, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"ON", 15, 16, "LABEL_DIM", "LABEL_LITE", "value", true, 0}
        }},
        {C::KEY_DELAY_REP, {
            {"KEY DELAY:REP", 0, 17, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"150:45", 15, 17, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::BATT_STATUS, {
            {"BATT.STATUS", 0, 18, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"USB/DC", 15, 18, "LABEL_DIM", "LABEL_LITE", "value", true, 0}
        }}
    };
}

inline std::unordered_map<CursorId, NavNode<CursorId>> GetSystemSettingsNavMap() {
    using C = CursorId;
    return {
        {C::BACKLIGHT,           {/*U*/C::NONE,                /*D*/C::FONT_OPTIONS,       /*L*/C::NONE,                 /*R*/C::NONE}},
        {C::FONT_OPTIONS,        {/*U*/C::BACKLIGHT,           /*D*/C::THEME_EDIT,         /*L*/C::NONE,                 /*R*/C::NONE}},
        {C::THEME_EDIT,          {/*U*/C::FONT_OPTIONS,        /*D*/C::NOTE_PREVIEW,       /*L*/C::NONE,                 /*R*/C::NONE}},
        {C::NOTE_PREVIEW,        {/*U*/C::THEME_EDIT,          /*D*/C::REC_METRONOME,      /*L*/C::NONE,                 /*R*/C::NONE}},
        {C::REC_METRONOME,       {/*U*/C::NOTE_PREVIEW,        /*D*/C::METRONOME_VOL,      /*L*/C::NONE,                 /*R*/C::NONE}},
        {C::METRONOME_VOL,       {/*U*/C::REC_METRONOME,       /*D*/C::SONG_TEMPLATE_SAVE, /*L*/C::NONE,                 /*R*/C::NONE}},
        {C::SONG_TEMPLATE_SAVE,  {/*U*/C::METRONOME_VOL,       /*D*/C::USB_AUDIO_MODE,     /*L*/C::NONE,                 /*R*/C::SONG_TEMPLATE_CLEAR}},
        {C::SONG_TEMPLATE_CLEAR, {/*U*/C::METRONOME_VOL,       /*D*/C::USB_AUDIO_MODE,     /*L*/C::SONG_TEMPLATE_SAVE,   /*R*/C::NONE}},
        {C::USB_AUDIO_MODE,      {/*U*/C::SONG_TEMPLATE_SAVE,  /*D*/C::USB_MAIN_OUT,       /*L*/C::NONE,                 /*R*/C::NONE}},
        {C::USB_MAIN_OUT,        {/*U*/C::USB_AUDIO_MODE,      /*D*/C::LINE_IN_GATE,       /*L*/C::NONE,                 /*R*/C::NONE}},
        {C::LINE_IN_GATE,        {/*U*/C::USB_MAIN_OUT,        /*D*/C::SPLASH_SCREEN,      /*L*/C::NONE,                 /*R*/C::NONE}},
        {C::SPLASH_SCREEN,       {/*U*/C::LINE_IN_GATE,        /*D*/C::HP_PROTECTION,      /*L*/C::NONE,                 /*R*/C::NONE}},
        {C::HP_PROTECTION,       {/*U*/C::SPLASH_SCREEN,       /*D*/C::KEY_DELAY_REP,      /*L*/C::NONE,                 /*R*/C::NONE}},
        {C::KEY_DELAY_REP,       {/*U*/C::HP_PROTECTION,       /*D*/C::BATT_STATUS,        /*L*/C::NONE,                 /*R*/C::NONE}},
        {C::BATT_STATUS,         {/*U*/C::KEY_DELAY_REP,       /*D*/C::NONE,               /*L*/C::NONE,                 /*R*/C::NONE}}
    };
}

} // namespace system_settings
} // namespace ui
} // namespace m8
