#include "RenderScreen.h"
#include "../project/ProjectScreen.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace m8 {
namespace ui {
namespace render {

namespace {

static SDL_Color GetColorFromString(const std::string& colorName) {
    if (colorName == "TITLE") return {255, 60, 60, 255}; 
    if (colorName == "LABEL_DIM") return {100, 100, 100, 255}; 
    if (colorName == "LABEL_LITE") return {0, 255, 255, 255}; 
    if (colorName == "VALUE") return {255, 255, 255, 255}; 
    if (colorName == "ACCENT") return {255, 60, 60, 255}; 
    return {255, 255, 255, 255};
}

static std::string Hex2(int val) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02X", val & 0xFF);
    return std::string(buf);
}

} // namespace

void RenderRenderScreen(Renderer& renderer, const RenderScreenState& state) {
    // 1. Draw static text
    for (const auto& item : GetRenderStaticText()) {
        renderer.drawString(item.text, item.col, item.row, GetColorFromString(item.normal_color));
    }

    // 2. Draw interactive fields
    using C = CursorId;
    auto fields = GetRenderInteractiveFields();

    for (const auto& [fieldId, components] : fields) {
        bool isActive = (state.cursorId == fieldId);

        for (const auto& comp : components) {
            SDL_Color color = GetColorFromString(isActive ? comp.selected_color : comp.normal_color);
            std::string drawText = comp.text;

            if (comp.role == "value") {
                switch (fieldId) {
                    case C::SONG_ROW_START:
                        drawText = Hex2(state.settings.songRowStart);
                        break;
                    case C::SONG_ROW_LAST:
                        drawText = (state.settings.songRowLast < 0) ? "--" : Hex2(state.settings.songRowLast);
                        break;
                    case C::REPEAT_SONG:
                        drawText = Hex2(state.settings.repeatCount);
                        break;
                    case C::TRACK_1: drawText = state.settings.trackEnabled[0] ? "ON" : "--"; break;
                    case C::TRACK_2: drawText = state.settings.trackEnabled[1] ? "ON" : "--"; break;
                    case C::TRACK_3: drawText = state.settings.trackEnabled[2] ? "ON" : "--"; break;
                    case C::TRACK_4: drawText = state.settings.trackEnabled[3] ? "ON" : "--"; break;
                    case C::TRACK_5: drawText = state.settings.trackEnabled[4] ? "ON" : "--"; break;
                    case C::TRACK_6: drawText = state.settings.trackEnabled[5] ? "ON" : "--"; break;
                    case C::TRACK_7: drawText = state.settings.trackEnabled[6] ? "ON" : "--"; break;
                    case C::TRACK_8: drawText = state.settings.trackEnabled[7] ? "ON" : "--"; break;
                    case C::MODFX:   drawText = state.settings.modfxEnabled ? "ON" : "--"; break;
                    case C::DELAY:   drawText = state.settings.delayEnabled ? "ON" : "--"; break;
                    case C::REVERB:  drawText = state.settings.reverbEnabled ? "ON" : "--"; break;
                    case C::LIMITER: drawText = state.settings.limiterEnabled ? "ON" : "--"; break;
                    case C::MIX_EQ:  drawText = state.settings.mixEqEnabled ? "ON" : "--"; break;
                    case C::MODE:    drawText = state.settings.is32Bit ? "32-BIT" : "16-BIT"; break;
                    case C::NAME: {
                        std::string nm = state.settings.name;
                        while (nm.size() < 18) nm += '-';
                        drawText = nm;
                        break;
                    }
                    case C::RENDER_MIXED: drawText = "MIXED"; break;
                    case C::RENDER_STEMS: drawText = "STEMS"; break;
                    default: break;
                }
            } else if (comp.role == "accent") {
                if (fieldId == C::SONG_ROW_LAST) {
                    drawText = (state.settings.songRowLast < 0) ? "AUTO" : "    ";
                } else if (fieldId == C::REPEAT_SONG) {
                    drawText = (state.settings.repeatCount == 0) ? "OFF" : "   ";
                }
            }

            if (!drawText.empty()) {
                renderer.drawString(drawText, comp.col, comp.row, color);
            }

            if (isActive && comp.has_cursor_box && comp.role == "value") {
                if (fieldId == C::NAME) {
                    bool isBlinkOn = ((SDL_GetTicks() / 500) % 2) == 0;
                    if (isBlinkOn) {
                        int clampedIdx = std::clamp(state.nameCharIndex, 0, 11);
                        renderer.drawBracket(comp.col + clampedIdx, comp.row, 1, {0, 255, 255, 255});
                    }
                } else {
                    renderer.drawBracket(comp.col, comp.row, static_cast<int>(drawText.length()), {0, 255, 255, 255});
                }
            }
        }
    }

    // 3. Status message
    if (!state.settings.statusMsg.empty() && SDL_GetTicks() < state.settings.statusExpiry) {
        renderer.drawString(state.settings.statusMsg, 0, 19, GetColorFromString("TITLE"));
    }
}

void HandleRenderInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                       RenderScreenState& state, const engine::Sequencer& uiSequencer,
                       const engine::EngineState& uiEngineState, ViewManager& viewManager,
                       CharPicker& charPicker) {
    using C = CursorId;

    if (event.type != SDL_EVENT_KEY_DOWN) return;

    // Option key (Z) or Escape
    if (event.key.key == SDLK_Z || event.key.key == SDLK_ESCAPE) {
        if (editHeld) {
            // EDIT + OPTION: Reset/Delete selected parameter
            switch (state.cursorId) {
                case C::SONG_ROW_START: state.settings.songRowStart = 0; break;
                case C::SONG_ROW_LAST:  state.settings.songRowLast = -1; break; // AUTO
                case C::REPEAT_SONG:    state.settings.repeatCount = 0; break;   // OFF
                case C::TRACK_1: state.settings.trackEnabled[0] = true; break;
                case C::TRACK_2: state.settings.trackEnabled[1] = true; break;
                case C::TRACK_3: state.settings.trackEnabled[2] = true; break;
                case C::TRACK_4: state.settings.trackEnabled[3] = true; break;
                case C::TRACK_5: state.settings.trackEnabled[4] = true; break;
                case C::TRACK_6: state.settings.trackEnabled[5] = true; break;
                case C::TRACK_7: state.settings.trackEnabled[6] = true; break;
                case C::TRACK_8: state.settings.trackEnabled[7] = true; break;
                case C::MODFX:   state.settings.modfxEnabled = true; break;
                case C::DELAY:   state.settings.delayEnabled = true; break;
                case C::REVERB:  state.settings.reverbEnabled = true; break;
                case C::LIMITER: state.settings.limiterEnabled = true; break;
                case C::MIX_EQ:  state.settings.mixEqEnabled = true; break;
                case C::MODE:    state.settings.is32Bit = false; break;
                case C::NAME:    std::strncpy(state.settings.name, "PROBE_SELFTE", sizeof(state.settings.name) - 1); break;
                default: break;
            }
            return;
        } else {
            // OPTION: Exits view
            viewManager.popModal();
            return;
        }
    }

    if (!editHeld) {
        auto navMap = GetRenderNavMap();
        auto it = navMap.find(state.cursorId);

        if (event.key.key == SDLK_RIGHT) {
            if (state.cursorId == C::NAME) {
                if (state.nameCharIndex < 11) state.nameCharIndex++;
            } else if (it != navMap.end() && it->second.right != C::NONE) {
                state.cursorId = it->second.right;
            }
        } else if (event.key.key == SDLK_LEFT) {
            if (state.cursorId == C::NAME) {
                if (state.nameCharIndex > 0) state.nameCharIndex--;
            } else if (it != navMap.end() && it->second.left != C::NONE) {
                state.cursorId = it->second.left;
            }
        } else if (event.key.key == SDLK_UP) {
            if (it != navMap.end() && it->second.up != C::NONE) state.cursorId = it->second.up;
        } else if (event.key.key == SDLK_DOWN) {
            if (it != navMap.end() && it->second.down != C::NONE) state.cursorId = it->second.down;
        }

        if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
            if (state.cursorId == C::NAME) {
                char curChar = (state.nameCharIndex < (int)std::strlen(state.settings.name)) ? state.settings.name[state.nameCharIndex] : 'P';
                charPicker.init(curChar);
                viewManager.pushModal(ViewType::CHAR_PICKER);
            } else if (state.cursorId == C::RENDER_MIXED) {
                state.settings.sampleRoot = project::getSampleRoot();
                auto res = io::RenderSongAudio(state.settings, uiSequencer, uiEngineState, false);
                if (res.ok) {
                    state.settings.statusMsg = "RENDERED: " + res.outputFiles[0];
                } else {
                    state.settings.statusMsg = "RENDER FAILED: " + res.errorMsg;
                }
                state.settings.statusExpiry = SDL_GetTicks() + 4000;
            } else if (state.cursorId == C::RENDER_STEMS) {
                state.settings.sampleRoot = project::getSampleRoot();
                auto res = io::RenderSongAudio(state.settings, uiSequencer, uiEngineState, true);
                if (res.ok) {
                    state.settings.statusMsg = "STEMS RENDERED (" + std::to_string(res.outputFiles.size()) + " FILES)";
                } else {
                    state.settings.statusMsg = "STEMS FAILED: " + res.errorMsg;
                }
                state.settings.statusExpiry = SDL_GetTicks() + 4000;
            }
        }
    } else {
        // Edit is held
        if (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT) {
            arrowPressedDuringEdit = true;
            int dir = (event.key.key == SDLK_RIGHT) ? 1 : -1;

            switch (state.cursorId) {
                case C::SONG_ROW_START: {
                    int val = state.settings.songRowStart + dir;
                    state.settings.songRowStart = (val < 0) ? 255 : (val > 255) ? 0 : val;
                    break;
                }
                case C::SONG_ROW_LAST: {
                    if (state.settings.songRowLast < 0) {
                        state.settings.songRowLast = (dir < 0) ? 255 : 0;
                    } else {
                        int val = state.settings.songRowLast + dir;
                        if (val < 0) state.settings.songRowLast = -1; // AUTO
                        else if (val > 255) state.settings.songRowLast = -1; // AUTO
                        else state.settings.songRowLast = val;
                    }
                    break;
                }
                case C::REPEAT_SONG: {
                    state.settings.repeatCount = std::clamp(state.settings.repeatCount + dir, 0, 99);
                    break;
                }
                case C::TRACK_1: state.settings.trackEnabled[0] = !state.settings.trackEnabled[0]; break;
                case C::TRACK_2: state.settings.trackEnabled[1] = !state.settings.trackEnabled[1]; break;
                case C::TRACK_3: state.settings.trackEnabled[2] = !state.settings.trackEnabled[2]; break;
                case C::TRACK_4: state.settings.trackEnabled[3] = !state.settings.trackEnabled[3]; break;
                case C::TRACK_5: state.settings.trackEnabled[4] = !state.settings.trackEnabled[4]; break;
                case C::TRACK_6: state.settings.trackEnabled[5] = !state.settings.trackEnabled[5]; break;
                case C::TRACK_7: state.settings.trackEnabled[6] = !state.settings.trackEnabled[6]; break;
                case C::TRACK_8: state.settings.trackEnabled[7] = !state.settings.trackEnabled[7]; break;
                case C::MODFX:   state.settings.modfxEnabled = !state.settings.modfxEnabled; break;
                case C::DELAY:   state.settings.delayEnabled = !state.settings.delayEnabled; break;
                case C::REVERB:  state.settings.reverbEnabled = !state.settings.reverbEnabled; break;
                case C::LIMITER: state.settings.limiterEnabled = !state.settings.limiterEnabled; break;
                case C::MIX_EQ:  state.settings.mixEqEnabled = !state.settings.mixEqEnabled; break;
                case C::MODE:    state.settings.is32Bit = !state.settings.is32Bit; break;
                default: break;
            }
        } else if (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN) {
            arrowPressedDuringEdit = true;
            int dir = (event.key.key == SDLK_UP) ? 1 : -1;

            switch (state.cursorId) {
                case C::SONG_ROW_START: {
                    int val = state.settings.songRowStart + dir * 16;
                    state.settings.songRowStart = (val < 0) ? (val + 256) : (val > 255) ? (val - 256) : val;
                    break;
                }
                case C::SONG_ROW_LAST: {
                    if (state.settings.songRowLast < 0) {
                        state.settings.songRowLast = (dir < 0) ? 240 : 0;
                    } else {
                        int val = state.settings.songRowLast + dir * 16;
                        if (val < 0 || val > 255) state.settings.songRowLast = -1; // AUTO
                        else state.settings.songRowLast = val;
                    }
                    break;
                }
                case C::REPEAT_SONG: {
                    state.settings.repeatCount = std::clamp(state.settings.repeatCount + dir * 10, 0, 99);
                    break;
                }
                default: break;
            }
        }
    }
}

