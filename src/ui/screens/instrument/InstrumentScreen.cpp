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
static std::string ResolveInstrumentValue(CursorId fieldId, const engine::Instrument& inst) {
    using C = CursorId;
    bool isMac = (inst.type == engine::InstType::INST_MACROSYN);

    if (fieldId == C::TYPE) return isMac ? "MACROSYN" : "SAMPLER ";
    if (fieldId == C::NAME) return inst.name;
    if (fieldId == C::CMD_LOAD) return "LOAD";
    if (fieldId == C::CMD_SAVE) return "SAVE";
    if (fieldId == C::SAMPLE_LOAD) return "LOAD";
    if (fieldId == C::SAMPLE_REC) return "REC.";

    if (fieldId == C::TRANSP) return (isMac ? inst.macrosyn.transp : inst.sampler.transp) ? "ON" : "OFF";
    int eq = isMac ? inst.macrosyn.eq : inst.sampler.eq;
    if (fieldId == C::EQ) return eq == 0 ? "--" : ToHex(eq);

    // Enums that have separate string accents
    if (fieldId == C::FILTER) return ToHex(isMac ? inst.macrosyn.filter_type : inst.sampler.filter_type);
    if (fieldId == C::PLAY) return ToHex(inst.sampler.play);
    if (fieldId == C::LIM) return ToHex(isMac ? inst.macrosyn.lim : inst.sampler.lim);
    if (fieldId == C::SLICE) return ToHex(inst.sampler.slice);

    // Standard Hex values
    if (fieldId == C::TBL_TIC) return ToHex(isMac ? inst.macrosyn.tbl_tic : inst.sampler.tbl_tic);
    if (fieldId == C::START) return ToHex(inst.sampler.start);
    if (fieldId == C::LOOP_ST) return ToHex(inst.sampler.loop_st);
    if (fieldId == C::LENGTH) return ToHex(inst.sampler.length);
    if (fieldId == C::DETUNE) return ToHex(inst.sampler.detune);
    if (fieldId == C::DEGRADE) return ToHex(isMac ? inst.macrosyn.degrade : inst.sampler.degrade);
    if (fieldId == C::CUTOFF) return ToHex(isMac ? inst.macrosyn.cutoff : inst.sampler.cutoff);
    if (fieldId == C::RES) return ToHex(isMac ? inst.macrosyn.res : inst.sampler.res);
    if (fieldId == C::AMP) return ToHex(isMac ? inst.macrosyn.amp : inst.sampler.amp);
    if (fieldId == C::PAN) return ToHex(isMac ? inst.macrosyn.pan : inst.sampler.pan);
    if (fieldId == C::DRY) return ToHex(isMac ? inst.macrosyn.dry : inst.sampler.dry);
    if (fieldId == C::CHO) return ToHex(isMac ? inst.macrosyn.cho : inst.sampler.cho);
    if (fieldId == C::DEL) return ToHex(isMac ? inst.macrosyn.del : inst.sampler.del);
    if (fieldId == C::REV) return ToHex(isMac ? inst.macrosyn.rev : inst.sampler.rev);

    if (fieldId == C::SHAPE) return ToHex(inst.macrosyn.shape);
    if (fieldId == C::TIMBRE) return ToHex(inst.macrosyn.timbre);
    if (fieldId == C::COLOR) return ToHex(inst.macrosyn.color);
    if (fieldId == C::REDUX) return ToHex(inst.macrosyn.redux);

    return "--";
}

// Helper: Resolves the accent string for enums
static std::string ResolveInstrumentAccent(CursorId fieldId, const engine::Instrument& inst, const std::string& fallback) {
    using C = CursorId;
    bool isMac = (inst.type == engine::InstType::INST_MACROSYN);

    if (fieldId == C::FILTER) {
        int filter_type = isMac ? inst.macrosyn.filter_type : inst.sampler.filter_type;
        if (filter_type == 0) return "OFF";
        if (filter_type == 1) return "LP ";
        if (filter_type == 2) return "HP ";
        if (filter_type == 3) return "BP ";
    }
    if (fieldId == C::PLAY) {
        if (inst.sampler.play == 0) return "FWD";
        if (inst.sampler.play == 1) return "REV";
    }
    if (fieldId == C::LIM) {
        int lim = isMac ? inst.macrosyn.lim : inst.sampler.lim;
        if (lim == 0) return "CLIP";
        if (lim == 1) return "SIN ";
    }
    if (fieldId == C::SLICE) {
        return inst.sampler.slice == 0 ? "OFF" : "ON ";
    }
    if (fieldId == C::SHAPE) {
        if (inst.macrosyn.shape == 0) return "CSAW";
        if (inst.macrosyn.shape == 1) return "TRI ";
        if (inst.macrosyn.shape == 2) return "SIN ";
        if (inst.macrosyn.shape == 3) return "SQU ";
    }
    return fallback;
}

