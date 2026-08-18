#include "ProjectScreen.h"
#include "ProjectScreenLayout.h"
#include "../../UiEditHelpers.h"
#include <iomanip>
#include <sstream>

namespace m8 {
namespace ui {
namespace project {

static std::string g_sampleRoot = "Samples";
static uint64_t g_transposeNoticeUntil = 0;

void setSampleRoot(const std::string& root) { g_sampleRoot = root; }
const std::string& getSampleRoot() { return g_sampleRoot; }

static SDL_Color GetColorFromString(const std::string& colorName) {
    if (colorName == "TITLE") return {255, 60, 60, 255}; 
    if (colorName == "LABEL_DIM") return {100, 100, 100, 255}; 
    if (colorName == "LABEL_LITE") return {0, 255, 255, 255}; 
    if (colorName == "VALUE") return {255, 255, 255, 255}; 
    if (colorName == "ACCENT") return {255, 60, 60, 255}; 
    return {255, 255, 255, 255};
}

static std::string ToHex(int value) {
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (static_cast<unsigned int>(value) & 0xFF);
    return ss.str();
}

static bool IsBlankName(const char* name) {
    if (!name || name[0] == '\0') return true;
    for (int i = 0; i < 12 && name[i] != '\0'; ++i) {
        if (name[i] != ' ' && name[i] != '-') return false;
    }
    return true;
}

static std::string ResolveProjectValue(CursorId fieldId, const engine::EngineState& state) {
    const engine::ProjectSettings& proj = state.project;
    using C = CursorId;

    if (fieldId == C::TEMPO_INT) return std::to_string(state.bpm);
    if (fieldId == C::TEMPO_DEC) {
        std::stringstream ss;
        ss << std::setfill('0') << std::setw(2) << state.bpm_frac;
        return ss.str();
    }
    if (fieldId == C::TEMPO_NUDGE) return "< >";
    if (fieldId == C::TRANSPOSE) return ToHex(proj.transpose);

    if (fieldId == C::GROOVE) return ToHex(proj.groove);
    if (fieldId == C::SCALE) return ToHex(proj.scale);
    if (fieldId == C::LIVE_QUANTIZE) return ToHex(proj.live_quantize);

    if (fieldId == C::NAME) {
        std::string s = proj.name;
        if (IsBlankName(proj.name)) s = "------------";
        while (s.length() < 12) s += '-';
        if (s.length() > 12) s = s.substr(0, 12);
        return s;
    }

    // Action Fields
    if (fieldId == C::MIDI_SETTINGS) return "SETTINGS";
    if (fieldId == C::MIDI_MAPPINGS) return "MAPPINGS";
    if (fieldId == C::PROJ_LOAD) return "LOAD";
    if (fieldId == C::PROJ_SAVE) return "SAVE";
    if (fieldId == C::PROJ_NEW) return "NEW";
    if (fieldId == C::EXPORT_RENDER) return "RENDER";
    if (fieldId == C::EXPORT_BUNDLE) return "BUNDLE";
    if (fieldId == C::CLEAR_PHRASES) return "PHRASES";
    if (fieldId == C::CLEAR_INST) return "INST/TBL";
    if (fieldId == C::INST_POOL) return "VIEW INST.POOL";
    if (fieldId == C::TIME_STATS) return "VIEW TIME STATS";
    if (fieldId == C::SYSTEM_SETTINGS) return "SETTINGS";
    if (fieldId == C::SAMPLE_ROOT) return g_sampleRoot.empty() ? "(none)" : g_sampleRoot;

    return "--";
}

void RenderProjectScreen(Renderer& renderer,
                         const engine::EngineState& engState,
                         CursorId active_cursor_id,
                         int nameCharIndex) {

    static std::vector<UI_GridCell> staticText = GetProjectStaticText();
    static std::vector<UI_GridCell> dynamicText = GetProjectDynamicTextDefaults();
    static std::unordered_map<CursorId, std::vector<UI_GridCell>> interactiveFields = GetProjectInteractiveFields();

    // 1. Draw Static Text
    for (const auto& cell : staticText) {
        renderer.drawString(cell.text, cell.col, cell.row, GetColorFromString(cell.normal_color));
    }

    // 2. Draw Dynamic Text
    for (const auto& cell : dynamicText) {
        std::string textToDraw = cell.text;
        if (cell.text.substr(0, 2) == "T>") {
            textToDraw = "T>" + std::to_string(engState.bpm);
        }
        renderer.drawString(textToDraw, cell.col, cell.row, GetColorFromString(cell.normal_color));
    }

    bool isBlinkOn = ((SDL_GetTicks() / 250) % 2 == 0);

    // 3. Render Interactive Fields
    for (const auto& [fieldId, components] : interactiveFields) {
        bool isActive = (fieldId == active_cursor_id);
        std::string liveText = ResolveProjectValue(fieldId, engState);

        for (const auto& comp : components) {
            SDL_Color color = GetColorFromString(isActive ? comp.selected_color : comp.normal_color);

            std::string drawText = (comp.role == "value" || comp.role == "accent") ? liveText : comp.text;
            
            // Draw dimmed dashes for blank song name when not active
            if (fieldId == CursorId::NAME && comp.role == "value" && IsBlankName(engState.project.name)) {
                drawText = "------------";
                if (!isActive) {
                    color = GetColorFromString("LABEL_DIM");
                }
            }

            // Draw logic for accented contextual text (e.g. "DEFAULT", " C", "A#1")
            if (comp.role == "accent") {
                if (fieldId == CursorId::GROOVE) {
                    drawText = (engState.project.groove == 0) ? "DEFAULT" : "       ";
                } else if (fieldId == CursorId::SCALE) {
                    // The SCALE row reads "<index> <key> <scale name>". The key
                    // is NOT derived from the index byte -- this used to index a
                    // key table with `scale & 0x0F`, which showed G# for scale
                    // 08 where a device shows C. Measured on fw 6.5.2: stepping
                    // the index from 00 to 08 left the key at C and moved the
                    // name to MINOR PENTATON.
                    static const char* kKeyNames[12] = {
                        " C", "C#", " D", "D#", " E", " F",
                        "F#", " G", "G#", " A", "A#", " B"
                    };
                    const int idx = engState.project.scale & 0x0F;
                    drawText = kKeyNames[engState.project.key % 12];
                    drawText += " ";
                    for (int i = 0; i < 16; ++i) {
                        const unsigned char c =
                            static_cast<unsigned char>(engState.scales[idx].name[i]);
                        if (c == 0x00 || c == 0xFF) break;
                        drawText += static_cast<char>(c);
                    }
                } else if (fieldId == CursorId::LIVE_QUANTIZE) {
                    drawText = (engState.project.live_quantize == 0) ? "CHAIN LEN" : "STEPS    ";
                } else {
                    drawText = "";
                }
                
                // If it resolves to empty, skip drawing
                if (drawText.empty()) continue; 
            }

            renderer.drawString(drawText, comp.col, comp.row, color);

            // Draw cyan bounding box on active values
            if (isActive && comp.has_cursor_box && comp.role == "value") {
                if (fieldId == CursorId::NAME) {
                    if (isBlinkOn) {
                        int clampedIdx = std::clamp(nameCharIndex, 0, 11);
                        renderer.drawBracket(comp.col + clampedIdx, comp.row, 1, {0, 255, 255, 255});
                    }
                } else {
                    renderer.drawBracket(comp.col, comp.row, drawText.length(), {0, 255, 255, 255});
                }
            }
        }
    }

    // Draw temporary "GLOBAL TRANSPOSE: <n>" notice at bottom
    if (SDL_GetTicks() < g_transposeNoticeUntil) {
        std::string msg = "GLOBAL TRANSPOSE: " + std::to_string(static_cast<int8_t>(engState.project.transpose));
        renderer.drawString(msg, 1, 28, {255, 255, 255, 255});
    }
}

void setTransposeNoticeUntil(uint64_t ticks) { g_transposeNoticeUntil = ticks; }
uint64_t getTransposeNoticeUntil() { return g_transposeNoticeUntil; }

static std::string GetSongInitialDir(const std::string& currentSongPath) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!currentSongPath.empty()) {
        fs::path p(currentSongPath);
        if (p.has_parent_path() && fs::is_directory(p.parent_path(), ec)) {
            return p.parent_path().string();
        }
    }
    if (fs::is_directory("songs", ec)) return "songs";
    if (fs::is_directory("Songs", ec)) return "Songs";
    return ".";
}

static bool g_nudgeActive = false;
static int g_preNudgeBpm = 120;

void HandleProjectInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                         engine::EngineState& uiEngineState, CursorId& cursor_id,
                         int& nameCharIndex, CommandSink& commandSink, ProjectActionState& actions) {
    using C = CursorId;
    auto navMap = GetProjectNavMap();
    if (event.key.key == SDLK_DOWN) {
        if (!editHeld && navMap.count(cursor_id) && navMap[cursor_id].down != C::NONE) {
            if (g_nudgeActive) {
                PushParam(commandSink, uiEngineState, m8::engine::ParamID::BPM_INT, g_preNudgeBpm);
                g_nudgeActive = false;
            }
            cursor_id = navMap[cursor_id].down;
        }
    } else if (event.key.key == SDLK_UP) {
        if (!editHeld && navMap.count(cursor_id) && navMap[cursor_id].up != C::NONE) {
            if (g_nudgeActive) {
                PushParam(commandSink, uiEngineState, m8::engine::ParamID::BPM_INT, g_preNudgeBpm);
                g_nudgeActive = false;
            }
            cursor_id = navMap[cursor_id].up;
        }
    } else if (event.key.key == SDLK_RIGHT) {
        if (cursor_id == C::NAME && !editHeld) {
            if (nameCharIndex < 11) {
                nameCharIndex++;
            }
        } else if (!editHeld && navMap.count(cursor_id) && navMap[cursor_id].right != C::NONE) {
            if (g_nudgeActive) {
                PushParam(commandSink, uiEngineState, m8::engine::ParamID::BPM_INT, g_preNudgeBpm);
                g_nudgeActive = false;
            }
            cursor_id = navMap[cursor_id].right;
        }
    } else if (event.key.key == SDLK_LEFT) {
        if (cursor_id == C::NAME && !editHeld) {
            if (nameCharIndex > 0) {
                nameCharIndex--;
            }
        } else if (!editHeld && navMap.count(cursor_id) && navMap[cursor_id].left != C::NONE) {
            if (g_nudgeActive) {
                PushParam(commandSink, uiEngineState, m8::engine::ParamID::BPM_INT, g_preNudgeBpm);
                g_nudgeActive = false;
            }
            cursor_id = navMap[cursor_id].left;
        }
    }

    if (editHeld) {
        if (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT) {
            arrowPressedDuringEdit = true;
            int step = (event.key.key == SDLK_RIGHT) ? 1 : -1;
            if (cursor_id == C::TEMPO_INT) {
                PushParam(commandSink, uiEngineState, m8::engine::ParamID::BPM_INT, std::clamp<int>(uiEngineState.bpm + step, 20, 400));
            } else if (cursor_id == C::TEMPO_DEC) {
                PushParam(commandSink, uiEngineState, m8::engine::ParamID::BPM_FRAC, std::clamp<int>(uiEngineState.bpm_frac + step, 0, 99));
            } else if (cursor_id == C::TEMPO_NUDGE) {
                if (!g_nudgeActive) {
                    g_nudgeActive = true;
                    g_preNudgeBpm = uiEngineState.bpm;
                }
                PushParam(commandSink, uiEngineState, m8::engine::ParamID::BPM_INT, std::clamp<int>(uiEngineState.bpm + step, 20, 400));
            } else if (cursor_id == C::TRANSPOSE) {
                uint8_t byteVal = static_cast<uint8_t>(uiEngineState.project.transpose);
                byteVal = static_cast<uint8_t>(byteVal + step);
                uiEngineState.project.transpose = static_cast<int8_t>(byteVal);
                g_transposeNoticeUntil = SDL_GetTicks() + 2500;
            } else if (cursor_id == C::GROOVE) {
                uiEngineState.project.groove = std::clamp<int>(uiEngineState.project.groove + step, 0, 31);
            } else if (cursor_id == C::SCALE) {
                int nextVal = uiEngineState.project.scale + step;
                uiEngineState.project.scale = (nextVal < 0) ? 255 : (nextVal > 255) ? 0 : nextVal;
            } else if (cursor_id == C::LIVE_QUANTIZE) {
                int nextVal = uiEngineState.project.live_quantize + step;
                uiEngineState.project.live_quantize = (nextVal < 0) ? 255 : (nextVal > 255) ? 0 : nextVal;
            }
        } else if (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN) {
            arrowPressedDuringEdit = true;
            int step = (event.key.key == SDLK_UP) ? 10 : -10;
            if (cursor_id == C::TEMPO_INT) {
                PushParam(commandSink, uiEngineState, m8::engine::ParamID::BPM_INT, std::clamp<int>(uiEngineState.bpm + step, 20, 400));
            } else if (cursor_id == C::TEMPO_DEC) {
                PushParam(commandSink, uiEngineState, m8::engine::ParamID::BPM_FRAC, std::clamp<int>(uiEngineState.bpm_frac + step, 0, 99));
            } else if (cursor_id == C::TRANSPOSE) {
                int octStep = (event.key.key == SDLK_UP) ? 12 : -12;
                uint8_t byteVal = static_cast<uint8_t>(uiEngineState.project.transpose);
                byteVal = static_cast<uint8_t>(byteVal + octStep);
                uiEngineState.project.transpose = static_cast<int8_t>(byteVal);
                g_transposeNoticeUntil = SDL_GetTicks() + 2500;
            } else if (cursor_id == C::GROOVE) {
                int hexStep = (event.key.key == SDLK_UP) ? 16 : -16;
                uiEngineState.project.groove = std::clamp<int>(uiEngineState.project.groove + hexStep, 0, 31);
            } else if (cursor_id == C::SCALE) {
                int hexStep = (event.key.key == SDLK_UP) ? 16 : -16;
                int nextVal = uiEngineState.project.scale + hexStep;
                uiEngineState.project.scale = (nextVal < 0) ? (nextVal + 256) : (nextVal > 255) ? (nextVal - 256) : nextVal;
            } else if (cursor_id == C::LIVE_QUANTIZE) {
                int hexStep = (event.key.key == SDLK_UP) ? 16 : -16;
                int nextVal = uiEngineState.project.live_quantize + hexStep;
                uiEngineState.project.live_quantize = (nextVal < 0) ? (nextVal + 256) : (nextVal > 255) ? (nextVal - 256) : nextVal;
            }
        }
    }

    if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
        if (cursor_id == C::PROJ_LOAD) {
            actions.browserForSongLoad = true;
            std::string startDir = GetSongInitialDir(actions.currentSongPath);
            actions.fileBrowser.init(startDir, ".m8s");
            actions.fileBrowser.setTitle("LOAD SONG");
            actions.viewManager.pushModal(m8::ui::ViewType::FILE_BROWSER);
        } else if (cursor_id == C::PROJ_SAVE) {
            if (actions.currentSongPath.empty()) {
                actions.textInputActive = true;
                actions.textInputBuffer.clear();
                actions.textInputPrompt = "SAVE AS:";
                SDL_StartTextInput(SDL_GetKeyboardFocus());
            } else {
                std::string err;
                bool ok = m8::io::saveSong(actions.currentSongPath, actions.currentLoadResult,
                                           actions.uiSequencer, uiEngineState, err);
                if (!ok) actions.missingSamplesMsg = "SAVE FAILED: " + err;
                else actions.missingSamplesMsg = "SAVED: " + actions.currentSongPath;
            }
        } else if (cursor_id == C::PROJ_NEW) {
            actions.confirmDialog.init("LOSE CHANGES TO CURRENT SONG?", 0);
            actions.viewManager.pushModal(m8::ui::ViewType::CONFIRMATION);
        } else if (cursor_id == C::EXPORT_RENDER) {
            actions.viewManager.pushModal(m8::ui::ViewType::RENDER);
        } else if (cursor_id == C::EXPORT_BUNDLE) {
            actions.confirmDialog.init("CREATE DIRECTORY OF SONG AND\nSAMPLES? A PRE-EXISTING BUNDLE\nMAY BE OVERWRITTEN", 0, 1);
            actions.viewManager.pushModal(m8::ui::ViewType::CONFIRMATION);
        } else if (cursor_id == C::CLEAR_PHRASES) {
            actions.confirmDialog.init("CLEAR UNUSED PHRASES/CHAINS\nAND REMOVE DUPLICATES?", 0, 2);
            actions.viewManager.pushModal(m8::ui::ViewType::CONFIRMATION);
        } else if (cursor_id == C::CLEAR_INST) {
            actions.confirmDialog.init("CLEAR UNUSED INST/TABLES/EQS AND\nREMOVE DUPLICATES?", 0, 3);
            actions.viewManager.pushModal(m8::ui::ViewType::CONFIRMATION);
        } else if (cursor_id == C::INST_POOL) {
            // Jump to the INST POOL screen (ViewManager coords x=3, y=2).
            actions.viewManager.setCoords(3, 2);
        } else if (cursor_id == C::SAMPLE_ROOT) {
            actions.textInputActive = true;
            actions.textInputBuffer = getSampleRoot();
            actions.textInputPrompt = "SAMPLE ROOT:";
            SDL_StartTextInput(SDL_GetKeyboardFocus());
        }
    }
}

