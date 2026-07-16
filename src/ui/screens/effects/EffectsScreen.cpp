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

static std::string ResolveEffectsValue(CursorId fieldId, const engine::EffectsState& fx) {
    if (fieldId == CursorId::CHO_EQ) return "EQ";
    if (fieldId == CursorId::CHO_MOD_DEP) return ToHex(fx.cho_mod_depth);
    if (fieldId == CursorId::CHO_MOD_FRQ) return ToHex(fx.cho_mod_freq);
    if (fieldId == CursorId::CHO_WID) return ToHex(fx.cho_width);
    if (fieldId == CursorId::CHO_REV) return ToHex(fx.cho_reverb);

    if (fieldId == CursorId::DEL_EQ) return "EQ";
    if (fieldId == CursorId::DEL_TIME_L) return ToHex(fx.del_time_l);
    if (fieldId == CursorId::DEL_TIME_R) return ToHex(fx.del_time_r);
    if (fieldId == CursorId::DEL_FBK) return ToHex(fx.del_feedback);
    if (fieldId == CursorId::DEL_WID) return ToHex(fx.del_width);
    if (fieldId == CursorId::DEL_REV) return ToHex(fx.del_reverb);

    if (fieldId == CursorId::REV_EQ) return "EQ";
    if (fieldId == CursorId::REV_SIZE) return ToHex(fx.rev_size);
    if (fieldId == CursorId::REV_DEC) return ToHex(fx.rev_decay);
    if (fieldId == CursorId::REV_MOD_DEP) return ToHex(fx.rev_mod_depth);
    if (fieldId == CursorId::REV_MOD_FRQ) return ToHex(fx.rev_mod_freq);
    if (fieldId == CursorId::REV_WID) return ToHex(fx.rev_width);

    return "00";
}

void RenderEffectsScreen(Renderer& renderer,
                         const engine::EngineState& engState,
                         CursorId active_cursor_id) {
                            
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

void HandleEffectsInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                         engine::EngineState& uiEngineState, CursorId& cursor_id,
                         CommandSink& commandSink) {
    auto navMap = GetEffectsNavMap();
    if (event.key.key == SDLK_DOWN) {
        if (!editHeld && navMap.count(cursor_id) && navMap[cursor_id].down != CursorId::NONE) {
            cursor_id = navMap[cursor_id].down;
        }
    } else if (event.key.key == SDLK_UP) {
        if (!editHeld && navMap.count(cursor_id) && navMap[cursor_id].up != CursorId::NONE) {
            cursor_id = navMap[cursor_id].up;
        }
    } else if (event.key.key == SDLK_RIGHT) {
        if (!editHeld && navMap.count(cursor_id) && navMap[cursor_id].right != CursorId::NONE) {
            cursor_id = navMap[cursor_id].right;
        }
    } else if (event.key.key == SDLK_LEFT) {
        if (!editHeld && navMap.count(cursor_id) && navMap[cursor_id].left != CursorId::NONE) {
            cursor_id = navMap[cursor_id].left;
        }
    }

    if (editHeld && (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN || event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT)) {
        arrowPressedDuringEdit = true;
        int step = (event.key.key == SDLK_RIGHT || event.key.key == SDLK_UP) ? 1 : -1;
        const auto& fx = uiEngineState.effects;

        if (cursor_id == CursorId::CHO_MOD_DEP) PushParam(commandSink, uiEngineState, m8::engine::ParamID::FX_CHO_MOD_DEPTH, std::clamp<int>(fx.cho_mod_depth + step, 0, 255));
        else if (cursor_id == CursorId::CHO_MOD_FRQ) PushParam(commandSink, uiEngineState, m8::engine::ParamID::FX_CHO_MOD_FREQ, std::clamp<int>(fx.cho_mod_freq + step, 0, 255));
        else if (cursor_id == CursorId::CHO_WID) PushParam(commandSink, uiEngineState, m8::engine::ParamID::FX_CHO_WIDTH, std::clamp<int>(fx.cho_width + step, 0, 255));
        else if (cursor_id == CursorId::CHO_REV) PushParam(commandSink, uiEngineState, m8::engine::ParamID::FX_CHO_REVERB, std::clamp<int>(fx.cho_reverb + step, 0, 255));
        else if (cursor_id == CursorId::DEL_TIME_L) PushParam(commandSink, uiEngineState, m8::engine::ParamID::FX_DEL_TIME_L, std::clamp<int>(fx.del_time_l + step, 0, 255));
        else if (cursor_id == CursorId::DEL_TIME_R) PushParam(commandSink, uiEngineState, m8::engine::ParamID::FX_DEL_TIME_R, std::clamp<int>(fx.del_time_r + step, 0, 255));
        else if (cursor_id == CursorId::DEL_FBK) PushParam(commandSink, uiEngineState, m8::engine::ParamID::FX_DEL_FEEDBACK, std::clamp<int>(fx.del_feedback + step, 0, 255));
        else if (cursor_id == CursorId::DEL_WID) PushParam(commandSink, uiEngineState, m8::engine::ParamID::FX_DEL_WIDTH, std::clamp<int>(fx.del_width + step, 0, 255));
        else if (cursor_id == CursorId::DEL_REV) PushParam(commandSink, uiEngineState, m8::engine::ParamID::FX_DEL_REVERB, std::clamp<int>(fx.del_reverb + step, 0, 255));
        else if (cursor_id == CursorId::REV_SIZE) PushParam(commandSink, uiEngineState, m8::engine::ParamID::FX_REV_SIZE, std::clamp<int>(fx.rev_size + step, 0, 255));
        else if (cursor_id == CursorId::REV_DEC) PushParam(commandSink, uiEngineState, m8::engine::ParamID::FX_REV_DECAY, std::clamp<int>(fx.rev_decay + step, 0, 255));
        else if (cursor_id == CursorId::REV_MOD_DEP) PushParam(commandSink, uiEngineState, m8::engine::ParamID::FX_REV_MOD_DEPTH, std::clamp<int>(fx.rev_mod_depth + step, 0, 255));
        else if (cursor_id == CursorId::REV_MOD_FRQ) PushParam(commandSink, uiEngineState, m8::engine::ParamID::FX_REV_MOD_FREQ, std::clamp<int>(fx.rev_mod_freq + step, 0, 255));
        else if (cursor_id == CursorId::REV_WID) PushParam(commandSink, uiEngineState, m8::engine::ParamID::FX_REV_WIDTH, std::clamp<int>(fx.rev_width + step, 0, 255));
    }
}

} // namespace effects
} // namespace ui
} // namespace m8
