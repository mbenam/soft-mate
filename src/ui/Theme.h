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

// The boot theme, read off a real M8 (firmware 6.5.2, theme m8-default-6.5.2)
// on 2026-08-21 rather than eyeballed. Every entry marked "observed" is a
// colour the device was seen rendering; docs/tools/hw_findings.md UI-15 records
// the captures and which cells justify each role.
//
// The device palette is RGB565: red and blue are multiples of 8, green of 4.
// That is exactly what the old defaults got wrong -- four slots were written as
// 0xFF, which the panel cannot produce, and quantizing each of those to RGB565
// lands on the measured device value, four times out of four. The three slots
// marked INFERRED were never observed (they need a selection or live meters on
// screen), so they keep their original hue with the same quantization applied,
// which at least makes them colours the hardware could actually display.
//
// One array, shared by the member initializer and resetDefault(), because two
// hand-maintained copies of the same thirteen colours will drift.
inline constexpr ThemeColor kDefaultThemeColors[static_cast<size_t>(ThemeSlot::COUNT)] = {
    {0x00, 0x00, 0x00}, // BACKGROUND    observed: every screen's background
    {0x20, 0x28, 0x30}, // TEXT_EMPTY    observed: the "---" empty-step dashes
    {0x48, 0x50, 0x60}, // TEXT_INFO     observed: column headers, T>120, nav map
    {0x90, 0xB0, 0xB8}, // TEXT_DEFAULT  observed: field labels
    {0xF8, 0xFC, 0xF8}, // TEXT_VALUE    observed: field values     (was FFFFFF)
    {0xF8, 0x20, 0x40}, // TEXT_TITLES   observed: SONG/PHRASE/MIXER (was FF2040)
    {0x00, 0xFC, 0x60}, // PLAY_MARKERS  observed: the < > playhead  (was 00FF60)
    {0x00, 0xFC, 0xF8}, // CURSOR        observed: the cursor cell   (was 00FFFF)
    {0x00, 0xFC, 0x80}, // SELECTION     INFERRED                    (was 00FF80)
    {0x90, 0xB8, 0xB8}, // SCOPE_SLIDER  observed: INSTRUMENT scope
    {0x00, 0xFC, 0xE0}, // METER_LOW     INFERRED                    (was 00FFE0)
    {0xF8, 0xFC, 0x50}, // METER_MID     INFERRED                    (was FFFF50)
    {0xF8, 0x00, 0x80}  // METER_PEAK    INFERRED                    (was FF0080)
};

struct Theme {
    char name[13] = "DEFAULT-----";
    std::array<ThemeColor, static_cast<size_t>(ThemeSlot::COUNT)> colors = [] {
        std::array<ThemeColor, static_cast<size_t>(ThemeSlot::COUNT)> a{};
        for (size_t i = 0; i < a.size(); ++i) a[i] = kDefaultThemeColors[i];
        return a;
    }();

    void resetDefault() {
        std::strncpy(name, "DEFAULT-----", 12);
        name[12] = '\0';
        for (size_t i = 0; i < colors.size(); ++i) colors[i] = kDefaultThemeColors[i];
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