void HandleProjectKeyUp(const SDL_Event& event, engine::EngineState& uiEngineState,
                        CursorId cursor_id, CommandSink& commandSink) {
    if (g_nudgeActive && (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT || event.key.key == SDLK_X)) {
        PushParam(commandSink, uiEngineState, m8::engine::ParamID::BPM_INT, g_preNudgeBpm);
        g_nudgeActive = false;
    }
    if (cursor_id == CursorId::TRANSPOSE) {
        g_transposeNoticeUntil = SDL_GetTicks() + 2500;
        if (event.key.key == SDLK_X) {
            PushParam(commandSink, uiEngineState, m8::engine::ParamID::PROJ_TRANSPOSE, static_cast<uint8_t>(uiEngineState.project.transpose));
        }
    }
    if (cursor_id == CursorId::GROOVE && event.key.key == SDLK_X) {
        PushParam(commandSink, uiEngineState, m8::engine::ParamID::PROJ_GROOVE, uiEngineState.project.groove);
    }
    if (cursor_id == CursorId::SCALE && event.key.key == SDLK_X) {
        PushParam(commandSink, uiEngineState, m8::engine::ParamID::PROJ_SCALE, uiEngineState.project.scale);
    }
    if (cursor_id == CursorId::LIVE_QUANTIZE && event.key.key == SDLK_X) {
        PushParam(commandSink, uiEngineState, m8::engine::ParamID::PROJ_LIVE_QUANTIZE, uiEngineState.project.live_quantize);
    }
}

