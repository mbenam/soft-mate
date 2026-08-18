#include "SystemSettingsScreen.h"
#include "../../Theme.h"
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace m8 {
namespace ui {
namespace system_settings {

static std::string ToHex(int value) {
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (static_cast<unsigned int>(value) & 0xFF);
    return ss.str();
}

static std::string ResolveValue(CursorId id, const SystemSettingsState& state) {
    using C = CursorId;
    switch (id) {
        case C::BACKLIGHT: return ToHex(state.backlight);
        case C::FONT_OPTIONS: return state.fontUppercase ? "UPPERCASE" : "LOWERCASE";
        case C::THEME_EDIT: return "EDIT THEME";
        case C::NOTE_PREVIEW: return state.notePreview ? "ON" : "OFF";
        case C::REC_METRONOME: return state.recMetronome ? "01ON" : "00OFF";
        case C::METRONOME_VOL: return ToHex(state.metronomeVol);
        case C::SONG_TEMPLATE_SAVE: return "SAVE";
        case C::SONG_TEMPLATE_CLEAR: return "CLEAR";
        case C::USB_AUDIO_MODE: return "STEREO OUT";
        case C::USB_MAIN_OUT: return "MIX";
        case C::LINE_IN_GATE: return "OFF";
        case C::SPLASH_SCREEN: return state.splashScreen ? "ON" : "OFF";
        case C::HP_PROTECTION: return "ON";
        case C::KEY_DELAY_REP: return std::to_string(state.keyDelay) + ":" + std::to_string(state.keyRepeat);
        case C::BATT_STATUS: return "USB/DC";
        default: return "";
    }
}

void RenderSystemSettingsScreen(Renderer& renderer, const SystemSettingsState& state) {
    auto staticText = GetSystemSettingsStaticText();
    for (const auto& cell : staticText) {
        renderer.drawString(cell.text, cell.col, cell.row, GetThemeColor(cell.normal_color));
    }

    auto fields = GetSystemSettingsInteractiveFields();
    for (const auto& pair : fields) {
        CursorId id = pair.first;
        const auto& cells = pair.second;
        bool isSelected = (state.cursorId == id);

        for (const auto& cell : cells) {
            std::string text = cell.text;
            if (cell.role == "value") {
                text = ResolveValue(id, state);
            }

            SDL_Color color = isSelected ? GetThemeColor(cell.selected_color) : GetThemeColor(cell.normal_color);
            renderer.drawString(text, cell.col, cell.row, color);

            if (isSelected && cell.has_cursor_box) {
                renderer.drawBracket(cell.col, cell.row, text.length(), GetThemeColor("LABEL_LITE"));
            }
        }
    }
}

bool HandleSystemSettingsInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                              SystemSettingsState& state, ViewManager& viewManager) {
    using C = CursorId;
    if (event.type != SDL_EVENT_KEY_DOWN) return false;

    // Option key (Z) or Escape exits back to Project screen
    if (event.key.key == SDLK_Z || event.key.key == SDLK_ESCAPE) {
        return true;
    }

    // Open Theme screen from THEME EDIT field
    if ((event.key.key == SDLK_X || event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) && state.cursorId == C::THEME_EDIT) {
        viewManager.pushModal(ViewType::THEME_SETTINGS);
        return false;
    }

    if (editHeld && (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN || event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT)) {
        arrowPressedDuringEdit = true;
        int step = (event.key.key == SDLK_UP || event.key.key == SDLK_RIGHT) ? 1 : -1;

        if (state.cursorId == C::FONT_OPTIONS) {
            state.fontUppercase = !state.fontUppercase;
        } else if (state.cursorId == C::NOTE_PREVIEW) {
            state.notePreview = !state.notePreview;
        } else if (state.cursorId == C::REC_METRONOME) {
            state.recMetronome = state.recMetronome ? 0 : 1;
        } else if (state.cursorId == C::METRONOME_VOL) {
            state.metronomeVol = static_cast<uint8_t>(std::clamp(static_cast<int>(state.metronomeVol) + step * 0x10, 0, 255));
        } else if (state.cursorId == C::SPLASH_SCREEN) {
            state.splashScreen = !state.splashScreen;
        } else if (state.cursorId == C::KEY_DELAY_REP) {
            state.keyRepeat = std::clamp(state.keyRepeat + step * 5, 10, 200);
        }
        return false;
    }

    if (!editHeld) {
        auto navMap = GetSystemSettingsNavMap();
        if (navMap.count(state.cursorId)) {
            auto node = navMap[state.cursorId];
            if (event.key.key == SDLK_UP && node.up != C::NONE) state.cursorId = node.up;
            else if (event.key.key == SDLK_DOWN && node.down != C::NONE) state.cursorId = node.down;
            else if (event.key.key == SDLK_LEFT && node.left != C::NONE) state.cursorId = node.left;
            else if (event.key.key == SDLK_RIGHT && node.right != C::NONE) state.cursorId = node.right;
        }
    }

    return false;
}

} // namespace system_settings
} // namespace ui
} // namespace m8
