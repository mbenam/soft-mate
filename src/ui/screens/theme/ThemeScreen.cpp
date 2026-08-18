#include "ThemeScreen.h"
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace m8 {
namespace ui {
namespace theme {

static std::string ToHex(uint8_t value) {
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << static_cast<unsigned int>(value);
    return ss.str();
}

void RenderThemeScreen(Renderer& renderer, const ThemeScreenState& state) {
    // Title
    renderer.drawString("THEME SETTINGS", 0, 0, GetThemeColor("TITLE"));

    // Header Mode row
    renderer.drawString("MODE", 0, 1, GetThemeColor("LABEL_DIM"));
    renderer.drawString("R", 14, 1, GetThemeColor("LABEL_DIM"));
    renderer.drawString("G", 17, 1, GetThemeColor("LABEL_DIM"));
    renderer.drawString("B", 20, 1, GetThemeColor("LABEL_DIM"));
    renderer.drawString("<>", 23, 1, GetThemeColor("LABEL_DIM"));
    if (state.cursorRow == 0) {
        renderer.drawBracket(0, 1, 4, GetThemeColor("LABEL_LITE"));
    }

    // 13 Color Rows
    for (size_t i = 0; i < sizeof(kThemeSlotInfos) / sizeof(kThemeSlotInfos[0]); ++i) {
        const auto& info = kThemeSlotInfos[i];
        int row = info.row;
        bool isRowActive = (state.cursorRow == static_cast<int>(i + 1));

        SDL_Color labelColor = isRowActive ? GetThemeColor("LABEL_LITE") : GetThemeColor("LABEL_DIM");
        renderer.drawString(info.label, 0, row, labelColor);

        const ThemeColor& col = g_currentTheme.colors[static_cast<size_t>(info.slot)];
        std::string rStr = ToHex(col.r);
        std::string gStr = ToHex(col.g);
        std::string bStr = ToHex(col.b);

        renderer.drawString(rStr, 14, row, isRowActive && state.cursorCol == 0 ? GetThemeColor("LABEL_LITE") : GetThemeColor("VALUE"));
        renderer.drawString(gStr, 17, row, isRowActive && state.cursorCol == 1 ? GetThemeColor("LABEL_LITE") : GetThemeColor("VALUE"));
        renderer.drawString(bStr, 20, row, isRowActive && state.cursorCol == 2 ? GetThemeColor("LABEL_LITE") : GetThemeColor("VALUE"));

        if (isRowActive) {
            int colX = (state.cursorCol == 0) ? 14 : (state.cursorCol == 1) ? 17 : 20;
            renderer.drawBracket(colX, row, 2, GetThemeColor("LABEL_LITE"));
        }
    }

    // Theme Name
    bool isNameActive = (state.cursorRow == 14);
    renderer.drawString("THEME NAME", 0, 16, isNameActive ? GetThemeColor("LABEL_LITE") : GetThemeColor("LABEL_DIM"));
    renderer.drawString(g_currentTheme.name, 14, 16, isNameActive ? GetThemeColor("LABEL_LITE") : GetThemeColor("VALUE"));
    if (isNameActive) {
        renderer.drawBracket(14 + state.nameCharIndex, 16, 1, GetThemeColor("LABEL_LITE"));
    }

    // Action buttons
    bool isActionsActive = (state.cursorRow == 15);
    renderer.drawString("LOAD", 14, 17, isActionsActive && state.cursorCol == 0 ? GetThemeColor("LABEL_LITE") : GetThemeColor("VALUE"));
    renderer.drawString("SAVE", 19, 17, isActionsActive && state.cursorCol == 1 ? GetThemeColor("LABEL_LITE") : GetThemeColor("VALUE"));
    renderer.drawString("RESET", 24, 17, isActionsActive && state.cursorCol == 2 ? GetThemeColor("LABEL_LITE") : GetThemeColor("VALUE"));

    if (isActionsActive) {
        int x = (state.cursorCol == 0) ? 14 : (state.cursorCol == 1) ? 19 : 24;
        int len = (state.cursorCol == 2) ? 5 : 4;
        renderer.drawBracket(x, 17, len, GetThemeColor("LABEL_LITE"));
    }
}

bool HandleThemeInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                      ThemeScreenState& state, ViewManager& viewManager) {
    (void)viewManager;
    if (event.type != SDL_EVENT_KEY_DOWN) return false;

    // Option key (Z) or Escape exits back to System Settings
    if (event.key.key == SDLK_Z || event.key.key == SDLK_ESCAPE) {
        return true;
    }

    // Action buttons trigger
    if ((event.key.key == SDLK_X || event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) && state.cursorRow == 15) {
        if (state.cursorCol == 2) {
            g_currentTheme.resetDefault();
        }
        return false;
    }

    if (editHeld && (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN || event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT)) {
        arrowPressedDuringEdit = true;
        int step = (event.key.key == SDLK_UP || event.key.key == SDLK_RIGHT) ? 1 : -1;
        bool largeStep = (event.key.key == SDLK_RIGHT || event.key.key == SDLK_LEFT);
        int delta = largeStep ? step * 0x10 : step;

        if (state.cursorRow >= 1 && state.cursorRow <= 13) {
            size_t slotIdx = static_cast<size_t>(state.cursorRow - 1);
            ThemeColor& col = g_currentTheme.colors[slotIdx];
            if (state.cursorCol == 0) col.r = static_cast<uint8_t>(std::clamp(static_cast<int>(col.r) + delta, 0, 255));
            else if (state.cursorCol == 1) col.g = static_cast<uint8_t>(std::clamp(static_cast<int>(col.g) + delta, 0, 255));
            else if (state.cursorCol == 2) col.b = static_cast<uint8_t>(std::clamp(static_cast<int>(col.b) + delta, 0, 255));
        }
        return false;
    }

    if (!editHeld) {
        if (event.key.key == SDLK_UP) {
            state.cursorRow = std::max(0, state.cursorRow - 1);
        } else if (event.key.key == SDLK_DOWN) {
            state.cursorRow = std::min(15, state.cursorRow + 1);
        } else if (event.key.key == SDLK_LEFT) {
            if (state.cursorRow == 14) {
                state.nameCharIndex = (state.nameCharIndex + 11) % 12;
            } else {
                state.cursorCol = std::max(0, state.cursorCol - 1);
            }
        } else if (event.key.key == SDLK_RIGHT) {
            if (state.cursorRow == 14) {
                state.nameCharIndex = (state.nameCharIndex + 1) % 12;
            } else {
                state.cursorCol = std::min(2, state.cursorCol + 1);
            }
        }
    }

    return false;
}

} // namespace theme
} // namespace ui
} // namespace m8
