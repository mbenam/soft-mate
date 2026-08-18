#include "InstPoolScreen.h"
#include "InstPoolScreenLayout.h"
#include "ui/Theme.h"
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace m8 {
namespace ui {
namespace inst_pool {

static SDL_Color GetColorFromString(const std::string& colorName) {
    return GetThemeColor(colorName);
}

static std::string ToHex(int value) {
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (static_cast<unsigned int>(value) & 0xFF);
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

// Open the instrument under the pool cursor on the INSTRUMENT screen.
//
// This is a clone-side navigation affordance, not a hardware-verified
// behaviour: no capture of the real M8's pool-key handling backs it, and it is
// not claimed as parity. It exists because the pool is the only screen that
// lists all 128 instruments, and without it the sole way out was SHIFT+RIGHT
// (docs/ui_screen_spec.md: a screen is done when you can use it to write
// songs, not when it matches the device). If a capture later shows the device
// doing something else with this key, that finding wins.
//
// Empty slots jump too, deliberately -- opening an unused slot is how you
// start defining one.
static void OpenInstrumentUnderCursor(int cursor_y, ViewManager& viewManager,
                                      int& currentInstIndex) {
    if (cursor_y < 0 || cursor_y > 127) return;
    currentInstIndex = cursor_y;
    viewManager.setCoords(3, 0);   // INSTRUMENT (ViewManager::getViewAt)
}

void HandleInstPoolEditRelease(int cursor_x, int cursor_y,
                               const engine::EngineState& uiEngineState,
                               ViewManager& viewManager, int& currentInstIndex, int& eqBank) {
    if (cursor_y < 0 || cursor_y > 127) return;

    // The EQ column is a doorway to the EQ editor rather than to the
    // instrument -- it is the one column that names something other than this
    // instrument's own parameters (EQ_SPEC.md step 7).
    if (cursor_x == 5) {
        eqBank = uiEngineState.instruments[cursor_y].getEq();
        currentInstIndex = cursor_y;
        viewManager.pushModal(m8::ui::ViewType::EQ);
        return;
    }
    OpenInstrumentUnderCursor(cursor_y, viewManager, currentInstIndex);
}

void HandleInstPoolInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                          engine::EngineState& uiEngineState, int& cursor_x, int& cursor_y,
                          CommandSink& commandSink,
                          ViewManager& viewManager, int& currentInstIndex) {
    if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
        OpenInstrumentUnderCursor(cursor_y, viewManager, currentInstIndex);
        return;
    }

    if (event.key.key == SDLK_DOWN) {
        if (!editHeld) cursor_y = (cursor_y + 1) % 128;
    } else if (event.key.key == SDLK_UP) {
        if (!editHeld) cursor_y = (cursor_y - 1 + 128) % 128;
    } else if (event.key.key == SDLK_RIGHT) {
        if (!editHeld) cursor_x = (cursor_x + 1) % 6;
    } else if (event.key.key == SDLK_LEFT) {
        if (!editHeld) cursor_x = (cursor_x - 1 + 6) % 6;
    }

    if (editHeld && (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT)) {
        // The pool has no horizontal edit action, but the hold is still an edit
        // gesture rather than a tap -- flag it so the X release opens nothing.
        arrowPressedDuringEdit = true;
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
            else if (cursor_x == 5) { PushParam(commandSink, uiEngineState, m8::engine::ParamID::INST_EQ, std::clamp<int>(inst.getEq() + step, 0, uiEngineState.eqBankCount - 1), cursor_y); }
        }
    }
}

} // namespace inst_pool
} // namespace ui
} // namespace m8
