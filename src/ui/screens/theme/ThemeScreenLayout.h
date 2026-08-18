#pragma once
#include "../../ui_types.h"
#include "../../Theme.h"
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>

namespace m8 {
namespace ui {
namespace theme {

enum class CursorId : uint8_t {
    NONE = 0,
    MODE,
    COLOR_BACKGROUND_R, COLOR_BACKGROUND_G, COLOR_BACKGROUND_B,
    COLOR_TEXT_EMPTY_R, COLOR_TEXT_EMPTY_G, COLOR_TEXT_EMPTY_B,
    COLOR_TEXT_INFO_R, COLOR_TEXT_INFO_G, COLOR_TEXT_INFO_B,
    COLOR_TEXT_DEFAULT_R, COLOR_TEXT_DEFAULT_G, COLOR_TEXT_DEFAULT_B,
    COLOR_TEXT_VALUE_R, COLOR_TEXT_VALUE_G, COLOR_TEXT_VALUE_B,
    COLOR_TEXT_TITLES_R, COLOR_TEXT_TITLES_G, COLOR_TEXT_TITLES_B,
    COLOR_PLAY_MARKERS_R, COLOR_PLAY_MARKERS_G, COLOR_PLAY_MARKERS_B,
    COLOR_CURSOR_R, COLOR_CURSOR_G, COLOR_CURSOR_B,
    COLOR_SELECTION_R, COLOR_SELECTION_G, COLOR_SELECTION_B,
    COLOR_SCOPE_SLIDER_R, COLOR_SCOPE_SLIDER_G, COLOR_SCOPE_SLIDER_B,
    COLOR_METER_LOW_R, COLOR_METER_LOW_G, COLOR_METER_LOW_B,
    COLOR_METER_MID_R, COLOR_METER_MID_G, COLOR_METER_MID_B,
    COLOR_METER_PEAK_R, COLOR_METER_PEAK_G, COLOR_METER_PEAK_B,
    THEME_NAME,
    THEME_LOAD, THEME_SAVE, THEME_RESET
};

struct ThemeSlotInfo {
    ThemeSlot slot;
    const char* label;
    int row;
    CursorId cidR;
    CursorId cidG;
    CursorId cidB;
};

inline const ThemeSlotInfo kThemeSlotInfos[] = {
    {ThemeSlot::BACKGROUND,   "BACKGROUND",   2,  CursorId::COLOR_BACKGROUND_R,   CursorId::COLOR_BACKGROUND_G,   CursorId::COLOR_BACKGROUND_B},
    {ThemeSlot::TEXT_EMPTY,   "TEXT:EMPTY",   3,  CursorId::COLOR_TEXT_EMPTY_R,   CursorId::COLOR_TEXT_EMPTY_G,   CursorId::COLOR_TEXT_EMPTY_B},
    {ThemeSlot::TEXT_INFO,    "TEXT:INFO",    4,  CursorId::COLOR_TEXT_INFO_R,    CursorId::COLOR_TEXT_INFO_G,    CursorId::COLOR_TEXT_INFO_B},
    {ThemeSlot::TEXT_DEFAULT, "TEXT:DEFAULT", 5,  CursorId::COLOR_TEXT_DEFAULT_R, CursorId::COLOR_TEXT_DEFAULT_G, CursorId::COLOR_TEXT_DEFAULT_B},
    {ThemeSlot::TEXT_VALUE,   "TEXT:VALUE",   6,  CursorId::COLOR_TEXT_VALUE_R,   CursorId::COLOR_TEXT_VALUE_G,   CursorId::COLOR_TEXT_VALUE_B},
    {ThemeSlot::TEXT_TITLES,  "TEXT:TITLES",  7,  CursorId::COLOR_TEXT_TITLES_R,  CursorId::COLOR_TEXT_TITLES_G,  CursorId::COLOR_TEXT_TITLES_B},
    {ThemeSlot::PLAY_MARKERS, "PLAY MARKERS", 8,  CursorId::COLOR_PLAY_MARKERS_R, CursorId::COLOR_PLAY_MARKERS_G, CursorId::COLOR_PLAY_MARKERS_B},
    {ThemeSlot::CURSOR,       "CURSOR",       9,  CursorId::COLOR_CURSOR_R,       CursorId::COLOR_CURSOR_G,       CursorId::COLOR_CURSOR_B},
    {ThemeSlot::SELECTION,    "SELECTION",    10, CursorId::COLOR_SELECTION_R,    CursorId::COLOR_SELECTION_G,    CursorId::COLOR_SELECTION_B},
    {ThemeSlot::SCOPE_SLIDER, "SCOPE/SLIDER", 11, CursorId::COLOR_SCOPE_SLIDER_R, CursorId::COLOR_SCOPE_SLIDER_G, CursorId::COLOR_SCOPE_SLIDER_B},
    {ThemeSlot::METER_LOW,    "METER LOW",    12, CursorId::COLOR_METER_LOW_R,    CursorId::COLOR_METER_LOW_G,    CursorId::COLOR_METER_LOW_B},
    {ThemeSlot::METER_MID,    "METER MID",    13, CursorId::COLOR_METER_MID_R,    CursorId::COLOR_METER_MID_G,    CursorId::COLOR_METER_MID_B},
    {ThemeSlot::METER_PEAK,   "METER PEAK",   14, CursorId::COLOR_METER_PEAK_R,   CursorId::COLOR_METER_PEAK_G,   CursorId::COLOR_METER_PEAK_B}
};

} // namespace theme
} // namespace ui
} // namespace m8
