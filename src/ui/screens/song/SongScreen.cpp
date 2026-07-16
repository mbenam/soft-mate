#include "SongScreen.h"
#include "SongScreenLayout.h"
#include "ui/HexFmt.h"
#include "ui/UiEditHelpers.h"
#include <iomanip>
#include <sstream>

namespace m8 {
namespace ui {
namespace song {

static SDL_Color GetColorFromString(const std::string& colorName) {
    if (colorName == "TITLE") return {255, 60, 60, 255}; // colorRed
    if (colorName == "LABEL_DIM") return {100, 100, 100, 255}; // colorGrey
    if (colorName == "LABEL_LITE") return {0, 255, 255, 255}; // colorCyan
    if (colorName == "VALUE") return {255, 255, 255, 255}; // colorWhite
    return {255, 255, 255, 255};
}

void RenderSongScreen(Renderer& renderer, 
                      const engine::Sequencer& uiSequencer,
                      const engine::EngineState& engState,
                      const engine::Playhead* playheads,
                      int cursor_x, int cursor_y,
                      bool isPlaying) {
                      
    static UI_GridCell SongGrid[16][8];
    static bool initialized = false;
    static std::vector<UI_GridCell> staticText;
    static std::vector<UI_GridCell> dynamicText;
    
    static int view_offset = 0;
    if (cursor_y < view_offset) view_offset = cursor_y;
    if (cursor_y >= view_offset + 16) view_offset = cursor_y - 15;
    
    if (!initialized) {
        InitSongGrid(SongGrid);
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
        // Here we could update row/col values dynamically if scrolling was implemented.
        // For now, we draw the default values from the JSON.
        std::string textToDraw = cell.text;
        
        // Dynamic row headers update
        if (cell.role == "dynamic_text" && cell.text.length() == 2 && cell.col == 0) {
            // It's a row header
            int r = cell.row - 3 + view_offset; // row 3 is index 0
            if (r >= 0 && r < 256) {
                std::stringstream ss; 
                ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << r;
                textToDraw = ss.str();
            }
        }
        
        // Dynamic Tempo
        if (cell.text.substr(0, 2) == "T>") {
            textToDraw = "T>" + std::to_string(engState.bpm);
        }
        
        SDL_Color c = GetColorFromString(cell.normal_color);
        // Highlight active row in cyan
        if (cell.col == 0 && (cell.row - 3) == (cursor_y - view_offset)) {
            c = {0, 255, 255, 255}; // colorCyan
        }
        // Highlight active col in cyan
        if (cell.row == 2 && cell.col >= 4 && cell.col <= 25 && ((cell.col - 4) / 3) == cursor_x) {
            c = {0, 255, 255, 255}; // colorCyan
        }
        
        renderer.drawString(textToDraw, cell.col, cell.row, c);
    }

    // Draw Interactive Grid
    for (int y = 0; y < 16; ++y) {
        int actual_y = y + view_offset;
        for (int x = 0; x < 8; ++x) {
            const UI_GridCell& cell = SongGrid[y][x];
            
            std::string val = m8::ui::HexU8(uiSequencer.song[actual_y].tracks[x], "--");
            
            if (isPlaying && playheads[x].is(engine::PlayMode::SONG) && actual_y == playheads[x].songRow && val != "--") {
                int px = (cell.col - 1) * 8 + 4;
                int py = cell.row * 8 + 1;
                SDL_Color cGreen = {0, 255, 100, 255};
                renderer.drawLinePixel(px, py, px, py+4, cGreen);
                renderer.drawLinePixel(px+1, py+1, px+1, py+3, cGreen);
                renderer.drawLinePixel(px+2, py+2, px+2, py+2, cGreen);
            }
            
            bool isSelected = (cursor_x == x && cursor_y == actual_y);
            SDL_Color color = isSelected ? GetColorFromString(cell.selected_color) 
                                         : GetColorFromString(uiSequencer.song[actual_y].tracks[x] == 255 ? "LABEL_DIM" : "VALUE");

            renderer.drawString(val, cell.col, cell.row, color);
            
            if (isSelected) {
                // Draw cursor bracket around the value
                // Length is always 2 for Song screen
                renderer.drawBracket(cell.col, cell.row, 2, {0, 255, 255, 255}); // colorCyan
            }
        }
    }
}

void HandleSongInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                      engine::Sequencer& uiSequencer, int& cursor_x, int& cursor_y,
                      CommandSink& commandSink) {
    using namespace m8::ui;
    auto& song = uiSequencer.song;
    auto pushSongStep = [&]() {
        m8::engine::EngineCommand cmd;
        cmd.type = m8::engine::CommandType::SET_SONG_STEP;
        cmd.targetId = cursor_y;
        cmd.row = cursor_x;
        cmd.value = song[cursor_y].tracks[cursor_x];
        commandSink.send(cmd);
    };

    if (event.key.key == SDLK_DOWN) {
        if (editHeld) {
            song[cursor_y].tracks[cursor_x] = AdjustU8(song[cursor_y].tracks[cursor_x], -1, 0, 254, engine::CHAIN_EMPTY);
            arrowPressedDuringEdit = true;
            pushSongStep();
        } else if (cursor_y < 255) cursor_y++;
    } else if (event.key.key == SDLK_UP) {
        if (editHeld) {
            song[cursor_y].tracks[cursor_x] = AdjustU8(song[cursor_y].tracks[cursor_x], 1, 0, 254, engine::CHAIN_EMPTY);
            arrowPressedDuringEdit = true;
            pushSongStep();
        } else if (cursor_y > 0) cursor_y--;
    } else if (event.key.key == SDLK_RIGHT) {
        if (editHeld) {
            song[cursor_y].tracks[cursor_x] = AdjustU8(song[cursor_y].tracks[cursor_x], 16, 0, 254, engine::CHAIN_EMPTY);
            arrowPressedDuringEdit = true;
            pushSongStep();
        } else cursor_x = (cursor_x + 1) % 8;
    } else if (event.key.key == SDLK_LEFT) {
        if (editHeld) {
            song[cursor_y].tracks[cursor_x] = AdjustU8(song[cursor_y].tracks[cursor_x], -16, 0, 254, engine::CHAIN_EMPTY);
            arrowPressedDuringEdit = true;
            pushSongStep();
        } else cursor_x = (cursor_x - 1 + 8) % 8;
    }
}

void HandleSongEditRelease(engine::Sequencer& uiSequencer, int cursor_x, int cursor_y,
                            CommandSink& commandSink) {
    auto& song = uiSequencer.song;
    if (song[cursor_y].tracks[cursor_x] == engine::CHAIN_EMPTY) song[cursor_y].tracks[cursor_x] = 0;
    else song[cursor_y].tracks[cursor_x] = engine::CHAIN_EMPTY;

    m8::engine::EngineCommand cmd;
    cmd.type = m8::engine::CommandType::SET_SONG_STEP;
    cmd.targetId = cursor_y;
    cmd.row = cursor_x;
    cmd.value = song[cursor_y].tracks[cursor_x];
    commandSink.send(cmd);
}

} // namespace song
} // namespace ui
} // namespace m8
