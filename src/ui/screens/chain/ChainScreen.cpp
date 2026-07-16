#include "ChainScreen.h"
#include "ChainScreenLayout.h"
#include "ui/HexFmt.h"
#include "ui/UiEditHelpers.h"
#include <iomanip>
#include <sstream>

namespace m8 {
namespace ui {
namespace chain {

static SDL_Color GetColorFromString(const std::string& colorName) {
    if (colorName == "TITLE") return {255, 60, 60, 255}; // colorRed
    if (colorName == "LABEL_DIM") return {100, 100, 100, 255}; // colorGrey
    if (colorName == "LABEL_LITE") return {0, 255, 255, 255}; // colorCyan
    if (colorName == "VALUE") return {255, 255, 255, 255}; // colorWhite
    return {255, 255, 255, 255};
}

void RenderChainScreen(Renderer& renderer, 
                       const engine::Sequencer& uiSequencer,
                       const engine::EngineState& engState,
                       const engine::Playhead* playheads,
                       int currentChain,
                       int cursor_x, int cursor_y,
                       bool isPlaying) {
                       
    static UI_GridCell ChainGrid[16][2];
    static bool initialized = false;
    static std::vector<UI_GridCell> staticText;
    static std::vector<UI_GridCell> dynamicText;
    
    static int view_offset = 0;
    if (cursor_y < view_offset) view_offset = cursor_y;
    if (cursor_y >= view_offset + 16) view_offset = cursor_y - 15;
    
    if (!initialized) {
        InitChainGrid(ChainGrid);
        staticText = GetStaticText();
        dynamicText = GetDynamicTextDefaults();
        initialized = true;
    }

    // Draw Static Text
    for (const auto& cell : staticText) {
        renderer.drawString(cell.text, cell.col, cell.row, GetColorFromString(cell.normal_color));
    }

    // Draw Dynamic Text (Headers)
    for (const auto& cell : dynamicText) {
        std::string textToDraw = cell.text;
        SDL_Color c = GetColorFromString(cell.normal_color);
        
        // Dynamic Title update
        if (cell.role == "dynamic_text" && cell.row == 0 && cell.col == 6) {
            std::stringstream ss; 
            ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << currentChain << "*";
            textToDraw = ss.str();
        }
        
        // Dynamic row headers update
        if (cell.role == "dynamic_text" && cell.text.length() <= 2 && cell.col == 0) {
            // It's a row header (rows 3 to 18 in layout)
            int layout_row = cell.row;
            if (layout_row >= 3 && layout_row <= 18) {
                int r = layout_row - 3 + view_offset; // layout row 3 is index 0
                if (r >= 0 && r < 256) {
                    std::stringstream ss; 
                    ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << r;
                    textToDraw = ss.str();
                }
                
                // Highlight active row in cyan
                if (r == cursor_y) {
                    c = {0, 255, 255, 255}; // colorCyan
                }
            }
        }
        
        // Dynamic Tempo
        if (cell.text.substr(0, 2) == "T>") {
            textToDraw = "T>" + std::to_string(engState.bpm);
        }
        
        // Highlight active col in cyan (PH is layout row 2 col 3, TSP is row 2 col 6)
        if (cell.row == 2) {
            if (cell.col == 3 && cursor_x == 0) c = {0, 255, 255, 255};
            if (cell.col == 6 && cursor_x == 1) c = {0, 255, 255, 255};
        }
        
        renderer.drawString(textToDraw, cell.col, cell.row, c);
    }

    // Draw Interactive Grid
    for (int y = 0; y < 16; ++y) {
        int actual_y = y + view_offset;
        
        // Loop over cols: 0 = PHRASE, 1 = TRANSPOSE
        for (int x = 0; x < 2; ++x) {
            const UI_GridCell& cell = ChainGrid[y][x];
            
            std::string val = (x == 0) ? m8::ui::HexU8(uiSequencer.chains[currentChain][actual_y].phrase, "--") : m8::ui::HexS8(uiSequencer.chains[currentChain][actual_y].tsp, "00");
            std::string emptyVal = (x == 0) ? "--" : "00";
            
            const uint8_t col = playheads[0].activeCol;
            bool isActiveRow = (isPlaying && playheads[col].is(engine::PlayMode::CHAIN) && actual_y == playheads[col].chainRow);
            
            // Draw playback indicator on the left side of the row if it's the active playback row
            if (x == 0 && isActiveRow && val != emptyVal) {
                int px = (cell.col - 1) * 8 + 4; // similar to song screen logic, offset by 1 column and shift a bit
                int py = cell.row * 8 + 1;
                SDL_Color cGreen = {0, 255, 100, 255};
                // Draw tiny right pointing triangle
                renderer.drawLinePixel(px, py, px, py+4, cGreen);
                renderer.drawLinePixel(px+1, py+1, px+1, py+3, cGreen);
                renderer.drawLinePixel(px+2, py+2, px+2, py+2, cGreen);
            }
            
            bool isSelected = (cursor_x == x && cursor_y == actual_y);
            SDL_Color color = isSelected ? GetColorFromString(cell.selected_color) 
                                         : GetColorFromString(val == "--" || val == emptyVal ? "LABEL_DIM" : "VALUE");

            renderer.drawString(val, cell.col, cell.row, color);
            
            if (isSelected) {
                // Draw cursor bracket around the value
                // Length is 2 for both PH (e.g. "0A") and TSP (e.g. "12")
                renderer.drawBracket(cell.col, cell.row, 2, {0, 255, 255, 255}); // colorCyan
            }
        }
    }
}

void HandleChainInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                       engine::Sequencer& uiSequencer, int currentChain,
                       int& cursor_x, int& cursor_y, CommandSink& commandSink) {
    auto& chains = uiSequencer.chains;
    auto pushChainStep = [&]() {
        m8::engine::EngineCommand cmd;
        cmd.type = m8::engine::CommandType::SET_CHAIN_STEP;
        cmd.targetId = currentChain;
        cmd.row = cursor_y;
        cmd.u.chainStep = chains[currentChain][cursor_y];
        commandSink.send(cmd);
    };

    if (event.key.key == SDLK_DOWN) {
        if (editHeld) {
            if (cursor_x == 0) chains[currentChain][cursor_y].phrase = AdjustU8(chains[currentChain][cursor_y].phrase, -1, 0, 254, engine::PHRASE_EMPTY);
            else chains[currentChain][cursor_y].tsp = AdjustS8(chains[currentChain][cursor_y].tsp, -1, -128, 127, 0);
            pushChainStep();
            arrowPressedDuringEdit = true;
        } else { cursor_y = (cursor_y + 1) % 16; }
    } else if (event.key.key == SDLK_UP) {
        if (editHeld) {
            if (cursor_x == 0) chains[currentChain][cursor_y].phrase = AdjustU8(chains[currentChain][cursor_y].phrase, 1, 0, 254, engine::PHRASE_EMPTY);
            else chains[currentChain][cursor_y].tsp = AdjustS8(chains[currentChain][cursor_y].tsp, 1, -128, 127, 0);
            pushChainStep();
            arrowPressedDuringEdit = true;
        } else { cursor_y = (cursor_y - 1 + 16) % 16; }
    } else if (event.key.key == SDLK_RIGHT) {
        if (editHeld) {
            if (cursor_x == 0) chains[currentChain][cursor_y].phrase = AdjustU8(chains[currentChain][cursor_y].phrase, 16, 0, 254, engine::PHRASE_EMPTY);
            else chains[currentChain][cursor_y].tsp = AdjustS8(chains[currentChain][cursor_y].tsp, 12, -128, 127, 0);
            pushChainStep();
            arrowPressedDuringEdit = true;
        } else { cursor_x = (cursor_x + 1) % 2; }
    } else if (event.key.key == SDLK_LEFT) {
        if (editHeld) {
            if (cursor_x == 0) chains[currentChain][cursor_y].phrase = AdjustU8(chains[currentChain][cursor_y].phrase, -16, 0, 254, engine::PHRASE_EMPTY);
            else chains[currentChain][cursor_y].tsp = AdjustS8(chains[currentChain][cursor_y].tsp, -12, -128, 127, 0);
            pushChainStep();
            arrowPressedDuringEdit = true;
        } else { cursor_x = (cursor_x - 1 + 2) % 2; }
    }
}

void HandleChainEditRelease(engine::Sequencer& uiSequencer, int currentChain,
                             int cursor_x, int cursor_y, CommandSink& commandSink) {
    auto& chains = uiSequencer.chains;
    if (cursor_x == 0) {
        if (chains[currentChain][cursor_y].phrase == engine::PHRASE_EMPTY) chains[currentChain][cursor_y].phrase = 0;
        else chains[currentChain][cursor_y].phrase = engine::PHRASE_EMPTY;

        m8::engine::EngineCommand cmd;
        cmd.type = m8::engine::CommandType::SET_CHAIN_STEP;
        cmd.targetId = currentChain;
        cmd.row = cursor_y;
        cmd.u.chainStep = chains[currentChain][cursor_y];
        commandSink.send(cmd);
    }
}

} // namespace chain
} // namespace ui
} // namespace m8
