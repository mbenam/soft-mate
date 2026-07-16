#include "ScaleScreen.h"
#include "ScaleScreenLayout.h"
#include <iomanip>
#include <sstream>

namespace m8 {
namespace ui {
namespace scale {

static SDL_Color GetColorFromString(const std::string& colorName) {
    if (colorName == "TITLE") return {255, 60, 60, 255}; 
    if (colorName == "LABEL_DIM") return {100, 100, 100, 255}; 
    if (colorName == "LABEL_LITE") return {0, 255, 255, 255}; 
    if (colorName == "VALUE") return {255, 255, 255, 255}; 
    return {255, 255, 255, 255};
}

static std::string ResolveScaleValue(CursorId fieldId, const engine::Scale& scale) {
    using C = CursorId;
    if (fieldId == C::KEY) {
        const char* notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        return notes[scale.key % 12];
    }
    if (fieldId == C::TUNE) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%06.2f", scale.tune);
        return std::string(buf);
    }
    if (fieldId == C::NAME) return scale.name;
    if (fieldId == C::CMD_LOAD) return "LOAD";
    if (fieldId == C::CMD_SAVE) return "SAVE";

    if (IsNoteEnCursor(fieldId)) {
        int idx = NoteEnIndexOf(fieldId);
        return scale.notes[idx].enable ? "ON" : "OFF";
    }
    if (IsNoteOffsetCursor(fieldId)) {
        int idx = NoteOffsetIndexOf(fieldId);
        char buf[16];
        snprintf(buf, sizeof(buf), "%05.2f", scale.notes[idx].offset);
        return std::string(buf);
    }
    return "--";
}

void RenderScaleScreen(Renderer& renderer,
                       const engine::EngineState& engState,
                       int currentScaleIndex,
                       CursorId active_cursor_id) {

    static std::vector<UI_GridCell> staticText = GetScaleStaticText();
    static std::vector<UI_GridCell> dynamicText = GetScaleDynamicTextDefaults();
    static std::unordered_map<CursorId, std::vector<UI_GridCell>> interactiveFields = GetScaleInteractiveFields();

    const engine::Scale& currentScale = engState.scales[currentScaleIndex];

    // 1. Draw Static Text
    for (const auto& cell : staticText) {
        renderer.drawString(cell.text, cell.col, cell.row, GetColorFromString(cell.normal_color));
    }

    // 2. Draw Dynamic Text
    for (const auto& cell : dynamicText) {
        std::string textToDraw = cell.text;
        
        if (cell.text == "00" && cell.col == 7) {
            std::stringstream ss;
            ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << currentScaleIndex;
            textToDraw = ss.str();
        } else if (cell.text.substr(0, 2) == "T>") {
            textToDraw = "T>" + std::to_string(engState.bpm);
        }
        
        renderer.drawString(textToDraw, cell.col, cell.row, GetColorFromString(cell.normal_color));
    }

    // 3. Render Interactive Fields
    for (const auto& [fieldId, components] : interactiveFields) {
        bool isActive = (fieldId == active_cursor_id);
        std::string liveText = ResolveScaleValue(fieldId, currentScale);

        for (const auto& comp : components) {
            SDL_Color color = GetColorFromString(isActive ? comp.selected_color : comp.normal_color);
            std::string drawText = (comp.role == "value") ? liveText : comp.text;

            renderer.drawString(drawText, comp.col, comp.row, color);

            // Draw cyan bounding box on active values
            if (isActive && comp.has_cursor_box && comp.role == "value") {
                renderer.drawBracket(comp.col, comp.row, drawText.length(), {0, 255, 255, 255});
            }
        }
    }
}

void HandleScaleInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                       engine::EngineState& uiEngineState, int currentScaleIndex,
                       CursorId& cursor_id, CommandSink& commandSink) {
    using C = CursorId;
    auto navMap = GetScaleNavMap();
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
        arrowPressedDuringEdit = true;
        int step = (event.key.key == SDLK_RIGHT) ? 1 : -1;
        const auto& scale = uiEngineState.scales[currentScaleIndex];

        if (cursor_id == C::KEY) PushParam(commandSink, uiEngineState, m8::engine::ParamID::SCALE_KEY, (scale.key + step + 12) % 12, currentScaleIndex);
        else if (cursor_id == C::TUNE) PushParam(commandSink, uiEngineState, m8::engine::ParamID::SCALE_TUNE, 0, currentScaleIndex, 0, scale.tune + step);
        else if (IsNoteEnCursor(cursor_id)) {
            int idx = NoteEnIndexOf(cursor_id);
            PushParam(commandSink, uiEngineState, m8::engine::ParamID::SCALE_NOTE_EN, !scale.notes[idx].enable, currentScaleIndex, idx);
        } else if (IsNoteOffsetCursor(cursor_id)) {
            int idx = NoteOffsetIndexOf(cursor_id);
            PushParam(commandSink, uiEngineState, m8::engine::ParamID::SCALE_NOTE_OFFSET, 0, currentScaleIndex, idx, scale.notes[idx].offset + step);
        }
    }
}

} // namespace scale
} // namespace ui
} // namespace m8
