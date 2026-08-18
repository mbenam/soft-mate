#include "TableScreen.h"
#include "TableScreenLayout.h"
#include "ui/HexFmt.h"
#include "ui/UiEditHelpers.h"
#include "ui/Theme.h"
#include <iomanip>
#include <sstream>

namespace m8 {
namespace ui {
namespace table {

static SDL_Color GetColorFromString(const std::string& colorName) {
    return GetThemeColor(colorName);
}

void RenderTableScreen(Renderer& renderer, 
                       const engine::Sequencer& uiSequencer,
                       const engine::EngineState& engState,
                       int currentTableIndex,
                       int cursor_x, int cursor_y,
                       int activeTableRow) {
    
    static UI_GridCell TableGrid[16][8];
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
        
        // Dynamic Table Index
        if (cell.col == 6 && cell.row == 0) {
            std::stringstream ss;
            ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (currentTableIndex & 0xFF);
            textToDraw = ss.str();
        }
        
        // Highlight active column header
        if (cell.row == 2) {
            if (cell.text == "N" && cursor_x == 0) c = GetColorFromString("LABEL_LITE");
            else if (cell.text == "V" && cursor_x == 1) c = GetColorFromString("LABEL_LITE");
            else if (cell.text == "FX1" && (cursor_x == 2 || cursor_x == 3)) c = GetColorFromString("LABEL_LITE");
            else if (cell.text == "FX2" && (cursor_x == 4 || cursor_x == 5)) c = GetColorFromString("LABEL_LITE");
            else if (cell.text == "FX3" && (cursor_x == 6 || cursor_x == 7)) c = GetColorFromString("LABEL_LITE");
        }
        
        // Highlight active row header
        if (cell.col == 1 && cell.row >= 3 && cell.row <= 18) {
            int r = cell.row - 3;
            if (r == cursor_y) c = GetColorFromString("LABEL_LITE");
        }
        
        renderer.drawString(textToDraw, cell.col, cell.row, c);
    }

    // Draw active table playhead arrow
    if (activeTableRow >= 0 && activeTableRow < 16) {
        int px = 0;
        int py = (activeTableRow + 3) * 8 + 1;
        SDL_Color cGreen = {0, 255, 100, 255};
        for (int ty = 0; ty < 7; ++ty) {
            int w = (ty < 4) ? (ty + 1) : (7 - ty);
            renderer.drawLinePixel(px, py + ty, px + w - 1, py + ty, cGreen);
        }
    }

