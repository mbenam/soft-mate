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

static int ResolveMixerValue(const std::string& fieldId, const engine::MixerState& mx) {
    if (fieldId == "OUT_VOL") return mx.out_vol;
    if (fieldId.find("TRK_VOL_") == 0) return mx.track_vol[fieldId.back() - '0'];
    if (fieldId == "MST_CHO") return mx.cho_vol;
    if (fieldId == "MST_DEL") return mx.del_vol;
    if (fieldId == "MST_REV") return mx.rev_vol;
    if (fieldId == "IN_VOL") return mx.in_vol;
    if (fieldId == "IN_CHO") return mx.in_cho;
    if (fieldId == "IN_DEL") return mx.in_del;
    if (fieldId == "IN_REV") return mx.in_rev;
    if (fieldId == "USB_VOL") return mx.usb_vol;
    if (fieldId == "USB_CHO") return mx.usb_cho;
    if (fieldId == "USB_DEL") return mx.usb_del;
    if (fieldId == "USB_REV") return mx.usb_rev;
    if (fieldId == "MIX_VOL") return mx.mix_vol;
    if (fieldId == "LIM_VAL") return mx.lim_val;
    if (fieldId == "DJF_FREQ") return mx.djf_freq;
    if (fieldId == "DJF_RES") return mx.djf_res;
    if (fieldId == "DJF_TYP") return mx.djf_typ;
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
                       const std::string& active_cursor_id) {
                            
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

} // namespace mixer
} // namespace ui
} // namespace m8