void HandleRenderEditRelease(RenderScreenState& state, const engine::Sequencer& uiSequencer,
                             const engine::EngineState& uiEngineState, ViewManager& viewManager,
                             CharPicker& charPicker) {
    (void)charPicker;
    (void)viewManager;
    using C = CursorId;
    if (state.cursorId == C::RENDER_MIXED) {
        state.settings.sampleRoot = project::getSampleRoot();
        auto res = io::RenderSongAudio(state.settings, uiSequencer, uiEngineState, false);
        if (res.ok) {
            state.settings.statusMsg = "RENDERED: " + res.outputFiles[0];
        } else {
            state.settings.statusMsg = "RENDER FAILED: " + res.errorMsg;
        }
        state.settings.statusExpiry = SDL_GetTicks() + 4000;
    } else if (state.cursorId == C::RENDER_STEMS) {
        state.settings.sampleRoot = project::getSampleRoot();
        auto res = io::RenderSongAudio(state.settings, uiSequencer, uiEngineState, true);
        if (res.ok) {
            state.settings.statusMsg = "STEMS RENDERED (" + std::to_string(res.outputFiles.size()) + " FILES)";
        } else {
            state.settings.statusMsg = "STEMS FAILED: " + res.errorMsg;
        }
        state.settings.statusExpiry = SDL_GetTicks() + 4000;
    }
}

} // namespace render
} // namespace ui
} // namespace m8
