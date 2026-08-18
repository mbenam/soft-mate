#pragma once
#include "../../Renderer.h"
#include "../../ViewManager.h"
#include "../../FileBrowser.h"
#include "../../Theme.h"
#include "ThemeScreenLayout.h"
#include <SDL3/SDL.h>

namespace m8 {
namespace ui {
namespace theme {

enum class ThemeBrowserMode {
    NONE,
    LOAD,
    SAVE
};

struct ThemeScreenState {
    int cursorRow = 1; // 0=MODE, 1..13=Colors, 14=NAME, 15=LOAD/SAVE/RESET
    int cursorCol = 0; // 0=H/R, 1=S/G, 2=V/B (for colors), or 0=MODE/LOAD, 1=NUDGE/SAVE, 2=RESET
    bool isHsv = false; // Toggle between RGB and HSV modes
    int nameCharIndex = 0;
};

struct ThemeActionState {
    FileBrowser& fileBrowser;
    ViewManager& viewManager;
    ThemeBrowserMode& themeBrowserMode;
    bool& textInputActive;
    std::string& textInputBuffer;
    std::string& textInputPrompt;
};

inline ThemeScreenState g_themeScreenState;

void RenderThemeScreen(Renderer& renderer, const ThemeScreenState& state);
bool HandleThemeInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                      ThemeScreenState& state, ViewManager& viewManager, ThemeActionState* actions = nullptr);
void HandleThemeEditRelease(ThemeScreenState& state, ThemeActionState* actions = nullptr);

} // namespace theme
} // namespace ui
} // namespace m8