void HandleProjectEditRelease(CursorId cursor_id, int& nameCharIndex,
                               engine::EngineState& uiEngineState, CommandSink& commandSink,
                               ProjectActionState& actions) {
    if (g_nudgeActive) {
        PushParam(commandSink, uiEngineState, m8::engine::ParamID::BPM_INT, g_preNudgeBpm);
        g_nudgeActive = false;
    }
    if (cursor_id == CursorId::TRANSPOSE) {
        g_transposeNoticeUntil = SDL_GetTicks() + 2500;
        PushParam(commandSink, uiEngineState, m8::engine::ParamID::PROJ_TRANSPOSE, static_cast<uint8_t>(uiEngineState.project.transpose));
    }
    if (cursor_id == CursorId::GROOVE) {
        PushParam(commandSink, uiEngineState, m8::engine::ParamID::PROJ_GROOVE, uiEngineState.project.groove);
    }
    if (cursor_id == CursorId::SCALE) {
        PushParam(commandSink, uiEngineState, m8::engine::ParamID::PROJ_SCALE, uiEngineState.project.scale);
    }
    if (cursor_id == CursorId::LIVE_QUANTIZE) {
        PushParam(commandSink, uiEngineState, m8::engine::ParamID::PROJ_LIVE_QUANTIZE, uiEngineState.project.live_quantize);
    }
    (void)nameCharIndex;
    (void)commandSink;
    using C = CursorId;
    if (cursor_id == C::PROJ_LOAD) {
        actions.browserForSongLoad = true;
        std::string startDir = GetSongInitialDir(actions.currentSongPath);
        actions.fileBrowser.init(startDir, ".m8s");
        actions.fileBrowser.setTitle("LOAD SONG");
        actions.viewManager.pushModal(m8::ui::ViewType::FILE_BROWSER);
    } else if (cursor_id == C::PROJ_SAVE) {
        if (actions.currentSongPath.empty()) {
            actions.textInputActive = true;
            actions.textInputBuffer.clear();
            actions.textInputPrompt = "SAVE AS:";
            SDL_StartTextInput(SDL_GetKeyboardFocus());
        } else {
            std::string err;
            bool ok = m8::io::saveSong(actions.currentSongPath, actions.currentLoadResult,
                                       actions.uiSequencer, uiEngineState, err);
            if (!ok) actions.missingSamplesMsg = "SAVE FAILED: " + err;
        }
    } else if (cursor_id == C::PROJ_NEW) {
        actions.confirmDialog.init("LOSE CHANGES TO CURRENT SONG?", 0);
        actions.viewManager.pushModal(m8::ui::ViewType::CONFIRMATION);
    } else if (cursor_id == C::EXPORT_RENDER) {
        actions.viewManager.pushModal(m8::ui::ViewType::RENDER);
    } else if (cursor_id == C::EXPORT_BUNDLE) {
        actions.confirmDialog.init("CREATE DIRECTORY OF SONG AND\nSAMPLES? A PRE-EXISTING BUNDLE\nMAY BE OVERWRITTEN", 0, 1);
        actions.viewManager.pushModal(m8::ui::ViewType::CONFIRMATION);
    } else if (cursor_id == C::CLEAR_PHRASES) {
        actions.confirmDialog.init("CLEAR UNUSED PHRASES/CHAINS\nAND REMOVE DUPLICATES?", 0, 2);
        actions.viewManager.pushModal(m8::ui::ViewType::CONFIRMATION);
    } else if (cursor_id == C::CLEAR_INST) {
        actions.confirmDialog.init("CLEAR UNUSED INST/TABLES/EQS AND\nREMOVE DUPLICATES?", 0, 3);
        actions.viewManager.pushModal(m8::ui::ViewType::CONFIRMATION);
    } else if (cursor_id == C::INST_POOL) {
        // Jump to the INST POOL screen (ViewManager coords x=3, y=2).
        actions.viewManager.setCoords(3, 2);
    } else if (cursor_id == C::SYSTEM_SETTINGS) {
        actions.viewManager.pushModal(m8::ui::ViewType::SYSTEM_SETTINGS);
    } else if (cursor_id == C::SAMPLE_ROOT) {
        actions.textInputActive = true;
        actions.textInputBuffer = getSampleRoot();
        actions.textInputPrompt = "SAMPLE ROOT:";
        SDL_StartTextInput(SDL_GetKeyboardFocus());
    }
}

} // namespace project
} // namespace ui
} // namespace m8
