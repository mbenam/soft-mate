#include "PhraseScreen.h"
#include "PhraseScreenLayout.h"
#include <sstream>
#include "ui/HexFmt.h"
#include "ui/UiEditHelpers.h"
#include <iomanip>

namespace m8 {
namespace ui {
namespace phrase {

static SDL_Color GetColorFromString(const std::string& colorName) {
    if (colorName == "TITLE") return {255, 60, 60, 255}; // colorRed
    if (colorName == "LABEL_DIM") return {100, 100, 100, 255}; // colorGrey
    if (colorName == "LABEL_LITE") return {0, 255, 255, 255}; // colorCyan
    if (colorName == "VALUE") return {255, 255, 255, 255}; // colorWhite
    return {255, 255, 255, 255};
}

static bool initialized = false;
static UI_GridCell PhraseGrid[16][9];
static std::vector<UI_GridCell> staticText;
static std::vector<UI_GridCell> dynamicText;

void RenderPhraseScreen(Renderer& renderer, 
                        const engine::Sequencer& uiSequencer,
                        const engine::EngineState& engState,
                        const engine::Playhead* playheads,
                        int currentPhrase,
                        int cursor_x, int cursor_y,
                        int songCol,
                        bool isPlaying) {
    
    if (!initialized) {
        InitPhraseGrid(PhraseGrid);
        staticText = GetStaticText();
        dynamicText = GetDynamicTextDefaults();
        initialized = true;
    }

    // Draw Static Text
    for (const auto& cell : staticText) {
        renderer.drawString(cell.text, cell.col, cell.row, GetColorFromString(cell.normal_color));
    }

    // Draw Dynamic Text
    for (const auto& cell : dynamicText) {
        std::string textToDraw = cell.text;
        SDL_Color c = GetColorFromString(cell.normal_color);
        
        if (cell.role == "dynamic_text") {
            // SCREEN_INDEX
            if (cell.text == "12" || cell.text == "00") { 
                std::stringstream ss;
                ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << currentPhrase;
                textToDraw = ss.str();
            }
            // Row headers
            if (cell.row >= 3 && cell.row <= 18 && cell.col == 0) {
                int r = cell.row - 3;
                std::stringstream ss;
                ss << std::hex << std::uppercase << r;
                textToDraw = ss.str();
                if (r == cursor_y) c = {0, 255, 255, 255}; // cyan
            }
            
            // Tempo
            if (cell.text.substr(0, 2) == "T>") {
                textToDraw = "T>" + std::to_string(engState.bpm);
            }
            
            // Col headers
            if (cell.row == 2) {
                if (cell.col == 2 && cursor_x == 0) c = {0, 255, 255, 255};
                else if (cell.col == 6 && cursor_x == 1) c = {0, 255, 255, 255};
                else if (cell.col == 9 && cursor_x == 2) c = {0, 255, 255, 255};
                else if (cell.col == 12 && (cursor_x == 3 || cursor_x == 4)) c = {0, 255, 255, 255};
                else if (cell.col == 18 && (cursor_x == 5 || cursor_x == 6)) c = {0, 255, 255, 255};
                else if (cell.col == 24 && (cursor_x == 7 || cursor_x == 8)) c = {0, 255, 255, 255};
            }
        }
        
        renderer.drawString(textToDraw, cell.col, cell.row, c);
    }
    
    // Draw Interactive Grid
    for (int y = 0; y < 16; ++y) {
        const bool phraseMode = playheads[songCol].is(engine::PlayMode::PHRASE);
        bool isActiveRow = (isPlaying && phraseMode && y == playheads[songCol].phraseRow);
        
        for (int x = 0; x < 9; ++x) {
            const UI_GridCell& cell = PhraseGrid[y][x];
            const engine::Step& step = uiSequencer.phrases[currentPhrase][y];
            
            std::string val;
            std::string emptyVal;
            
            if (x == 0) { val = m8::ui::NoteName(step.note); emptyVal = "---"; }
            else if (x == 1) { val = m8::ui::HexU8(step.vol, "--"); emptyVal = "--"; }
            else if (x == 2) { val = m8::ui::HexU8(step.instr, "--"); emptyVal = "--"; }
            else if (x == 3) { val = m8::ui::FxName(step.fx[0].cmd); emptyVal = "---"; }
            else if (x == 4) { val = m8::ui::HexU8(step.fx[0].val, "00"); emptyVal = "00"; }
            else if (x == 5) { val = m8::ui::FxName(step.fx[1].cmd); emptyVal = "---"; }
            else if (x == 6) { val = m8::ui::HexU8(step.fx[1].val, "00"); emptyVal = "00"; }
            else if (x == 7) { val = m8::ui::FxName(step.fx[2].cmd); emptyVal = "---"; }
            else if (x == 8) { val = m8::ui::HexU8(step.fx[2].val, "00"); emptyVal = "00"; }
            
            if (x == 0 && isActiveRow) {
                int px = (cell.col - 1) * 8 + 2;
                int py = cell.row * 8 + 1;
                SDL_Color cGreen = {0, 255, 100, 255};
                for (int ty = 0; ty < 7; ++ty) {
                    int w = (ty < 4) ? (ty + 1) : (7 - ty);
                    renderer.drawLinePixel(px, py + ty, px + w - 1, py + ty, cGreen);
                }
            }
            
            bool isSelected = (cursor_x == x && cursor_y == y);
            SDL_Color color = isSelected ? GetColorFromString(cell.selected_color) 
                                         : GetColorFromString(val == "--" || val == "---" || val == "---00" || val == emptyVal ? "LABEL_DIM" : "VALUE");
                                         
            renderer.drawString(val, cell.col, cell.row, color);
            
            if (isSelected) {
                int cw = val.length();
                renderer.drawBracket(cell.col, cell.row, cw, {0, 255, 255, 255});
            }
        }
    }
}

void HandlePhraseInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                        engine::Sequencer& uiSequencer, int currentPhrase,
                        int& cursor_x, int& cursor_y, CommandSink& commandSink) {
    auto& phrases = uiSequencer.phrases;
    auto pushStep = [&]() {
        m8::engine::EngineCommand cmd;
        cmd.type = m8::engine::CommandType::SET_STEP;
        cmd.targetId = currentPhrase;
        cmd.row = cursor_y;
        cmd.u.step = phrases[currentPhrase][cursor_y];
        commandSink.send(cmd);
    };

    if (event.key.key == SDLK_DOWN) {
        if (editHeld) { ModifyValue(phrases[currentPhrase][cursor_y], cursor_x, -1, true); arrowPressedDuringEdit = true; pushStep(); }
        else { cursor_y = (cursor_y + 1) % 16; }
    } else if (event.key.key == SDLK_UP) {
        if (editHeld) { ModifyValue(phrases[currentPhrase][cursor_y], cursor_x, 1, true); arrowPressedDuringEdit = true; pushStep(); }
        else { cursor_y = (cursor_y - 1 + 16) % 16; }
    } else if (event.key.key == SDLK_RIGHT) {
        if (editHeld) { ModifyValue(phrases[currentPhrase][cursor_y], cursor_x, 1, false); arrowPressedDuringEdit = true; pushStep(); }
        else { cursor_x = (cursor_x + 1) % 9; }
    } else if (event.key.key == SDLK_LEFT) {
        if (editHeld) { ModifyValue(phrases[currentPhrase][cursor_y], cursor_x, -1, false); arrowPressedDuringEdit = true; pushStep(); }
        else { cursor_x = (cursor_x - 1 + 9) % 9; }
    }
}

void HandlePhraseEditRelease(engine::Sequencer& uiSequencer, int currentPhrase,
                              int cursor_x, int cursor_y, CommandSink& commandSink) {
    auto& phrases = uiSequencer.phrases;
    InsertDefault(phrases[currentPhrase][cursor_y], cursor_x);

    m8::engine::EngineCommand cmd;
    cmd.type = m8::engine::CommandType::SET_STEP;
    cmd.targetId = currentPhrase;
    cmd.row = cursor_y;
    cmd.u.step = phrases[currentPhrase][cursor_y];
    commandSink.send(cmd);
}

} // namespace phrase
} // namespace ui
} // namespace m8
