#pragma once
#include <SDL3/SDL.h>
#include <string>
#include <array>
#include <cstdint>
#include <cstring>

namespace m8 {
namespace ui {

struct ThemeColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    
    SDL_Color toSDL() const {
        return {r, g, b, 255};
    }
};

enum class ThemeSlot : uint8_t {
    BACKGROUND = 0,
    TEXT_EMPTY,
    TEXT_INFO,
    TEXT_DEFAULT,
    TEXT_VALUE,
    TEXT_TITLES,
    PLAY_MARKERS,
    CURSOR,
    SELECTION,
    SCOPE_SLIDER,
    METER_LOW,
    METER_MID,
    METER_PEAK,
    COUNT
};

struct Theme {
    char name[13] = "DEFAULT-----";
    std::array<ThemeColor, static_cast<size_t>(ThemeSlot::COUNT)> colors = {{
        {0x00, 0x00, 0x00}, // BACKGROUND
        {0x20, 0x28, 0x30}, // TEXT_EMPTY (LABEL_DIM)
        {0x48, 0x50, 0x60}, // TEXT_INFO
        {0x90, 0xB0, 0xB8}, // TEXT_DEFAULT (LABEL_LITE)
        {0xFF, 0xFF, 0xFF}, // TEXT_VALUE (VALUE)
        {0xFF, 0x20, 0x40}, // TEXT_TITLES (TITLE)
        {0x00, 0xFF, 0x60}, // PLAY_MARKERS
        {0x00, 0xFF, 0xFF}, // CURSOR
        {0x00, 0xFF, 0x80}, // SELECTION
        {0x90, 0xB8, 0xB8}, // SCOPE_SLIDER
        {0x00, 0xFF, 0xE0}, // METER_LOW
        {0xFF, 0xFF, 0x50}, // METER_MID
        {0xFF, 0x00, 0x80}  // METER_PEAK
    }};

    void resetDefault() {
        std::strncpy(name, "DEFAULT-----", 12);
        name[12] = '\0';
        colors[0]  = {0x00, 0x00, 0x00};
        colors[1]  = {0x20, 0x28, 0x30};
        colors[2]  = {0x48, 0x50, 0x60};
        colors[3]  = {0x90, 0xB0, 0xB8};
        colors[4]  = {0xFF, 0xFF, 0xFF};
        colors[5]  = {0xFF, 0x20, 0x40};
        colors[6]  = {0x00, 0xFF, 0x60};
        colors[7]  = {0x00, 0xFF, 0xFF};
        colors[8]  = {0x00, 0xFF, 0x80};
        colors[9]  = {0x90, 0xB8, 0xB8};
        colors[10] = {0x00, 0xFF, 0xE0};
        colors[11] = {0xFF, 0xFF, 0x50};
        colors[12] = {0xFF, 0x00, 0x80};
    }
};

inline Theme g_currentTheme;

inline SDL_Color GetThemeColor(const std::string& name) {
    if (name == "BACKGROUND") return g_currentTheme.colors[static_cast<size_t>(ThemeSlot::BACKGROUND)].toSDL();
    if (name == "LABEL_DIM" || name == "TEXT_EMPTY" || name == "DIM") return g_currentTheme.colors[static_cast<size_t>(ThemeSlot::TEXT_EMPTY)].toSDL();
    if (name == "TEXT_INFO" || name == "INFO") return g_currentTheme.colors[static_cast<size_t>(ThemeSlot::TEXT_INFO)].toSDL();
    if (name == "LABEL_LITE" || name == "TEXT_DEFAULT" || name == "LITE") return g_currentTheme.colors[static_cast<size_t>(ThemeSlot::TEXT_DEFAULT)].toSDL();
    if (name == "VALUE" || name == "TEXT_VALUE") return g_currentTheme.colors[static_cast<size_t>(ThemeSlot::TEXT_VALUE)].toSDL();
    if (name == "TITLE" || name == "TEXT_TITLES" || name == "ACCENT") return g_currentTheme.colors[static_cast<size_t>(ThemeSlot::TEXT_TITLES)].toSDL();
    if (name == "PLAY" || name == "PLAY_MARKERS" || name == "PLAY_MARKER") return g_currentTheme.colors[static_cast<size_t>(ThemeSlot::PLAY_MARKERS)].toSDL();
    if (name == "CURSOR") return g_currentTheme.colors[static_cast<size_t>(ThemeSlot::CURSOR)].toSDL();
    if (name == "SELECTION") return g_currentTheme.colors[static_cast<size_t>(ThemeSlot::SELECTION)].toSDL();
    if (name == "SCOPE" || name == "SCOPE_SLIDER") return g_currentTheme.colors[static_cast<size_t>(ThemeSlot::SCOPE_SLIDER)].toSDL();
    if (name == "METER_LOW") return g_currentTheme.colors[static_cast<size_t>(ThemeSlot::METER_LOW)].toSDL();
    if (name == "METER_MID") return g_currentTheme.colors[static_cast<size_t>(ThemeSlot::METER_MID)].toSDL();
    if (name == "METER_PEAK") return g_currentTheme.colors[static_cast<size_t>(ThemeSlot::METER_PEAK)].toSDL();
    return {255, 255, 255, 255};
}

inline SDL_Color GetThemeColor(ThemeSlot slot) {
    return g_currentTheme.colors[static_cast<size_t>(slot)].toSDL();
}

} // namespace ui
} // namespace m8