// Helper: Gets the raw integer value to calculate slider width
static int GetSliderValue(CursorId fieldId, const engine::Instrument& inst) {
    using C = CursorId;
    bool isMac = (inst.type == engine::InstType::INST_MACROSYN);

    if (fieldId == C::START) return inst.sampler.start;
    if (fieldId == C::LOOP_ST) return inst.sampler.loop_st;
    if (fieldId == C::LENGTH) return inst.sampler.length;
    if (fieldId == C::DETUNE) return inst.sampler.detune;

    if (fieldId == C::DEGRADE) return isMac ? inst.macrosyn.degrade : inst.sampler.degrade;
    if (fieldId == C::CUTOFF) return isMac ? inst.macrosyn.cutoff : inst.sampler.cutoff;
    if (fieldId == C::RES) return isMac ? inst.macrosyn.res : inst.sampler.res;
    if (fieldId == C::AMP) return isMac ? inst.macrosyn.amp : inst.sampler.amp;
    if (fieldId == C::PAN) return isMac ? inst.macrosyn.pan : inst.sampler.pan;
    if (fieldId == C::DRY) return isMac ? inst.macrosyn.dry : inst.sampler.dry;
    if (fieldId == C::CHO) return isMac ? inst.macrosyn.cho : inst.sampler.cho;
    if (fieldId == C::DEL) return isMac ? inst.macrosyn.del : inst.sampler.del;
    if (fieldId == C::REV) return isMac ? inst.macrosyn.rev : inst.sampler.rev;

    if (fieldId == C::TIMBRE) return inst.macrosyn.timbre;
    if (fieldId == C::COLOR) return inst.macrosyn.color;
    if (fieldId == C::REDUX) return inst.macrosyn.redux;

    return 0;
}

