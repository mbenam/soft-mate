#pragma once
#include "../../Renderer.h"
#include "../../ViewManager.h"
#include "SystemSettingsScreenLayout.h"
#include <SDL3/SDL.h>

namespace m8 {
namespace ui {
namespace system_settings {

struct SystemSettingsState {
    uint8_t backlight = 0xC0;
    bool fontUppercase = true;
    bool notePreview = true;
    uint8_t recMetronome = 0; // 0=OFF, 1=ON
    uint8_t metronomeVol = 0xE0;
    bool splashScreen = true;
    int keyDelay = 150;
    int keyRepeat = 45;
    CursorId cursorId = CursorId::FONT_OPTIONS;
};

inline SystemSettingsState g_systemSettingsState;

void RenderSystemSettingsScreen(Renderer& renderer, const SystemSettingsState& state);
bool HandleSystemSettingsInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                              SystemSettingsState& state, ViewManager& viewManager);

} // namespace system_settings
} // namespace ui
} // namespace m8
