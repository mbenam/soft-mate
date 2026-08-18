#include "GrooveScreen.h"
#include "GrooveScreenLayout.h"
#include "ui/HexFmt.h"
#include "ui/UiEditHelpers.h"
#include "ui/Theme.h"
#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace m8 {
namespace ui {
namespace groove {

static SDL_Color GetColorFromString(const std::string& colorName) {
    return GetThemeColor(colorName);
}

static int ComputeCurrentPPQ(const engine::Groove& g) {
    uint8_t s0 = (g.steps[0] == 0xFF) ? 6 : g.steps[0];
    uint8_t s1 = (g.steps[1] == 0xFF) ? s0 : g.steps[1];
    int rawPpq = (s0 + s1) * 2;
    if (rawPpq >= 144) return 192;
    if (rawPpq >= 72) return 96;
    if (rawPpq >= 36) return 48;
    return 24;
}

static std::string FormatSwing(uint8_t stepA, uint8_t stepB) {
    if (stepA == 0xFF || stepB == 0xFF) return "";
    int sum = stepA + stepB;
    if (sum == 0) return " 50.0%";
    float pct = ((float)stepA / (float)sum) * 100.0f;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%5.1f%%", pct);
    return std::string(buf);
}

static std::string FormatLoopSummary(const engine::Groove& g) {
    int activeSteps = 0;
    int loopTicks = 0;
    for (int i = 0; i < 16; ++i) {
        if (g.steps[i] == 0xFF) break;
        activeSteps++;
        loopTicks += g.steps[i];
    }
    if (activeSteps == 0) {
        activeSteps = 1;
        loopTicks = 6;
    }
    int phraseTicks = (activeSteps > 0) ? (loopTicks * 16) / activeSteps : 96;
    return std::to_string(phraseTicks) + " TICKS/PHRASE (" +
           std::to_string(loopTicks) + " TICKS/" + std::to_string(activeSteps) + " STEPS)";
}

void RenderGrooveScreen(Renderer& renderer, 
                        const engine::EngineState& engState,
                        const engine::Groove& grooveData,
                        int currentGrooveIndex,
                        int cursor_x,
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

    // 1. Draw Static Text (Headers)
    for (const auto& cell : staticText) {
        SDL_Color c = GetColorFromString(cell.normal_color);
        if (cell.text == "TIC" && cursor_x == 0) c = GetColorFromString("LABEL_LITE");
        else if (cell.text == "PPQ" && cursor_x == 1) c = GetColorFromString("LABEL_LITE");
        renderer.drawString(cell.text, cell.col, cell.row, c);
    }

    // 2. Draw Dynamic Headers (Groove index & Row numbers)
    for (const auto& cell : dynamicText) {
        std::string textToDraw = cell.text;
        SDL_Color c = GetColorFromString(cell.normal_color);
        
        // Dynamic Title
        if (cell.text == "00" && cell.col == 7) {
            std::stringstream ss;
            ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << currentGrooveIndex;
            textToDraw = ss.str();
        }
        
        // Highlight active row header
        if (cell.col == 1 && cell.row >= 3 && cell.row <= 18) {
            int r = cell.row - 3;
            if (r == cursor_y) c = GetColorFromString("LABEL_LITE");
            else c = GetColorFromString("LABEL_DIM");
        }
        
        renderer.drawString(textToDraw, cell.col, cell.row, c);
    }

    // 3. Draw Step Rows (TIC, PPQ, SWING)
    int ppqVal = ComputeCurrentPPQ(grooveData);
    std::string ppqStr = (ppqVal < 100 ? " " : "") + std::to_string(ppqVal);

    for (int y = 0; y < 16; ++y) {
        int rowY = y + 3;
        
        // TIC value
        std::string val = (grooveData.steps[y] == 0xFF) ? "--" : m8::ui::HexU8(grooveData.steps[y], "00");
        bool isTicSelected = (cursor_x == 0 && cursor_y == y);
        SDL_Color color = isTicSelected ? GetColorFromString("LABEL_LITE")
                                        : GetColorFromString(grooveData.steps[y] == 0xFF ? "LABEL_DIM" : "VALUE");

        renderer.drawString(val, 3, rowY, color);
        if (isTicSelected) {
            renderer.drawBracket(3, rowY, 2, GetColorFromString("LABEL_LITE"));
        }

        // PPQ value (only displayed on step 0)
        if (y == 0) {
            bool isPpqSelected = (cursor_x == 1 && cursor_y == 0);
            SDL_Color ppqColor = isPpqSelected ? GetColorFromString("LABEL_LITE")
                                               : GetColorFromString("VALUE");
            renderer.drawString(ppqStr, 8, rowY, ppqColor);
            if (isPpqSelected) {
                renderer.drawBracket(8, rowY, 3, GetColorFromString("LABEL_LITE"));
            }
        }

        // SWING value (displayed on even rows: 0, 2, 4, 6, 8, 10, 12, 14)
        if (y % 2 == 0 && y + 1 < 16) {
            std::string swingStr = FormatSwing(grooveData.steps[y], grooveData.steps[y + 1]);
            if (!swingStr.empty()) {
                renderer.drawString(swingStr, 12, rowY, GetColorFromString("LABEL_DIM"));
            }
        }
    }

    // 4. Draw Bottom Loop Summary
    std::string summary = FormatLoopSummary(grooveData);
    renderer.drawString(summary, 1, 22, GetColorFromString("LABEL_DIM"));
}

void HandleGrooveInput(const SDL_Event& event, bool editHeld, bool optHeld, bool shiftHeld,
                        bool& arrowPressedDuringEdit,
                        engine::Sequencer& sequencer, int& currentGrooveIndex,
                        int& cursor_x, int& cursor_y,
                        uint8_t& lastEditedValue,
                        CommandSink& commandSink) {
    (void)shiftHeld;
    auto pushStep = [&](int r) {
        m8::engine::EngineCommand cmd;
        cmd.type = m8::engine::CommandType::SET_GROOVE_STEP;
        cmd.targetId = currentGrooveIndex;
        cmd.row = r;
        cmd.value = sequencer.grooves[currentGrooveIndex].steps[r];
        commandSink.send(cmd);
    };

    // Option Navigation (switch grooves)
    if (optHeld && !editHeld) {
        if (event.key.key == SDLK_LEFT) {
            currentGrooveIndex = (currentGrooveIndex - 1 + 32) % 32;
            return;
        } else if (event.key.key == SDLK_RIGHT) {
            currentGrooveIndex = (currentGrooveIndex + 1) % 32;
            return;
        } else if (event.key.key == SDLK_UP) {
            currentGrooveIndex = (currentGrooveIndex - 16 + 32) % 32;
            return;
        } else if (event.key.key == SDLK_DOWN) {
            currentGrooveIndex = (currentGrooveIndex + 16) % 32;
            return;
        }
    }

    // Delete step (EDIT+OPT or DELETE/BACKSPACE with EDIT)
    if (editHeld && (event.key.key == SDLK_DELETE || event.key.key == SDLK_BACKSPACE || (optHeld && (event.key.key == SDLK_Z || event.key.key == SDLK_LALT)))) {
        if (cursor_x == 0) {
            sequencer.grooves[currentGrooveIndex].steps[cursor_y] = 0xFF;
            pushStep(cursor_y);
            arrowPressedDuringEdit = true;
            return;
        }
    }

    // Normal Navigation
    if (!editHeld) {
        if (event.key.key == SDLK_DOWN) {
            if (cursor_x == 1) {
                cursor_x = 0;
                cursor_y = 1;
            } else {
                cursor_y = (cursor_y + 1) % 16;
            }
        } else if (event.key.key == SDLK_UP) {
            if (cursor_x == 1) {
                cursor_x = 0;
                cursor_y = 15;
            } else {
                cursor_y = (cursor_y - 1 + 16) % 16;
            }
        } else if (event.key.key == SDLK_RIGHT) {
            if (cursor_x == 0 && cursor_y == 0) {
                cursor_x = 1;
            }
        } else if (event.key.key == SDLK_LEFT) {
            if (cursor_x == 1) {
                cursor_x = 0;
            }
        }
        return;
    }

    // Edit Mode
    auto& groove = sequencer.grooves[currentGrooveIndex];

    if (cursor_x == 0) {
        // TIC column editing
        if (groove.steps[cursor_y] == 0xFF) {
            groove.steps[cursor_y] = (lastEditedValue != 0 && lastEditedValue != 0xFF) ? lastEditedValue : 6;
            pushStep(cursor_y);
        }

        if (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN) {
            // Dual-row swing pair adjustment
            int pair = cursor_y / 2;
            int r0 = pair * 2;
            int r1 = r0 + 1;

            if (cursor_y == r0) {
                if (groove.steps[r1] != 0xFF) {
                    if (event.key.key == SDLK_UP) {
                        if (groove.steps[r1] > 1 && groove.steps[r0] < 254) {
                            groove.steps[r0]++;
                            groove.steps[r1]--;
                            lastEditedValue = groove.steps[r0];
                            pushStep(r0);
                            pushStep(r1);
                        }
                    } else if (event.key.key == SDLK_DOWN) {
                        if (groove.steps[r0] > 1 && groove.steps[r1] < 254) {
                            groove.steps[r0]--;
                            groove.steps[r1]++;
                            lastEditedValue = groove.steps[r0];
                            pushStep(r0);
                            pushStep(r1);
                        }
                    }
                } else {
                    if (event.key.key == SDLK_UP) {
                        groove.steps[r0] = static_cast<uint8_t>(std::min(255, static_cast<int>(groove.steps[r0]) + 1));
                        lastEditedValue = groove.steps[r0];
                        pushStep(r0);
                    } else if (event.key.key == SDLK_DOWN) {
                        groove.steps[r0] = static_cast<uint8_t>(std::max(1, static_cast<int>(groove.steps[r0]) - 1));
                        lastEditedValue = groove.steps[r0];
                        pushStep(r0);
                    }
                }
            } else if (cursor_y == r1) {
                if (groove.steps[r0] != 0xFF) {
                    if (event.key.key == SDLK_UP) {
                        if (groove.steps[r0] > 1 && groove.steps[r1] < 254) {
                            groove.steps[r1]++;
                            groove.steps[r0]--;
                            lastEditedValue = groove.steps[r1];
                            pushStep(r0);
                            pushStep(r1);
                        }
                    } else if (event.key.key == SDLK_DOWN) {
                        if (groove.steps[r1] > 1 && groove.steps[r0] < 254) {
                            groove.steps[r1]--;
                            groove.steps[r0]++;
                            lastEditedValue = groove.steps[r1];
                            pushStep(r0);
                            pushStep(r1);
                        }
                    }
                } else {
                    if (event.key.key == SDLK_UP) {
                        groove.steps[r1] = static_cast<uint8_t>(std::min(255, static_cast<int>(groove.steps[r1]) + 1));
                        lastEditedValue = groove.steps[r1];
                        pushStep(r1);
                    } else if (event.key.key == SDLK_DOWN) {
                        groove.steps[r1] = static_cast<uint8_t>(std::max(1, static_cast<int>(groove.steps[r1]) - 1));
                        lastEditedValue = groove.steps[r1];
                        pushStep(r1);
                    }
                }
            }
            arrowPressedDuringEdit = true;
        } else if (event.key.key == SDLK_RIGHT) {
            groove.steps[cursor_y] = static_cast<uint8_t>(std::min(255, static_cast<int>(groove.steps[cursor_y]) + 1));
            lastEditedValue = groove.steps[cursor_y];
            pushStep(cursor_y);
            arrowPressedDuringEdit = true;
        } else if (event.key.key == SDLK_LEFT) {
            groove.steps[cursor_y] = static_cast<uint8_t>(std::max(1, static_cast<int>(groove.steps[cursor_y]) - 1));
            lastEditedValue = groove.steps[cursor_y];
            pushStep(cursor_y);
            arrowPressedDuringEdit = true;
        }
    } else if (cursor_x == 1) {
        // PPQ column editing
        int curPpq = ComputeCurrentPPQ(groove);
        if (event.key.key == SDLK_RIGHT || event.key.key == SDLK_UP) {
            if (curPpq < 192) {
                for (int i = 0; i < 16; ++i) {
                    if (groove.steps[i] != 0xFF) {
                        groove.steps[i] = static_cast<uint8_t>(std::min(255, groove.steps[i] * 2));
                        pushStep(i);
                    }
                }
                arrowPressedDuringEdit = true;
            }
        } else if (event.key.key == SDLK_LEFT || event.key.key == SDLK_DOWN) {
            if (curPpq > 24) {
                for (int i = 0; i < 16; ++i) {
                    if (groove.steps[i] != 0xFF) {
                        groove.steps[i] = static_cast<uint8_t>(std::max(1, groove.steps[i] / 2));
                        pushStep(i);
                    }
                }
                arrowPressedDuringEdit = true;
            }
        }
    }
}

void HandleGrooveEditRelease(engine::Groove& groove, int currentGrooveIndex,
                              int cursor_x, int cursor_y,
                              uint8_t lastEditedValue,
                              CommandSink& commandSink) {
    if (cursor_x != 0) return;
    if (groove.steps[cursor_y] == 0xFF || groove.steps[cursor_y] == 0) {
        groove.steps[cursor_y] = (lastEditedValue != 0 && lastEditedValue != 0xFF) ? lastEditedValue : 6;
    } else {
        groove.steps[cursor_y] = 0xFF;
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
