#include "ProjectScreen.h"
#include "ProjectScreenLayout.h"
#include <iomanip>
#include <sstream>

namespace m8 {
namespace ui {
namespace project {

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

static std::string ResolveProjectValue(const std::string& fieldId, const engine::EngineState& state) {
    const engine::ProjectSettings& proj = state.project;

    if (fieldId == "TEMPO_INT") return std::to_string(state.bpm);
    if (fieldId == "TEMPO_DEC") {
        std::stringstream ss;
        ss << std::setfill('0') << std::setw(2) << state.bpm_frac;
        return ss.str();
    }
    if (fieldId == "TEMPO_NUDGE") return "< >";
    if (fieldId == "TRANSPOSE") return ToHex(proj.transpose);
    
    if (fieldId == "GROOVE") return ToHex(proj.groove);
    if (fieldId == "SCALE") return ToHex(proj.scale);
    if (fieldId == "LIVE_QUANTIZE") return ToHex(proj.live_quantize);
    
    if (fieldId == "NAME") return proj.name;

    // Action Fields
    if (fieldId == "MIDI_SETTINGS") return "SETTINGS";
    if (fieldId == "MIDI_MAPPINGS") return "MAPPINGS";
    if (fieldId == "PROJ_LOAD") return "LOAD";
    if (fieldId == "PROJ_SAVE") return "SAVE";
    if (fieldId == "PROJ_NEW") return "NEW";
    if (fieldId == "EXPORT_RENDER") return "RENDER";
    if (fieldId == "EXPORT_BUNDLE") return "BUNDLE";
    if (fieldId == "CLEAR_PHRASES") return "PHRASES";
    if (fieldId == "CLEAR_INST") return "INST/TBL";
    if (fieldId == "INST_POOL") return "VIEW INST.POOL";
    if (fieldId == "TIME_STATS") return "VIEW TIME STATS";
    if (fieldId == "SYSTEM_SETTINGS") return "SETTINGS";

    return "--";
}

void RenderProjectScreen(Renderer& renderer, 
                         const engine::EngineState& engState, 
                         const std::string& active_cursor_id) {
                            
    static std::vector<UI_GridCell> staticText = GetProjectStaticText();
    static std::vector<UI_GridCell> dynamicText = GetProjectDynamicTextDefaults();
    static std::unordered_map<std::string, std::vector<UI_GridCell>> interactiveFields = GetProjectInteractiveFields();

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
                if (fieldId == "GROOVE" && engState.project.groove == 0) drawText = "DEFAULT";
                else if (fieldId == "SCALE" && engState.project.scale == 0) drawText = "C CHROMATIC";
                else if (fieldId == "LIVE_QUANTIZE" && engState.project.live_quantize == 0) drawText = "CHAIN LEN";
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

} // namespace project
} // namespace ui
} // namespace m8
