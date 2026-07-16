#include "InstPoolScreen.h"
#include "InstPoolScreenLayout.h"
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace m8 {
namespace ui {
namespace inst_pool {

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

void RenderInstPoolScreen(Renderer& renderer, 
                          const engine::EngineState& engState,
                          int cursor_x, int cursor_y) {
    
    static UI_GridCell Grid[16][6];
    static bool initialized = false;
    static std::vector<UI_GridCell> staticText;
    static std::vector<UI_GridCell> dynamicText;
    
    static int view_offset = 0;
    if (cursor_y < view_offset) view_offset = cursor_y;
    if (cursor_y >= view_offset + 16) view_offset = cursor_y - 15;
    
    if (!initialized) {
        InitInstPoolGrid(Grid);
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
        
        if (cell.text.substr(0, 2) == "T>") {
            textToDraw = "T>" + std::to_string(engState.bpm);
        }
        
        // Highlight active column header
        if (cell.row == 2) {
            if (cell.text == "INST." && cursor_x == 0) c = GetColorFromString("LABEL_LITE");
            else if (cell.text == "DRY" && cursor_x == 1) c = GetColorFromString("LABEL_LITE");
            else if (cell.text == "CH" && cursor_x == 2) c = GetColorFromString("LABEL_LITE");
            else if (cell.text == "DE" && cursor_x == 3) c = GetColorFromString("LABEL_LITE");
            else if (cell.text == "RV" && cursor_x == 4) c = GetColorFromString("LABEL_LITE");
            else if (cell.text == "EQ" && cursor_x == 5) c = GetColorFromString("LABEL_LITE");
            else c = GetColorFromString("LABEL_DIM");
        }
        
        // Only draw the dummy row header if it isn't part of the loop below. 
        // Instead, we'll draw the dynamic row headers in the grid loop.
        if (cell.row != 4 || cell.col != 0) {
            renderer.drawString(textToDraw, cell.col, cell.row, c);
        }
    }

    // 3. Draw 16x6 Interactive Grid & Row Headers
    for (int y = 0; y < 16; ++y) {
        int actual_y = y + view_offset;
        if (actual_y >= 128) continue; // M8 max instruments

        // Draw Row Header
        SDL_Color rowColor = (cursor_y == actual_y) ? GetColorFromString("LABEL_LITE") : GetColorFromString("LABEL_DIM");
        renderer.drawString(ToHex(actual_y), 0, y + 4, rowColor);

        const engine::Instrument& inst = engState.instruments[actual_y];
        bool isMac = (inst.type == engine::InstType::INST_MACROSYN);
        bool isEmpty = (inst.type == engine::InstType::INST_NONE || inst.name == "------------");

        for (int x = 0; x < 6; ++x) {
            const UI_GridCell& cell = Grid[y][x];
            
            std::string val;
            if (x == 0) val = isEmpty ? "------------" : inst.name;
            else if (isEmpty) val = "--";
            else {
                if (x == 1) val = ToHex(isMac ? inst.macrosyn.dry : inst.sampler.dry);
                else if (x == 2) val = ToHex(isMac ? inst.macrosyn.cho : inst.sampler.cho);
                else if (x == 3) val = ToHex(isMac ? inst.macrosyn.del : inst.sampler.del);
                else if (x == 4) val = ToHex(isMac ? inst.macrosyn.rev : inst.sampler.rev);
                else if (x == 5) {
                    int eq = isMac ? inst.macrosyn.eq : inst.sampler.eq;
                    val = (eq == 0) ? "--" : ToHex(eq);
                }
            }
            
            bool isSelected = (cursor_x == x && cursor_y == actual_y);
            
            // In M8, 00 and -- are rendered dim to reduce visual noise.
            bool isDim = (val == "00" || val == "--" || val == "------------");
            SDL_Color color = isSelected ? GetColorFromString(cell.selected_color) 
                                         : GetColorFromString(isDim ? "LABEL_DIM" : "VALUE");

            renderer.drawString(val, cell.col, cell.row, color);
            
            if (isSelected) {
                renderer.drawBracket(cell.col, cell.row, val.length(), GetColorFromString("LABEL_LITE"));
            }
        }
    }
}

void HandleInstPoolInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                          engine::EngineState& uiEngineState, int& cursor_x, int& cursor_y,
                          CommandSink& commandSink) {
    if (event.key.key == SDLK_DOWN) {
        if (!editHeld) cursor_y = (cursor_y + 1) % 128;
    } else if (event.key.key == SDLK_UP) {
        if (!editHeld) cursor_y = (cursor_y - 1 + 128) % 128;
    } else if (event.key.key == SDLK_RIGHT) {
        if (!editHeld) cursor_x = (cursor_x + 1) % 6;
    } else if (event.key.key == SDLK_LEFT) {
        if (!editHeld) cursor_x = (cursor_x - 1 + 6) % 6;
    }

    if (editHeld && (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN)) {
        arrowPressedDuringEdit = true;
        int step = (event.key.key == SDLK_UP) ? 1 : -1;
        const auto& inst = uiEngineState.instruments[cursor_y];
        bool isMac = (inst.type == m8::engine::InstType::INST_MACROSYN);

        if (cursor_x == 0) {
            int t = static_cast<int>(inst.type);
            PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_TYPE, (t + step + 3) % 3, cursor_y);
        } else if (inst.type != m8::engine::InstType::INST_NONE) {
            if (cursor_x == 1) { int v = isMac ? inst.macrosyn.dry : inst.sampler.dry; PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_DRY, std::clamp<int>(v + step, 0, 255), cursor_y); }
            else if (cursor_x == 2) { int v = isMac ? inst.macrosyn.cho : inst.sampler.cho; PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_CHO, std::clamp<int>(v + step, 0, 255), cursor_y); }
            else if (cursor_x == 3) { int v = isMac ? inst.macrosyn.del : inst.sampler.del; PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_DEL, std::clamp<int>(v + step, 0, 255), cursor_y); }
            else if (cursor_x == 4) { int v = isMac ? inst.macrosyn.rev : inst.sampler.rev; PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_REV, std::clamp<int>(v + step, 0, 255), cursor_y); }
            else if (cursor_x == 5) { int v = isMac ? inst.macrosyn.eq : inst.sampler.eq; PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_EQ, std::clamp<int>(v + step, 0, 255), cursor_y); }
        }
    }
}

} // namespace inst_pool
} // namespace ui
} // namespace m8
