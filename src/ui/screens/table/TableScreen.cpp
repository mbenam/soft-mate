#include "TableScreen.h"
#include "TableScreenLayout.h"
#include "ui/HexFmt.h"
#include <iomanip>
#include <sstream>

namespace m8 {
namespace ui {
namespace table {

static SDL_Color GetColorFromString(const std::string& colorName) {
    if (colorName == "TITLE") return {255, 60, 60, 255}; 
    if (colorName == "LABEL_DIM") return {100, 100, 100, 255}; 
    if (colorName == "LABEL_LITE") return {0, 255, 255, 255}; 
    if (colorName == "VALUE") return {255, 255, 255, 255}; 
    return {255, 255, 255, 255};
}

void RenderTableScreen(Renderer& renderer, 
                       const engine::Sequencer& uiSequencer,
                       const engine::EngineState& engState,
                       int currentTableIndex,
                       int cursor_x, int cursor_y) {
    
    static UI_GridCell TableGrid[16][5];
    static bool initialized = false;
    static std::vector<UI_GridCell> staticText;
    static std::vector<UI_GridCell> dynamicText;
    
    if (!initialized) {
        InitTableGrid(TableGrid);
        staticText = GetStaticText();
        dynamicText = GetDynamicTextDefaults();
        initialized = true;
    }

    // 1. Draw Static Text
    for (const auto& cell : staticText) {
        renderer.drawString(cell.text, cell.col, cell.row, GetColorFromString(cell.normal_color));
    }

    // 2. Draw Dynamic Headers
    for (const auto& cell : dynamicText) {
        std::string textToDraw = cell.text;
        SDL_Color c = GetColorFromString(cell.normal_color);
        
        // Dynamic Title & Tempo
        if (cell.text == "13") {
            std::stringstream ss;
            ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << currentTableIndex;
            textToDraw = ss.str();
        } else if (cell.text.substr(0, 2) == "T>") {
            textToDraw = "T>" + std::to_string(engState.bpm);
        }
        
        // Highlight active column header
        if (cell.row == 2) {
            if (cell.text == "N" && cursor_x == 0) c = GetColorFromString("LABEL_LITE");
            else if (cell.text == "V" && cursor_x == 1) c = GetColorFromString("LABEL_LITE");
            else if (cell.text == "FX1" && cursor_x == 2) c = GetColorFromString("LABEL_LITE");
            else if (cell.text == "FX2" && cursor_x == 3) c = GetColorFromString("LABEL_LITE");
            else if (cell.text == "FX3" && cursor_x == 4) c = GetColorFromString("LABEL_LITE");
        }
        
        // Highlight active row header
        if (cell.col == 0 && cell.row >= 3 && cell.row <= 18) {
            int r = cell.row - 3;
            if (r == cursor_y) c = GetColorFromString("LABEL_LITE");
        }
        
        renderer.drawString(textToDraw, cell.col, cell.row, c);
    }

    // 3. Draw 16x5 Interactive Grid
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 5; ++x) {
            const UI_GridCell& cell = TableGrid[y][x];
            const engine::TableStep& step = uiSequencer.tables[currentTableIndex][y];
            
            std::string val;
            std::string emptyVal;
            
            // Map column x to the appropriate struct field
            if (x == 0) { val = m8::ui::HexS8(step.transp, "00"); emptyVal = "00"; }
            else if (x == 1) { val = m8::ui::HexU8(step.vol, "--"); emptyVal = "--"; }
            else if (x == 2) { val = m8::ui::FormatFx(step.fx[0]); emptyVal = "---00"; }
            else if (x == 3) { val = m8::ui::FormatFx(step.fx[1]); emptyVal = "---00"; }
            else if (x == 4) { val = m8::ui::FormatFx(step.fx[2]); emptyVal = "---00"; }
            
            bool isSelected = (cursor_x == x && cursor_y == y);
            SDL_Color color = isSelected ? GetColorFromString(cell.selected_color) 
                                         : GetColorFromString(val == emptyVal ? "LABEL_DIM" : "VALUE");

            renderer.drawString(val, cell.col, cell.row, color);
            
            if (isSelected) {
                // Draw cursor bracket around the currently selected cell
                renderer.drawBracket(cell.col, cell.row, val.length(), GetColorFromString("LABEL_LITE"));
            }
        }
    }
}

} // namespace table
} // namespace ui
} // namespace m8
