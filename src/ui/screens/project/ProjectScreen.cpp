#include "ProjectScreen.h"
#include "ProjectScreenLayout.h"
#include <iomanip>
#include <sstream>

namespace m8 {
namespace ui {
namespace project {

static std::string g_sampleRoot = "Samples";

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
    ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << value;
    return ss.str();
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

    if (fieldId == C::NAME) return proj.name;

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
                         CursorId active_cursor_id) {

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

    // 3. Render Interactive Fields
    for (const auto& [fieldId, components] : interactiveFields) {
        bool isActive = (fieldId == active_cursor_id);
        std::string liveText = ResolveProjectValue(fieldId, engState);

        for (const auto& comp : components) {
            SDL_Color color = GetColorFromString(isActive ? comp.selected_color : comp.normal_color);

            std::string drawText = (comp.role == "value" || comp.role == "accent") ? liveText : comp.text;
            
            // Draw logic for accented contextual text (e.g. "DEFAULT", "C CHROMATIC")
            if (comp.role == "accent") {
                if (fieldId == CursorId::GROOVE && engState.project.groove == 0) drawText = "DEFAULT";
                else if (fieldId == CursorId::SCALE && engState.project.scale == 0) drawText = "C CHROMATIC";
                else if (fieldId == CursorId::LIVE_QUANTIZE && engState.project.live_quantize == 0) drawText = "CHAIN LEN";
                else drawText = ""; // Hide accent if non-zero
                
                // If it resolves to empty, skip drawing
                if (drawText.empty()) continue; 
            }

            renderer.drawString(drawText, comp.col, comp.row, color);

            // Draw cyan bounding box on active values
            if (isActive && comp.has_cursor_box && comp.role == "value") {
                renderer.drawBracket(comp.col, comp.row, drawText.length(), {0, 255, 255, 255});
            }
        }
    }
}

void HandleProjectInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                         engine::EngineState& uiEngineState, CursorId& cursor_id,
                         CommandSink& commandSink, ProjectActionState& actions) {
    using C = CursorId;
    auto navMap = GetProjectNavMap();
    if (event.key.key == SDLK_DOWN) {
        if (!editHeld && navMap.count(cursor_id) && navMap[cursor_id].down != C::NONE) {
            cursor_id = navMap[cursor_id].down;
        }
    } else if (event.key.key == SDLK_UP) {
        if (!editHeld && navMap.count(cursor_id) && navMap[cursor_id].up != C::NONE) {
            cursor_id = navMap[cursor_id].up;
        }
    } else if (event.key.key == SDLK_RIGHT) {
        if (!editHeld && navMap.count(cursor_id) && navMap[cursor_id].right != C::NONE) {
            cursor_id = navMap[cursor_id].right;
        }
    } else if (event.key.key == SDLK_LEFT) {
        if (!editHeld && navMap.count(cursor_id) && navMap[cursor_id].left != C::NONE) {
            cursor_id = navMap[cursor_id].left;
        }
    }

    if (editHeld && (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT)) {
        int step = (event.key.key == SDLK_RIGHT) ? 1 : -1;
        if (cursor_id == C::TEMPO_INT) {
            PushParam(commandSink, uiEngineState, m8::engine::ParamID::BPM_INT, std::clamp<int>(uiEngineState.bpm + step, 20, 999));
        } else if (cursor_id == C::TEMPO_DEC) {
            PushParam(commandSink, uiEngineState, m8::engine::ParamID::BPM_FRAC, std::clamp<int>(uiEngineState.bpm_frac + step, 0, 99));
        }
    }

    if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
        if (cursor_id == C::PROJ_LOAD) {
            actions.browserForSongLoad = true;
            actions.fileBrowser.init(".", ".M8S");
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
        } else if (cursor_id == C::SAMPLE_ROOT) {
            actions.textInputActive = true;
            actions.textInputBuffer = getSampleRoot();
            actions.textInputPrompt = "SAMPLE ROOT:";
            SDL_StartTextInput(SDL_GetKeyboardFocus());
        }
    }
}

void HandleProjectEditRelease(CursorId cursor_id, engine::EngineState& uiEngineState,
                               ProjectActionState& actions) {
    using C = CursorId;
    if (cursor_id == C::PROJ_LOAD) {
        actions.browserForSongLoad = true;
        actions.fileBrowser.init(".", ".M8S");
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
            // NOTE: matches the original -- X-release save only reports failure,
            // not success (the KEY_DOWN/ENTER path sets a "SAVED: ..." message on
            // success; this path historically did not). Preserved as-is (mechanical
            // move, CODE_CLEANUP_SPEC.md #1) rather than "fixed" into something new.
            bool ok = m8::io::saveSong(actions.currentSongPath, actions.currentLoadResult,
                                       actions.uiSequencer, uiEngineState, err);
            if (!ok) actions.missingSamplesMsg = "SAVE FAILED: " + err;
        }
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
