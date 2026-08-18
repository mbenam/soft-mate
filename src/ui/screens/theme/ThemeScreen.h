#pragma once
#include "../../Renderer.h"
#include "../../ViewManager.h"
#include "../../Theme.h"
#include "ThemeScreenLayout.h"
#include <SDL3/SDL.h>

namespace m8 {
namespace ui {
namespace theme {

struct ThemeScreenState {
    int cursorRow = 1; // 0=MODE, 1..13=Colors, 14=NAME, 15=LOAD/SAVE/RESET
    int cursorCol = 0; // 0=R, 1=G, 2=B (for colors), or 0=LOAD, 1=SAVE, 2=RESET
    int nameCharIndex = 0;
};

inline ThemeScreenState g_themeScreenState;

void RenderThemeScreen(Renderer& renderer, const ThemeScreenState& state);
bool HandleThemeInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                      ThemeScreenState& state, ViewManager& viewManager);

} // namespace theme
} // namespace ui
} // namespace m8