void RenderInstrumentScreen(Renderer& renderer,
                            const engine::EngineState& engState,
                            int currentInstIndex,
                            CursorId active_cursor_id) {

    const engine::Instrument& currentInst = engState.instruments[currentInstIndex];
    bool isMac = (currentInst.type == engine::InstType::INST_MACROSYN);

    const std::vector<UI_GridCell>& staticText = isMac ? GetMacrosynStaticText() : GetSamplerStaticText();
    const std::vector<UI_GridCell>& dynamicText = isMac ? GetMacrosynDynamicTextDefaults() : GetSamplerDynamicTextDefaults();
    const std::unordered_map<CursorId, std::vector<UI_GridCell>>& interactiveFields = isMac ? GetMacrosynInteractiveFields() : GetSamplerInteractiveFields();

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

void HandleInstrumentInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                            engine::EngineState& uiEngineState, int currentInstIndex,
                            CursorId& cursor_id, CommandSink& commandSink,
                            ViewManager& viewManager) {
    using C = CursorId;
    bool isMac = (uiEngineState.instruments[currentInstIndex].type == m8::engine::InstType::INST_MACROSYN);
    auto navMap = isMac ? GetMacrosynNavMap() : GetSamplerNavMap();

    if (editHeld && (event.key.key == SDLK_RIGHT || event.key.key == SDLK_UP || event.key.key == SDLK_LEFT || event.key.key == SDLK_DOWN)) {
        arrowPressedDuringEdit = true;
        int step = (event.key.key == SDLK_RIGHT || event.key.key == SDLK_UP) ? 1 : -1;
        const m8::engine::Instrument& inst = uiEngineState.instruments[currentInstIndex];
        bool isMac2 = (inst.type == m8::engine::InstType::INST_MACROSYN);

        if (cursor_id == C::TYPE) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_TYPE, std::clamp<int>(static_cast<int>(inst.type) + step, 0, 1), currentInstIndex);
        else if (cursor_id == C::TRANSP) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_TRANSP, std::clamp<int>((isMac2 ? inst.macrosyn.transp : inst.sampler.transp) + step, 0, 1), currentInstIndex);
        else if (cursor_id == C::TBL_TIC) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_TBL_TIC, std::clamp<int>((isMac2 ? inst.macrosyn.tbl_tic : inst.sampler.tbl_tic) + step, 0, 255), currentInstIndex);
        else if (cursor_id == C::EQ) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_EQ, std::clamp<int>((isMac2 ? inst.macrosyn.eq : inst.sampler.eq) + step, 0, 255), currentInstIndex);
        else if (cursor_id == C::AMP) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_AMP, std::clamp<int>((isMac2 ? inst.macrosyn.amp : inst.sampler.amp) + step, 0, 255), currentInstIndex);
        else if (cursor_id == C::LIM) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_LIM, std::clamp<int>((isMac2 ? inst.macrosyn.lim : inst.sampler.lim) + step, 0, 1), currentInstIndex);
        else if (cursor_id == C::PAN) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_PAN, std::clamp<int>((isMac2 ? inst.macrosyn.pan : inst.sampler.pan) + step, 0, 255), currentInstIndex);
        else if (cursor_id == C::DRY) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_DRY, std::clamp<int>((isMac2 ? inst.macrosyn.dry : inst.sampler.dry) + step, 0, 255), currentInstIndex);
        else if (cursor_id == C::CHO) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_CHO, std::clamp<int>((isMac2 ? inst.macrosyn.cho : inst.sampler.cho) + step, 0, 255), currentInstIndex);
        else if (cursor_id == C::DEL) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_DEL, std::clamp<int>((isMac2 ? inst.macrosyn.del : inst.sampler.del) + step, 0, 255), currentInstIndex);
        else if (cursor_id == C::REV) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_REV, std::clamp<int>((isMac2 ? inst.macrosyn.rev : inst.sampler.rev) + step, 0, 255), currentInstIndex);
        else if (cursor_id == C::DEGRADE) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_DEGRADE, std::clamp<int>((isMac2 ? inst.macrosyn.degrade : inst.sampler.degrade) + step, 0, 255), currentInstIndex);
        else if (cursor_id == C::FILTER) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_FILTER, std::clamp<int>((isMac2 ? inst.macrosyn.filter_type : inst.sampler.filter_type) + step, 0, 3), currentInstIndex);
        else if (cursor_id == C::CUTOFF) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_CUTOFF, std::clamp<int>((isMac2 ? inst.macrosyn.cutoff : inst.sampler.cutoff) + step, 0, 255), currentInstIndex);
        else if (cursor_id == C::RES) PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_RES, std::clamp<int>((isMac2 ? inst.macrosyn.res : inst.sampler.res) + step, 0, 255), currentInstIndex);
        else if (!isMac2 && cursor_id == C::SLICE) PushParam(commandSink, uiEngineState, m8::engine::ParamID::SAMP_SLICE, std::clamp<int>(inst.sampler.slice + step, 0, 255), currentInstIndex);
        else if (!isMac2 && cursor_id == C::PLAY) PushParam(commandSink, uiEngineState, m8::engine::ParamID::SAMP_PLAY, std::clamp<int>(inst.sampler.play + step, 0, 1), currentInstIndex);
        else if (!isMac2 && cursor_id == C::START) PushParam(commandSink, uiEngineState, m8::engine::ParamID::SAMP_START, std::clamp<int>(inst.sampler.start + step, 0, 255), currentInstIndex);
        else if (!isMac2 && cursor_id == C::LOOP_ST) PushParam(commandSink, uiEngineState, m8::engine::ParamID::SAMP_LOOP_ST, std::clamp<int>(inst.sampler.loop_st + step, 0, 255), currentInstIndex);
        else if (!isMac2 && cursor_id == C::LENGTH) PushParam(commandSink, uiEngineState, m8::engine::ParamID::SAMP_LENGTH, std::clamp<int>(inst.sampler.length + step, 0, 255), currentInstIndex);
        else if (!isMac2 && cursor_id == C::DETUNE) PushParam(commandSink, uiEngineState, m8::engine::ParamID::SAMP_DETUNE, std::clamp<int>(inst.sampler.detune + step, 0, 255), currentInstIndex);
        else if (isMac2 && cursor_id == C::SHAPE) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MAC_SHAPE, std::clamp<int>(inst.macrosyn.shape + step, 0, 3), currentInstIndex);
        else if (isMac2 && cursor_id == C::TIMBRE) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MAC_TIMBRE, std::clamp<int>(inst.macrosyn.timbre + step, 0, 255), currentInstIndex);
        else if (isMac2 && cursor_id == C::COLOR) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MAC_COLOR, std::clamp<int>(inst.macrosyn.color + step, 0, 255), currentInstIndex);
        else if (isMac2 && cursor_id == C::REDUX) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MAC_REDUX, std::clamp<int>(inst.macrosyn.redux + step, 0, 255), currentInstIndex);
    } else {
        if (event.key.key == SDLK_DOWN) {
            if (navMap.count(cursor_id) && navMap[cursor_id].down != C::NONE) {
                cursor_id = navMap[cursor_id].down;
            }
        } else if (event.key.key == SDLK_UP) {
            if (navMap.count(cursor_id) && navMap[cursor_id].up != C::NONE) {
                cursor_id = navMap[cursor_id].up;
            }
        } else if (event.key.key == SDLK_RIGHT) {
            if (navMap.count(cursor_id) && navMap[cursor_id].right != C::NONE) {
                cursor_id = navMap[cursor_id].right;
            }
        } else if (event.key.key == SDLK_LEFT) {
            if (navMap.count(cursor_id) && navMap[cursor_id].left != C::NONE) {
                cursor_id = navMap[cursor_id].left;
            }
        } else if (event.key.key == SDLK_RETURN) {
            if (cursor_id == C::SAMPLE_LOAD || cursor_id == C::CMD_LOAD) {
                viewManager.pushModal(m8::ui::ViewType::FILE_BROWSER);
            }
        }
    }
}

void HandleInstrumentEditRelease(CursorId cursor_id, bool& browserForSongLoad,
                                  ::FileBrowser& fileBrowser, ViewManager& viewManager) {
    if (cursor_id == CursorId::SAMPLE_LOAD || cursor_id == CursorId::CMD_LOAD) {
        browserForSongLoad = false;
        fileBrowser.init("Samples", ".WAV");
        fileBrowser.setTitle("LOAD SAMPLE");
        viewManager.pushModal(m8::ui::ViewType::FILE_BROWSER);
    }
}

} // namespace instrument
} // namespace ui
} // namespace m8
