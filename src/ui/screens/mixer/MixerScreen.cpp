#include "MixerScreen.h"
#include "MixerScreenLayout.h"
#include <iomanip>
#include <sstream>

namespace m8 {
namespace ui {
namespace mixer {

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

static int ResolveMixerValue(CursorId fieldId, const engine::MixerState& mx) {
    using C = CursorId;
    if (fieldId == C::OUT_VOL) return mx.out_vol;
    if (IsTrackVolCursor(fieldId)) return mx.track_vol[TrackIndexOf(fieldId)];
    if (fieldId == C::MST_CHO) return mx.cho_vol;
    if (fieldId == C::MST_DEL) return mx.del_vol;
    if (fieldId == C::MST_REV) return mx.rev_vol;
    if (fieldId == C::IN_VOL) return mx.in_vol;
    if (fieldId == C::IN_CHO) return mx.in_cho;
    if (fieldId == C::IN_DEL) return mx.in_del;
    if (fieldId == C::IN_REV) return mx.in_rev;
    if (fieldId == C::USB_VOL) return mx.usb_vol;
    if (fieldId == C::USB_CHO) return mx.usb_cho;
    if (fieldId == C::USB_DEL) return mx.usb_del;
    if (fieldId == C::USB_REV) return mx.usb_rev;
    if (fieldId == C::MIX_VOL) return mx.mix_vol;
    if (fieldId == C::LIM_VAL) return mx.lim_val;
    if (fieldId == C::DJF_FREQ) return mx.djf_freq;
    if (fieldId == C::DJF_RES) return mx.djf_res;
    if (fieldId == C::DJF_TYP) return mx.djf_typ;
    return 0;
}

static void DrawVerticalBar(Renderer& renderer, int col, int rowBottom, int rowHeight, int val, bool doubleWidth = false) {
    int px = col * 8;
    int pyBottom = (rowBottom + 1) * 8;
    int maxHeight = rowHeight * 8;
    int w = doubleWidth ? 16 : 8;
    
    // Background Dark Slate
    SDL_Color bgCol = {40, 50, 50, 255}; 
    renderer.fillRectPixel(px, pyBottom - maxHeight, w - 1, maxHeight, bgCol);
    
    // Foreground Fill
    int fillHeight = (val * maxHeight) / 255;
    if (fillHeight > 0) {
        SDL_Color fillCol = {180, 200, 200, 255}; 
        renderer.fillRectPixel(px, pyBottom - fillHeight, w - 1, fillHeight, fillCol);
    }
}

void RenderMixerScreen(Renderer& renderer,
                       const engine::EngineState& engState,
                       CursorId active_cursor_id) {
                            
    const engine::MixerState& mx = engState.mixer;
                            
    static std::vector<UI_GridCell> staticText = GetMixerStaticText();
    static auto interactiveFields = GetMixerInteractiveFields();

    for (const auto& cell : staticText) {
        renderer.drawString(cell.text, cell.col, cell.row, GetColorFromString(cell.normal_color));
    }

    // Dynamic Tempo
    

    // Draw Output Vol Bars (Double width, 12 rows high, row 5 to 16)
    DrawVerticalBar(renderer, 12, 16, 12, mx.out_vol, true);

    // Draw Track Vol Bars (1 char width, 8 rows high, row 9 to 16)
    for (int i = 0; i < 8; i++) {
        DrawVerticalBar(renderer, i * 3, 16, 8, mx.track_vol[i], false);
    }

    // Render Interactive Fields
    for (const auto& [fieldId, components] : interactiveFields) {
        bool isActive = (fieldId == active_cursor_id);
        int val = ResolveMixerValue(fieldId, mx);
        std::string liveText = ToHex(val);

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

void HandleMixerInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                       engine::EngineState& uiEngineState, CursorId& cursor_id,
                       CommandSink& commandSink) {
    using C = CursorId;
    auto navMap = GetMixerNavMap();
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

    if (editHeld && (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN || event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT)) {
        arrowPressedDuringEdit = true;
        int step = (event.key.key == SDLK_RIGHT || event.key.key == SDLK_UP) ? 1 : -1;

        const auto& mx = uiEngineState.mixer;
        if (cursor_id == C::OUT_VOL) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MIX_OUT_VOL, std::clamp<int>((int)mx.out_vol + step, 0, 255));
        else if (IsTrackVolCursor(cursor_id)) {
            int t = TrackIndexOf(cursor_id);
            PushParam(commandSink, uiEngineState, m8::engine::ParamID::MIX_TRK_VOL, std::clamp<int>((int)mx.track_vol[t] + step, 0, 255), 0, t);
        }
        else if (cursor_id == C::MST_CHO) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MIX_CHO_VOL, std::clamp<int>((int)mx.cho_vol + step, 0, 255));
        else if (cursor_id == C::MST_DEL) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MIX_DEL_VOL, std::clamp<int>((int)mx.del_vol + step, 0, 255));
        else if (cursor_id == C::MST_REV) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MIX_REV_VOL, std::clamp<int>((int)mx.rev_vol + step, 0, 255));
        else if (cursor_id == C::IN_VOL) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MIX_IN_VOL, std::clamp<int>((int)mx.in_vol + step, 0, 255));
        else if (cursor_id == C::IN_CHO) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MIX_IN_CHO, std::clamp<int>((int)mx.in_cho + step, 0, 255));
        else if (cursor_id == C::IN_DEL) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MIX_IN_DEL, std::clamp<int>((int)mx.in_del + step, 0, 255));
        else if (cursor_id == C::IN_REV) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MIX_IN_REV, std::clamp<int>((int)mx.in_rev + step, 0, 255));
        else if (cursor_id == C::USB_VOL) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MIX_USB_VOL, std::clamp<int>((int)mx.usb_vol + step, 0, 255));
        else if (cursor_id == C::USB_CHO) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MIX_USB_CHO, std::clamp<int>((int)mx.usb_cho + step, 0, 255));
        else if (cursor_id == C::USB_DEL) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MIX_USB_DEL, std::clamp<int>((int)mx.usb_del + step, 0, 255));
        else if (cursor_id == C::USB_REV) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MIX_USB_REV, std::clamp<int>((int)mx.usb_rev + step, 0, 255));
        else if (cursor_id == C::MIX_VOL) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MIX_MIX_VOL, std::clamp<int>((int)mx.mix_vol + step, 0, 255));
        else if (cursor_id == C::LIM_VAL) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MIX_LIM_VAL, std::clamp<int>((int)mx.lim_val + step, 0, 255));
        else if (cursor_id == C::DJF_FREQ) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MIX_DJF_FREQ, std::clamp<int>((int)mx.djf_freq + step, 0, 255));
        else if (cursor_id == C::DJF_RES) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MIX_DJF_RES, std::clamp<int>((int)mx.djf_res + step, 0, 255));
        else if (cursor_id == C::DJF_TYP) PushParam(commandSink, uiEngineState, m8::engine::ParamID::MIX_DJF_TYP, std::clamp<int>((int)mx.djf_typ + step, 0, 255));
    }
}

} // namespace mixer
} // namespace ui
} // namespace m8