    // 3. Draw 16x8 Interactive Grid
    for (int y = 0; y < 16; ++y) {
        const engine::TableStep& step = uiSequencer.tables[currentTableIndex][y];
        for (int x = 0; x < 8; ++x) {
            const UI_GridCell& cell = TableGrid[y][x];
            
            std::string val;
            std::string emptyVal;
            
            // Map column x to the appropriate struct field
            if (x == 0) { val = m8::ui::HexS8(step.transp, "00"); emptyVal = "00"; }
            else if (x == 1) { val = m8::ui::HexU8(step.vol, "--"); emptyVal = "--"; }
            else if (x == 2) { val = m8::ui::FxName(step.fx[0].cmd); emptyVal = "---"; }
            else if (x == 3) { val = m8::ui::HexU8(step.fx[0].val, "00"); emptyVal = "00"; }
            else if (x == 4) { val = m8::ui::FxName(step.fx[1].cmd); emptyVal = "---"; }
            else if (x == 5) { val = m8::ui::HexU8(step.fx[1].val, "00"); emptyVal = "00"; }
            else if (x == 6) { val = m8::ui::FxName(step.fx[2].cmd); emptyVal = "---"; }
            else if (x == 7) { val = m8::ui::HexU8(step.fx[2].val, "00"); emptyVal = "00"; }
            
            bool isSelected = (cursor_x == x && cursor_y == y);
            bool isDim = (val == "--" || val == "---" || val == emptyVal);
            SDL_Color color = isSelected ? GetColorFromString(cell.selected_color) 
                                         : GetColorFromString(isDim ? "LABEL_DIM" : "VALUE");

            renderer.drawString(val, cell.col, cell.row, color);
            
            if (isSelected) {
                // Draw cursor bracket around the currently selected cell
                renderer.drawBracket(cell.col, cell.row, val.length(), GetThemeColor("CURSOR"));
            }
        }
    }
}

void HandleTableInput(const SDL_Event& event,
                      bool editHeld, bool optHeld, bool shiftHeld,
                      bool& arrowPressedDuringEdit,
                      engine::Sequencer& uiSequencer,
                      int& currentTableIndex,
                      int& cursor_x, int& cursor_y,
                      CommandSink& commandSink) {
    auto& tables = uiSequencer.tables;
    auto pushStep = [&]() {
        m8::engine::EngineCommand cmd;
        cmd.type = m8::engine::CommandType::SET_TABLE_STEP;
        cmd.targetId = currentTableIndex;
        cmd.row = cursor_y;
        cmd.u.tableStep = tables[currentTableIndex][cursor_y];
        commandSink.send(cmd);
    };

    // Option navigation: switch table
    if (optHeld && !editHeld) {
        if (event.key.key == SDLK_LEFT) {
            currentTableIndex = (currentTableIndex - 1 + 256) % 256;
            return;
        } else if (event.key.key == SDLK_RIGHT) {
            currentTableIndex = (currentTableIndex + 1) % 256;
            return;
        } else if (event.key.key == SDLK_UP) {
            currentTableIndex = (currentTableIndex - 16 + 256) % 256;
            return;
        } else if (event.key.key == SDLK_DOWN) {
            currentTableIndex = (currentTableIndex + 16) % 256;
            return;
        }
    }

    // Delete cell value
    if ((editHeld && optHeld) || (editHeld && (event.key.key == SDLK_DELETE || event.key.key == SDLK_BACKSPACE))) {
        DeleteTableValue(tables[currentTableIndex][cursor_y], cursor_x);
        arrowPressedDuringEdit = true;
        pushStep();
        return;
    }

    if (event.key.key == SDLK_DOWN) {
        if (editHeld) { ModifyTableValue(tables[currentTableIndex][cursor_y], cursor_x, -1, true); arrowPressedDuringEdit = true; pushStep(); }
        else { cursor_y = (cursor_y + 1) % 16; }
    } else if (event.key.key == SDLK_UP) {
        if (editHeld) { ModifyTableValue(tables[currentTableIndex][cursor_y], cursor_x, 1, true); arrowPressedDuringEdit = true; pushStep(); }
        else { cursor_y = (cursor_y - 1 + 16) % 16; }
    } else if (event.key.key == SDLK_RIGHT) {
        if (editHeld) { ModifyTableValue(tables[currentTableIndex][cursor_y], cursor_x, 1, false); arrowPressedDuringEdit = true; pushStep(); }
        else { cursor_x = (cursor_x + 1) % 8; }
    } else if (event.key.key == SDLK_LEFT) {
        if (editHeld) { ModifyTableValue(tables[currentTableIndex][cursor_y], cursor_x, -1, false); arrowPressedDuringEdit = true; pushStep(); }
        else { cursor_x = (cursor_x - 1 + 8) % 8; }
    }
}

void HandleTableEditRelease(engine::TableStep& step,
                            int currentTableIndex,
                            int cursor_x, int cursor_y,
                            CommandSink& commandSink) {
    InsertTableDefault(step, cursor_x);

    m8::engine::EngineCommand cmd;
    cmd.type = m8::engine::CommandType::SET_TABLE_STEP;
    cmd.targetId = currentTableIndex;
    cmd.row = cursor_y;
    cmd.u.tableStep = step;
    commandSink.send(cmd);
}

} // namespace table
} // namespace ui
} // namespace m8
