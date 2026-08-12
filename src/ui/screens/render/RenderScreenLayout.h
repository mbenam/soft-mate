#pragma once
#include "../../ui_types.h"
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>

namespace m8 {
namespace ui {
namespace render {

enum class CursorId : uint8_t {
    NONE = 0,
    SONG_ROW_START,
    SONG_ROW_LAST,
    REPEAT_SONG,
    TRACK_1, TRACK_2, TRACK_3, TRACK_4, TRACK_5, TRACK_6, TRACK_7, TRACK_8,
    MODFX,
    DELAY,
    REVERB,
    LIMITER,
    MIX_EQ,
    MODE,
    NAME,
    RENDER_MIXED,
    RENDER_STEMS
};

inline std::vector<UI_GridCell> GetRenderStaticText() {
    return {
        {"RENDER AUDIO", 0, 0, "TITLE", "", "static", false, 0},
        {"1", 16, 6, "LABEL_DIM", "", "static", false, 0},
        {"2", 19, 6, "LABEL_DIM", "", "static", false, 0},
        {"3", 22, 6, "LABEL_DIM", "", "static", false, 0},
        {"4", 25, 6, "LABEL_DIM", "", "static", false, 0},
        {"5", 28, 6, "LABEL_DIM", "", "static", false, 0},
        {"6", 31, 6, "LABEL_DIM", "", "static", false, 0},
        {"7", 34, 6, "LABEL_DIM", "", "static", false, 0},
        {"8", 37, 6, "LABEL_DIM", "", "static", false, 0},
    };
}

inline std::unordered_map<CursorId, std::vector<UI_GridCell>> GetRenderInteractiveFields() {
    using C = CursorId;
    return {
        {C::SONG_ROW_START, {
            {"SONG ROW START", 0, 2, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 16, 2, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::SONG_ROW_LAST, {
            {"SONG ROW LAST", 0, 3, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"--", 16, 3, "VALUE", "LABEL_LITE", "value", true, 0},
            {"AUTO", 18, 3, "ACCENT", "LABEL_LITE", "accent", false, 0}
        }},
        {C::REPEAT_SONG, {
            {"REPEAT SONG", 0, 4, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"00", 16, 4, "VALUE", "LABEL_LITE", "value", true, 0},
            {"OFF", 18, 4, "ACCENT", "LABEL_LITE", "accent", false, 0}
        }},
        {C::TRACK_1, {
            {"TRACKS", 0, 7, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"ON", 16, 7, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::TRACK_2, {
            {"ON", 19, 7, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::TRACK_3, {
            {"ON", 22, 7, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::TRACK_4, {
            {"ON", 25, 7, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::TRACK_5, {
            {"ON", 28, 7, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::TRACK_6, {
            {"ON", 31, 7, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::TRACK_7, {
            {"ON", 34, 7, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::TRACK_8, {
            {"ON", 37, 7, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::MODFX, {
            {"MODFX", 0, 8, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"ON", 16, 8, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::DELAY, {
            {"DELAY", 0, 9, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"ON", 16, 9, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::REVERB, {
            {"REVERB", 0, 10, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"ON", 16, 10, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::LIMITER, {
            {"LIMITER", 0, 11, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"ON", 16, 11, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::MIX_EQ, {
            {"MIX EQ", 0, 12, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"ON", 16, 12, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::MODE, {
            {"MODE", 0, 14, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"16-BIT", 16, 14, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::NAME, {
            {"NAME", 0, 16, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"PROBE_SELFTE------", 16, 16, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::RENDER_MIXED, {
            {"RENDER", 0, 17, "LABEL_DIM", "LABEL_LITE", "label", false, 0},
            {"MIXED", 16, 17, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
        {C::RENDER_STEMS, {
            {"STEMS", 22, 17, "VALUE", "LABEL_LITE", "value", true, 0}
        }},
    };
}

inline std::unordered_map<CursorId, NavNode<CursorId>> GetRenderNavMap() {
    using C = CursorId;
    return {
        {C::SONG_ROW_START, {C::NONE, C::SONG_ROW_LAST, C::NONE, C::NONE}},
        {C::SONG_ROW_LAST,  {C::SONG_ROW_START, C::REPEAT_SONG, C::NONE, C::NONE}},
        {C::REPEAT_SONG,    {C::SONG_ROW_LAST, C::TRACK_1, C::NONE, C::NONE}},

        {C::TRACK_1,        {C::REPEAT_SONG, C::MODFX, C::NONE, C::TRACK_2}},
        {C::TRACK_2,        {C::REPEAT_SONG, C::MODFX, C::TRACK_1, C::TRACK_3}},
        {C::TRACK_3,        {C::REPEAT_SONG, C::MODFX, C::TRACK_2, C::TRACK_4}},
        {C::TRACK_4,        {C::REPEAT_SONG, C::MODFX, C::TRACK_3, C::TRACK_5}},
        {C::TRACK_5,        {C::REPEAT_SONG, C::MODFX, C::TRACK_4, C::TRACK_6}},
        {C::TRACK_6,        {C::REPEAT_SONG, C::MODFX, C::TRACK_5, C::TRACK_7}},
        {C::TRACK_7,        {C::REPEAT_SONG, C::MODFX, C::TRACK_6, C::TRACK_8}},
        {C::TRACK_8,        {C::REPEAT_SONG, C::MODFX, C::TRACK_7, C::NONE}},

        {C::MODFX,          {C::TRACK_1, C::DELAY, C::NONE, C::NONE}},
        {C::DELAY,          {C::MODFX, C::REVERB, C::NONE, C::NONE}},
        {C::REVERB,         {C::DELAY, C::LIMITER, C::NONE, C::NONE}},
        {C::LIMITER,        {C::REVERB, C::MIX_EQ, C::NONE, C::NONE}},
        {C::MIX_EQ,         {C::LIMITER, C::MODE, C::NONE, C::NONE}},
        {C::MODE,           {C::MIX_EQ, C::NAME, C::NONE, C::NONE}},
        {C::NAME,           {C::MODE, C::RENDER_MIXED, C::NONE, C::NONE}},
        {C::RENDER_MIXED,   {C::NAME, C::NONE, C::NONE, C::RENDER_STEMS}},
        {C::RENDER_STEMS,   {C::NAME, C::NONE, C::RENDER_MIXED, C::NONE}},
    };
}

} // namespace render
} // namespace ui
} // namespace m8
