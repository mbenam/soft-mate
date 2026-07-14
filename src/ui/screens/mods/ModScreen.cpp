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

static std::string ResolveModValue(const std::string& fieldId, const engine::Modulator& mod) {
    if (fieldId.find("MOD_TYPE") != std::string::npos) return ToHex(mod.type);
    if (fieldId.find("MOD_DEST") != std::string::npos) return ToHex(mod.dest);
    if (fieldId.find("MOD_AMT") != std::string::npos) return ToHex(mod.amt);
    if (fieldId.find("MOD_P1") != std::string::npos) return ToHex(mod.p1);
    if (fieldId.find("MOD_P2") != std::string::npos) return ToHex(mod.p2);
    if (fieldId.find("MOD_P3") != std::string::npos) return ToHex(mod.p3);
    if (fieldId.find("MOD_P4") != std::string::npos) return ToHex(mod.p4);
    return "00";
}

static std::string ResolveModAccent(const std::string& fieldId, const engine::Modulator& mod) {
    if (fieldId.find("MOD_TYPE") != std::string::npos) {
        const char* names[] = {"AHD ENV", "ADSR ENV", "DRUM ENV", "LFO", "TRIG ENV", "TRACKING"};
        return names[mod.type % 6];
    }
    if (fieldId.find("MOD_DEST") != std::string::npos) {
        if (mod.dest == 0) return "OFF";
        return "PARAM " + std::to_string(mod.dest); // Can map to actual names later
    }
    
    if (mod.type == 3) { // LFO
        if (fieldId.find("MOD_P1") != std::string::npos) {
            const char* osc[] = {"TRI", "SIN", "SQU", "S+H"};
            return osc[mod.p1 % 4];
        }
        if (fieldId.find("MOD_P2") != std::string::npos) return (mod.p2 == 0) ? "FREE" : "RETRIG";
    }
    if (mod.type == 4 && fieldId.find("MOD_P4") != std::string::npos) {
        return (mod.p4 == 0) ? "WAVSYNTH" : "EXTERNAL";
    }
    if (mod.type == 5 && fieldId.find("MOD_P1") != std::string::npos) {
        return (mod.p1 == 0) ? "NOTE" : "VELOCITY";
    }
    return "";
}

static int GetModSliderValue(const std::string& fieldId, const engine::Modulator& mod) {
    if (fieldId.find("MOD_AMT") != std::string::npos) return mod.amt;
    if (fieldId.find("MOD_P1") != std::string::npos) return mod.p1;
    if (fieldId.find("MOD_P2") != std::string::npos) return mod.p2;
    if (fieldId.find("MOD_P3") != std::string::npos) return mod.p3;
    if (fieldId.find("MOD_P4") != std::string::npos) return mod.p4;
    return 0;
}

void RenderModScreen(Renderer& renderer, 
                     const engine::EngineState& engState, 
                     int currentInstIndex,
                     const std::string& active_cursor_id) {
                            
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
        int q = fieldId.back() - '0';
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

} // namespace mods
} // namespace ui
} // namespace m8
