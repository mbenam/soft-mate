#include "ModScreen.h"
#include "ModScreenLayout.h"
#include <iomanip>
#include <sstream>

namespace m8 {
namespace ui {
namespace mods {

static SDL_Color GetColorFromString(const std::string& colorName) {
    if (colorName == "TITLE") return {255, 60, 60, 255}; 
    if (colorName == "LABEL_DIM") return {100, 100, 100, 255}; 
    if (colorName == "LABEL_LITE") return {0, 255, 255, 255}; 
    if (colorName == "VALUE") return {255, 255, 255, 255}; 
    if (colorName == "ACCENT") return {255, 60, 60, 255}; 
    if (colorName == "SLIDER_BG") return {100, 100, 100, 255}; 
    return {255, 255, 255, 255};
}

static std::string ToHex(int value) {
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << value;
    return ss.str();
}

static std::string ResolveModValue(CursorId fieldId, const engine::Modulator& mod) {
    if (IsTypeCursor(fieldId)) return ToHex(mod.type);
    if (IsDestCursor(fieldId)) return ToHex(mod.dest);
    if (IsAmtCursor(fieldId)) return ToHex(mod.amt);
    if (IsParamCursor(fieldId)) {
        switch (ParamSlotOf(fieldId)) {
            case 1: return ToHex(mod.p1);
            case 2: return ToHex(mod.p2);
            case 3: return ToHex(mod.p3);
            case 4: return ToHex(mod.p4);
        }
    }
    return "00";
}

static std::string ResolveModAccent(CursorId fieldId, const engine::Modulator& mod) {
    if (IsTypeCursor(fieldId)) {
        const char* names[] = {"AHD ENV", "ADSR ENV", "DRUM ENV", "LFO", "TRIG ENV", "TRACKING"};
        return names[mod.type % 6];
    }
    if (IsDestCursor(fieldId)) {
        if (mod.dest == 0) return "OFF";
        return "PARAM " + std::to_string(mod.dest); // Can map to actual names later
    }

    int paramSlot = IsParamCursor(fieldId) ? ParamSlotOf(fieldId) : 0;
    if (mod.type == 3) { // LFO
        if (paramSlot == 1) {
            const char* osc[] = {"TRI", "SIN", "SQU", "S+H"};
            return osc[mod.p1 % 4];
        }
        if (paramSlot == 2) return (mod.p2 == 0) ? "FREE" : "RETRIG";
    }
    if (mod.type == 4 && paramSlot == 4) {
        return (mod.p4 == 0) ? "WAVSYNTH" : "EXTERNAL";
    }
    if (mod.type == 5 && paramSlot == 1) {
        return (mod.p1 == 0) ? "NOTE" : "VELOCITY";
    }
    return "";
}

static int GetModSliderValue(CursorId fieldId, const engine::Modulator& mod) {
    if (IsAmtCursor(fieldId)) return mod.amt;
    if (IsParamCursor(fieldId)) {
        switch (ParamSlotOf(fieldId)) {
            case 1: return mod.p1;
            case 2: return mod.p2;
            case 3: return mod.p3;
            case 4: return mod.p4;
        }
    }
    return 0;
}

void RenderModScreen(Renderer& renderer,
                     const engine::EngineState& engState,
                     int currentInstIndex,
                     CursorId active_cursor_id) {
                            
    const engine::Instrument& currentInst = engState.instruments[currentInstIndex];
                            
    static std::vector<UI_GridCell> staticText = GetModStaticText();
    static std::vector<UI_GridCell> dynamicText = GetModDynamicTextDefaults();
    auto interactiveFields = GetModInteractiveFields(currentInst); // Dynamic every frame

    for (const auto& cell : staticText) {
        renderer.drawString(cell.text, cell.col, cell.row, GetColorFromString(cell.normal_color));
    }

    for (const auto& cell : dynamicText) {
        std::string textToDraw = cell.text;
        if (cell.text == "13") textToDraw = ToHex(currentInstIndex);
        else if (cell.text.substr(0, 2) == "T>") textToDraw = "T>" + std::to_string(engState.bpm);
        
        renderer.drawString(textToDraw, cell.col, cell.row, GetColorFromString(cell.normal_color));
    }

    for (const auto& [fieldId, components] : interactiveFields) {
        bool isActive = (fieldId == active_cursor_id);
        int q = QuadrantOf(fieldId);
        const auto& mod = currentInst.mods[q];

        std::string liveText = ResolveModValue(fieldId, mod);
        std::string accentText = ResolveModAccent(fieldId, mod);

        for (const auto& comp : components) {
            SDL_Color color = GetColorFromString(isActive ? comp.selected_color : comp.normal_color);

            if (comp.role == "slider") {
                int px = comp.col * 8;
                int py = comp.row * 8;
                int max_pixels = comp.width * 8; 
                int val = GetModSliderValue(fieldId, mod);
                int fill_pixels = (val * max_pixels) / 255;
                renderer.fillRectPixel(px, py, fill_pixels, 8, color);
            } 
            else {
                std::string drawText = comp.text;
                if (comp.role == "value") drawText = liveText;
                else if (comp.role == "accent") drawText = accentText;

                renderer.drawString(drawText, comp.col, comp.row, color);

                if (isActive && comp.has_cursor_box && comp.role == "value") {
                    // M8 aesthetic: Include the width of the accent text in the cyan bracket bounds
                    int bracketLen = drawText.length() + (accentText.empty() ? 0 : 1 + accentText.length());
                    renderer.drawBracket(comp.col, comp.row, bracketLen, {0, 255, 255, 255});
                }
            }
        }
    }
}

void HandleModInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                     engine::EngineState& uiEngineState, int currentInstIndex,
                     CursorId& cursor_id, CommandSink& commandSink) {
    using C = CursorId;
    auto navMap = GetModNavMap(uiEngineState.instruments[currentInstIndex]);

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

    // Value Editing Block
    if (editHeld && (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN || event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT)) {
        arrowPressedDuringEdit = true;
        int step = (event.key.key == SDLK_RIGHT || event.key.key == SDLK_UP) ? 1 : -1;
        int q = QuadrantOf(cursor_id);
        const auto& mod = uiEngineState.instruments[currentInstIndex].mods[q];

        if (IsTypeCursor(cursor_id)) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MOD_TYPE, std::clamp<int>(mod.type + step, 0, 5), currentInstIndex, q);
        else if (IsDestCursor(cursor_id)) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MOD_DEST, std::clamp<int>(mod.dest + step, 0, 255), currentInstIndex, q);
        else if (IsAmtCursor(cursor_id)) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MOD_AMT, std::clamp<int>(mod.amt + step, 0, 255), currentInstIndex, q);
        else if (IsParamCursor(cursor_id) && ParamSlotOf(cursor_id) == 1) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MOD_P1, std::clamp<int>(mod.p1 + step, 0, 255), currentInstIndex, q);
        else if (IsParamCursor(cursor_id) && ParamSlotOf(cursor_id) == 2) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MOD_P2, std::clamp<int>(mod.p2 + step, 0, 255), currentInstIndex, q);
        else if (IsParamCursor(cursor_id) && ParamSlotOf(cursor_id) == 3) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MOD_P3, std::clamp<int>(mod.p3 + step, 0, 255), currentInstIndex, q);
        else if (IsParamCursor(cursor_id) && ParamSlotOf(cursor_id) == 4) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MOD_P4, std::clamp<int>(mod.p4 + step, 0, 255), currentInstIndex, q);
    }
}

} // namespace mods
} // namespace ui
} // namespace m8
