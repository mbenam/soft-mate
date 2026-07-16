#include "GrooveScreen.h"
#include "GrooveScreenLayout.h"
#include "ui/HexFmt.h"
#include "ui/UiEditHelpers.h"
#include <iomanip>
#include <sstream>

namespace m8 {
namespace ui {
namespace groove {

static SDL_Color GetColorFromString(const std::string& colorName) {
    if (colorName == "TITLE") return {255, 60, 60, 255}; 
    if (colorName == "LABEL_DIM") return {100, 100, 100, 255}; 
    if (colorName == "LABEL_LITE") return {0, 255, 255, 255}; 
    if (colorName == "VALUE") return {255, 255, 255, 255}; 
    return {255, 255, 255, 255};
}

void RenderGrooveScreen(Renderer& renderer, 
                        const engine::EngineState& engState,
                        const engine::Groove& grooveData,
                        int currentGrooveIndex,
                        int cursor_y) {
    
    static UI_GridCell GrooveGrid[16][1];
    static bool initialized = false;
    static std::vector<UI_GridCell> staticText;
    static std::vector<UI_GridCell> dynamicText;
    
    if (!initialized) {
        InitGrooveGrid(GrooveGrid);
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
        if (cell.text == "00" && cell.col == 7) {
            std::stringstream ss;
            ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << currentGrooveIndex;
            textToDraw = ss.str();
        } else if (cell.text.substr(0, 2) == "T>") {
            textToDraw = "T>" + std::to_string(engState.bpm);
        }
        
        // Highlight active row header
        if (cell.col == 0 && cell.row >= 3 && cell.row <= 18) {
            int r = cell.row - 3;
            if (r == cursor_y) c = GetColorFromString("LABEL_LITE");
            else c = GetColorFromString("LABEL_DIM");
        }
        
        renderer.drawString(textToDraw, cell.col, cell.row, c);
    }

    // 3. Draw 16x1 Interactive Grid

    for (int y = 0; y < 16; ++y) {
        const UI_GridCell& cell = GrooveGrid[y][0];
        std::string val = m8::ui::HexU8(grooveData.steps[y], "0");
        
        bool isSelected = (cursor_y == y);
        SDL_Color color = isSelected ? GetColorFromString(cell.selected_color) 
                                     : GetColorFromString(grooveData.steps[y] == 0 ? "LABEL_DIM" : "VALUE");

        renderer.drawString(val, cell.col, cell.row, color);
        
        if (isSelected) {
            // Draw cursor bracket around the selected cell
            renderer.drawBracket(cell.col, cell.row, val.length(), GetColorFromString("LABEL_LITE"));
        }
    }
}

void HandleGrooveInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                        engine::Groove& groove, int currentGrooveIndex, int& cursor_y,
                        CommandSink& commandSink) {
    auto pushStep = [&]() {
        m8::engine::EngineCommand cmd;
        cmd.type = m8::engine::CommandType::SET_GROOVE_STEP;
        cmd.targetId = currentGrooveIndex;
        cmd.row = cursor_y;
        cmd.value = groove.steps[cursor_y];
        commandSink.send(cmd);
    };

    if (event.key.key == SDLK_DOWN) {
        if (editHeld) {
            groove.steps[cursor_y] = AdjustU8(groove.steps[cursor_y], -1, 1, 255, 0);
            pushStep();
            arrowPressedDuringEdit = true;
        } else {
            cursor_y = (cursor_y + 1) % 16;
        }
    } else if (event.key.key == SDLK_UP) {
        if (editHeld) {
            groove.steps[cursor_y] = AdjustU8(groove.steps[cursor_y], 1, 1, 255, 0);
            pushStep();
            arrowPressedDuringEdit = true;
        } else {
            cursor_y = (cursor_y - 1 + 16) % 16;
        }
    } else if (event.key.key == SDLK_RIGHT) {
        if (editHeld) {
            groove.steps[cursor_y] = AdjustU8(groove.steps[cursor_y], 16, 1, 255, 0);
            pushStep();
            arrowPressedDuringEdit = true;
        }
    } else if (event.key.key == SDLK_LEFT) {
        if (editHeld) {
            groove.steps[cursor_y] = AdjustU8(groove.steps[cursor_y], -16, 1, 255, 0);
            pushStep();
            arrowPressedDuringEdit = true;
        }
    }
}

void HandleGrooveEditRelease(engine::Groove& groove, int currentGrooveIndex, int cursor_y,
                              CommandSink& commandSink) {
    if (groove.steps[cursor_y] == 0) {
        groove.steps[cursor_y] = 6;
    } else {
        groove.steps[cursor_y] = 0;
    }
    m8::engine::EngineCommand cmd;
    cmd.type = m8::engine::CommandType::SET_GROOVE_STEP;
    cmd.targetId = currentGrooveIndex;
    cmd.row = cursor_y;
    cmd.value = groove.steps[cursor_y];
    commandSink.send(cmd);
}

} // namespace groove
} // namespace ui
} // namespace m8
