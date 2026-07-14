#include "InstrumentScreen.h"
#include "InstrumentSamplerLayout.h"
#include "InstrumentMacrosynLayout.h"
#include <iomanip>
#include <sstream>

namespace m8 {
namespace ui {
namespace instrument {

static SDL_Color GetColorFromString(const std::string& colorName) {
    if (colorName == "TITLE") return {255, 60, 60, 255}; 
    if (colorName == "LABEL_DIM") return {100, 100, 100, 255}; 
    if (colorName == "LABEL_LITE") return {0, 255, 255, 255}; 
    if (colorName == "VALUE") return {255, 255, 255, 255}; 
    if (colorName == "ACCENT") return {255, 60, 60, 255}; 
    if (colorName == "SLIDER_BG") return {100, 100, 100, 255}; 
    return {255, 255, 255, 255};
}

// Helper: Formats an integer as a 2-digit uppercase hex string
static std::string ToHex(int value) {
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << value;
    return ss.str();
}

// Helper: Resolves the live string value for a given Field ID
static std::string ResolveInstrumentValue(const std::string& fieldId, const engine::Instrument& inst) {
    bool isMac = (inst.type == engine::InstType::INST_MACROSYN);
    
    if (fieldId == "TYPE") return isMac ? "MACROSYN" : "SAMPLER ";
    if (fieldId == "NAME") return inst.name;
    if (fieldId == "CMD_LOAD") return "LOAD";
    if (fieldId == "CMD_SAVE") return "SAVE";
    if (fieldId == "SAMPLE_LOAD") return "LOAD";
    if (fieldId == "SAMPLE_REC") return "REC.";
    
    if (fieldId == "TRANSP") return (isMac ? inst.macrosyn.transp : inst.sampler.transp) ? "ON" : "OFF";
    int eq = isMac ? inst.macrosyn.eq : inst.sampler.eq;
    if (fieldId == "EQ") return eq == 0 ? "--" : ToHex(eq);

    // Enums that have separate string accents
    if (fieldId == "FILTER") return ToHex(isMac ? inst.macrosyn.filter_type : inst.sampler.filter_type);
    if (fieldId == "PLAY") return ToHex(inst.sampler.play);
    if (fieldId == "LIM") return ToHex(isMac ? inst.macrosyn.lim : inst.sampler.lim);
    if (fieldId == "SLICE") return ToHex(inst.sampler.slice);

    // Standard Hex values
    if (fieldId == "TBL_TIC") return ToHex(isMac ? inst.macrosyn.tbl_tic : inst.sampler.tbl_tic);
    if (fieldId == "START") return ToHex(inst.sampler.start);
    if (fieldId == "LOOP_ST") return ToHex(inst.sampler.loop_st);
    if (fieldId == "LENGTH") return ToHex(inst.sampler.length);
    if (fieldId == "DETUNE") return ToHex(inst.sampler.detune);
    if (fieldId == "DEGRADE") return ToHex(isMac ? inst.macrosyn.degrade : inst.sampler.degrade);
    if (fieldId == "CUTOFF") return ToHex(isMac ? inst.macrosyn.cutoff : inst.sampler.cutoff);
    if (fieldId == "RES") return ToHex(isMac ? inst.macrosyn.res : inst.sampler.res);
    if (fieldId == "AMP") return ToHex(isMac ? inst.macrosyn.amp : inst.sampler.amp);
    if (fieldId == "PAN") return ToHex(isMac ? inst.macrosyn.pan : inst.sampler.pan);
    if (fieldId == "DRY") return ToHex(isMac ? inst.macrosyn.dry : inst.sampler.dry);
    if (fieldId == "CHO") return ToHex(isMac ? inst.macrosyn.cho : inst.sampler.cho);
    if (fieldId == "DEL") return ToHex(isMac ? inst.macrosyn.del : inst.sampler.del);
    if (fieldId == "REV") return ToHex(isMac ? inst.macrosyn.rev : inst.sampler.rev);

    if (fieldId == "SHAPE") return ToHex(inst.macrosyn.shape);
    if (fieldId == "TIMBRE") return ToHex(inst.macrosyn.timbre);
    if (fieldId == "COLOR") return ToHex(inst.macrosyn.color);
    if (fieldId == "REDUX") return ToHex(inst.macrosyn.redux);

    return "--";
}

// Helper: Resolves the accent string for enums
static std::string ResolveInstrumentAccent(const std::string& fieldId, const engine::Instrument& inst, const std::string& fallback) {
    bool isMac = (inst.type == engine::InstType::INST_MACROSYN);
    
    if (fieldId == "FILTER") {
        int filter_type = isMac ? inst.macrosyn.filter_type : inst.sampler.filter_type;
        if (filter_type == 0) return "OFF";
        if (filter_type == 1) return "LP ";
        if (filter_type == 2) return "HP ";
        if (filter_type == 3) return "BP ";
    }
    if (fieldId == "PLAY") {
        if (inst.sampler.play == 0) return "FWD";
        if (inst.sampler.play == 1) return "REV";
    }
    if (fieldId == "LIM") {
        int lim = isMac ? inst.macrosyn.lim : inst.sampler.lim;
        if (lim == 0) return "CLIP";
        if (lim == 1) return "SIN ";
    }
    if (fieldId == "SLICE") {
        return inst.sampler.slice == 0 ? "OFF" : "ON ";
    }
    if (fieldId == "SHAPE") {
        if (inst.macrosyn.shape == 0) return "CSAW";
        if (inst.macrosyn.shape == 1) return "TRI ";
        if (inst.macrosyn.shape == 2) return "SIN ";
        if (inst.macrosyn.shape == 3) return "SQU ";
    }
    return fallback;
}

// Helper: Gets the raw integer value to calculate slider width
static int GetSliderValue(const std::string& fieldId, const engine::Instrument& inst) {
    bool isMac = (inst.type == engine::InstType::INST_MACROSYN);
    
    if (fieldId == "START") return inst.sampler.start;
    if (fieldId == "LOOP_ST") return inst.sampler.loop_st;
    if (fieldId == "LENGTH") return inst.sampler.length;
    if (fieldId == "DETUNE") return inst.sampler.detune;
    
    if (fieldId == "DEGRADE") return isMac ? inst.macrosyn.degrade : inst.sampler.degrade;
    if (fieldId == "CUTOFF") return isMac ? inst.macrosyn.cutoff : inst.sampler.cutoff;
    if (fieldId == "RES") return isMac ? inst.macrosyn.res : inst.sampler.res;
    if (fieldId == "AMP") return isMac ? inst.macrosyn.amp : inst.sampler.amp;
    if (fieldId == "PAN") return isMac ? inst.macrosyn.pan : inst.sampler.pan;
    if (fieldId == "DRY") return isMac ? inst.macrosyn.dry : inst.sampler.dry;
    if (fieldId == "CHO") return isMac ? inst.macrosyn.cho : inst.sampler.cho;
    if (fieldId == "DEL") return isMac ? inst.macrosyn.del : inst.sampler.del;
    if (fieldId == "REV") return isMac ? inst.macrosyn.rev : inst.sampler.rev;
    
    if (fieldId == "TIMBRE") return inst.macrosyn.timbre;
    if (fieldId == "COLOR") return inst.macrosyn.color;
    if (fieldId == "REDUX") return inst.macrosyn.redux;

    return 0;
}

void RenderInstrumentScreen(Renderer& renderer, 
                            const engine::EngineState& engState, 
                            int currentInstIndex,
                            const std::string& active_cursor_id) {
                            
    const engine::Instrument& currentInst = engState.instruments[currentInstIndex];
    bool isMac = (currentInst.type == engine::InstType::INST_MACROSYN);
                            
    const std::vector<UI_GridCell>& staticText = isMac ? GetMacrosynStaticText() : GetSamplerStaticText();
    const std::vector<UI_GridCell>& dynamicText = isMac ? GetMacrosynDynamicTextDefaults() : GetSamplerDynamicTextDefaults();
    const std::unordered_map<std::string, std::vector<UI_GridCell>>& interactiveFields = isMac ? GetMacrosynInteractiveFields() : GetSamplerInteractiveFields();

    // Render Static Background Text
    for (const auto& cell : staticText) {
        renderer.drawString(cell.text, cell.col, cell.row, GetColorFromString(cell.normal_color));
    }

    // Render Dynamic Text (Title, Tempo)
    for (const auto& cell : dynamicText) {
        std::string textToDraw = cell.text;
        if (cell.text == "13") {
            textToDraw = ToHex(currentInstIndex);
        } else if (cell.text.substr(0, 2) == "T>") {
            textToDraw = "T>" + std::to_string(engState.bpm);
        }
        renderer.drawString(textToDraw, cell.col, cell.row, GetColorFromString(cell.normal_color));
    }

    // Render Interactive Fields
    for (const auto& [fieldId, components] : interactiveFields) {
        bool isActive = (fieldId == active_cursor_id);
        std::string liveText = ResolveInstrumentValue(fieldId, currentInst);

        for (const auto& comp : components) {
            SDL_Color color = GetColorFromString(isActive ? comp.selected_color : comp.normal_color);

            if (comp.role == "slider") {
                int px = comp.col * 8;
                int py = comp.row * 8;
                int max_pixels = comp.width * 8; 
                
                int val = GetSliderValue(fieldId, currentInst);
                int fill_pixels = (val * max_pixels) / 255;
                
                renderer.fillRectPixel(px, py, fill_pixels, 8, color);
            } 
            else {
                std::string drawText = comp.text;
                if (comp.role == "value") drawText = liveText;
                else if (comp.role == "accent") drawText = ResolveInstrumentAccent(fieldId, currentInst, comp.text);

                renderer.drawString(drawText, comp.col, comp.row, color);

                if (isActive && comp.has_cursor_box) {
                    renderer.drawBracket(comp.col, comp.row, drawText.length(), {0, 255, 255, 255});
                }
            }
        }
    }
}

} // namespace instrument
} // namespace ui
} // namespace m8
