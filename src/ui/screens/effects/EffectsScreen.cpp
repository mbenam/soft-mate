#include "EffectsScreen.h"
#include "EffectsScreenLayout.h"
#include <iomanip>
#include <sstream>

namespace m8 {
namespace ui {
namespace effects {

static SDL_Color GetColorFromString(const std::string& colorName) {
    if (colorName == "TITLE") return {255, 60, 60, 255}; 
    if (colorName == "LABEL_DIM") return {100, 100, 100, 255}; 
    if (colorName == "LABEL_LITE") return {0, 255, 255, 255}; 
    if (colorName == "VALUE") return {255, 255, 255, 255}; 
    return {255, 255, 255, 255};
}

static std::string ToHex(int value) {
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << value;
    return ss.str();
}

static std::string ResolveEffectsValue(const std::string& fieldId, const engine::EffectsState& fx) {
    if (fieldId == "CHO_EQ") return "EQ";
    if (fieldId == "CHO_MOD_DEP") return ToHex(fx.cho_mod_depth);
    if (fieldId == "CHO_MOD_FRQ") return ToHex(fx.cho_mod_freq);
    if (fieldId == "CHO_WID") return ToHex(fx.cho_width);
    if (fieldId == "CHO_REV") return ToHex(fx.cho_reverb);
    
    if (fieldId == "DEL_EQ") return "EQ";
    if (fieldId == "DEL_TIME_L") return ToHex(fx.del_time_l);
    if (fieldId == "DEL_TIME_R") return ToHex(fx.del_time_r);
    if (fieldId == "DEL_FBK") return ToHex(fx.del_feedback);
    if (fieldId == "DEL_WID") return ToHex(fx.del_width);
    if (fieldId == "DEL_REV") return ToHex(fx.del_reverb);

    if (fieldId == "REV_EQ") return "EQ";
    if (fieldId == "REV_SIZE") return ToHex(fx.rev_size);
    if (fieldId == "REV_DEC") return ToHex(fx.rev_decay);
    if (fieldId == "REV_MOD_DEP") return ToHex(fx.rev_mod_depth);
    if (fieldId == "REV_MOD_FRQ") return ToHex(fx.rev_mod_freq);
    if (fieldId == "REV_WID") return ToHex(fx.rev_width);
    
    return "00";
}

void RenderEffectsScreen(Renderer& renderer, 
                         const engine::EngineState& engState, 
                         const std::string& active_cursor_id) {
                            
    const engine::EffectsState& fx = engState.effects;
                            
    static std::vector<UI_GridCell> staticText = GetEffectsStaticText();
    static auto interactiveFields = GetEffectsInteractiveFields();

    for (const auto& cell : staticText) {
        renderer.drawString(cell.text, cell.col, cell.row, GetColorFromString(cell.normal_color));
    }

    // Dynamic Tempo
    

    // Render Interactive Fields
    for (const auto& [fieldId, components] : interactiveFields) {
        bool isActive = (fieldId == active_cursor_id);
        std::string liveText = ResolveEffectsValue(fieldId, fx);

        for (const auto& comp : components) {
            SDL_Color color = GetColorFromString(isActive ? comp.selected_color : comp.normal_color);
            std::string drawText = (comp.role == "value") ? liveText : comp.text;

            renderer.drawString(drawText, comp.col, comp.row, color);

            if (isActive && comp.has_cursor_box && comp.role == "value") {
                renderer.drawBracket(comp.col, comp.row, drawText.length(), {0, 255, 255, 255});
            }
        }
    }

    
}

} // namespace effects
} // namespace ui
} // namespace m8
