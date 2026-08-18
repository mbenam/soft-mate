#include <catch2/catch_test_macros.hpp>
#include "ui/screens/system_settings/SystemSettingsScreen.h"
#include "ui/screens/theme/ThemeScreen.h"
#include "ui/Theme.h"
#include "ui/ViewManager.h"

using namespace m8::ui;

TEST_CASE("System Settings: Navigation and Active Value Edits", "[ui]") {
    system_settings::SystemSettingsState state;
    ViewManager vm;
    bool arrow = false;

    // Default cursor is FONT_OPTIONS
    REQUIRE(state.fontUppercase == true);

    // Edit FONT_OPTIONS -> toggles to lowercase
    SDL_Event editEv{};
    editEv.type = SDL_EVENT_KEY_DOWN;
    editEv.key.key = SDLK_RIGHT;
    system_settings::HandleSystemSettingsInput(editEv, true, arrow, state, vm);
    REQUIRE(state.fontUppercase == false);

    // Navigate DOWN to THEME_EDIT
    SDL_Event downEv{};
    downEv.type = SDL_EVENT_KEY_DOWN;
    downEv.key.key = SDLK_DOWN;
    system_settings::HandleSystemSettingsInput(downEv, false, arrow, state, vm);
    REQUIRE(state.cursorId == system_settings::CursorId::THEME_EDIT);

    // Press X on THEME_EDIT -> pushes THEME_SETTINGS modal
    SDL_Event enterEv{};
    enterEv.type = SDL_EVENT_KEY_DOWN;
    enterEv.key.key = SDLK_X;
    system_settings::HandleSystemSettingsInput(enterEv, false, arrow, state, vm);
    REQUIRE(vm.getCurrentView() == ViewType::THEME_SETTINGS);

    // Navigate to NOTE_PREVIEW and toggle
    state.cursorId = system_settings::CursorId::NOTE_PREVIEW;
    REQUIRE(state.notePreview == true);
    system_settings::HandleSystemSettingsInput(editEv, true, arrow, state, vm);
    REQUIRE(state.notePreview == false);

    // Navigate to REC_METRONOME and toggle
    state.cursorId = system_settings::CursorId::REC_METRONOME;
    REQUIRE(state.recMetronome == 0);
    system_settings::HandleSystemSettingsInput(editEv, true, arrow, state, vm);
    REQUIRE(state.recMetronome == 1);

    // Option key (Z) exits System Settings
    SDL_Event optEv{};
    optEv.type = SDL_EVENT_KEY_DOWN;
    optEv.key.key = SDLK_Z;
    bool shouldExit = system_settings::HandleSystemSettingsInput(optEv, false, arrow, state, vm);
    REQUIRE(shouldExit == true);
}

TEST_CASE("Theme Settings: Navigation, Color Edits, and Reset", "[ui]") {
    theme::ThemeScreenState state;
    ViewManager vm;
    bool arrow = false;

    g_currentTheme.resetDefault();
    REQUIRE(g_currentTheme.colors[0].r == 0x00);
    REQUIRE(g_currentTheme.colors[0].g == 0x00);
    REQUIRE(g_currentTheme.colors[0].b == 0x00);

    // Cursor on row 1 (BACKGROUND), col 0 (R)
    state.cursorRow = 1;
    state.cursorCol = 0;

    // Edit R component
    SDL_Event editEv{};
    editEv.type = SDL_EVENT_KEY_DOWN;
    editEv.key.key = SDLK_UP;
    theme::HandleThemeInput(editEv, true, arrow, state, vm);
    REQUIRE(g_currentTheme.colors[0].r == 0x01);

    // Navigate to col 1 (G)
    SDL_Event rightEv{};
    rightEv.type = SDL_EVENT_KEY_DOWN;
    rightEv.key.key = SDLK_RIGHT;
    theme::HandleThemeInput(rightEv, false, arrow, state, vm);
    REQUIRE(state.cursorCol == 1);

    // Edit G component
    theme::HandleThemeInput(editEv, true, arrow, state, vm);
    REQUIRE(g_currentTheme.colors[0].g == 0x01);

    // Navigate to RESET button (row 15, col 2)
    state.cursorRow = 15;
    state.cursorCol = 2;

    // Press X to reset theme defaults
    SDL_Event resetEv{};
    resetEv.type = SDL_EVENT_KEY_DOWN;
    resetEv.key.key = SDLK_X;
    theme::HandleThemeInput(resetEv, false, arrow, state, vm);
    REQUIRE(g_currentTheme.colors[0].r == 0x00);
    REQUIRE(g_currentTheme.colors[0].g == 0x00);

    // Navigate to MODE row (row 0, col 0)
    state.cursorRow = 0;
    state.cursorCol = 0;
    REQUIRE(state.isHsv == false);

    // Press X to toggle to HSV mode
    SDL_Event enterEv{};
    enterEv.type = SDL_EVENT_KEY_DOWN;
    enterEv.key.key = SDLK_X;
    theme::HandleThemeInput(enterEv, false, arrow, state, vm);
    REQUIRE(state.isHsv == true);

    // Navigate to col 1 (Nudge <>)
    theme::HandleThemeInput(rightEv, false, arrow, state, vm);
    REQUIRE(state.cursorCol == 1);

    // Edit Nudge <> with Right arrow (large step +0x10)
    uint8_t oldG5 = g_currentTheme.colors[5].g;
    theme::HandleThemeInput(rightEv, true, arrow, state, vm);
    // Highly saturated slot 5 (TEXT_TITLES) shifted hue
    REQUIRE(g_currentTheme.colors[5].g != oldG5);

    // Edit Theme Name
    state.cursorRow = 14;
    state.nameCharIndex = 0;
    g_currentTheme.name[0] = 'D';
    theme::HandleThemeInput(editEv, true, arrow, state, vm);
    REQUIRE(g_currentTheme.name[0] == 'E');

    // Test HandleThemeEditRelease on RESET
    state.cursorRow = 15;
    state.cursorCol = 2;
    g_currentTheme.colors[0].r = 0x55;
    theme::HandleThemeEditRelease(state, nullptr);
    REQUIRE(g_currentTheme.colors[0].r == 0x00);

    // Option key (Z) exits Theme screen
    SDL_Event optEv{};
    optEv.type = SDL_EVENT_KEY_DOWN;
    optEv.key.key = SDLK_Z;
    bool shouldExit = theme::HandleThemeInput(optEv, false, arrow, state, vm);
    REQUIRE(shouldExit == true);
}

