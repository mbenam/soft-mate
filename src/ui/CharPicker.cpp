#include "CharPicker.h"
#include <algorithm>
#include <vector>
#include <cstdlib>

namespace m8::ui {

static const char kCharGridABC[5][10] = {
    {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'},
    {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J'},
    {'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T'},
    {'U', 'V', 'W', 'X', 'Y', 'Z', ' ', ' ', '-', '+'},
    {'!', '@', '#', '$', '%', '^', '&', '=', '(', ')'}
};

static const char kCharGridQWERTY[5][10] = {
    {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'},
    {'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P'},
    {'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ' '},
    {'Z', 'X', 'C', 'V', 'B', 'N', 'M', ' ', '-', '+'},
    {'!', '@', '#', '$', '%', '^', '&', '=', '(', ')'}
};

CharPicker::CharPicker() : m_gridX(0), m_gridY(0), m_layout(LayoutMode::ABC) {}

void CharPicker::init(char currentChar) {
    m_gridX = 0;
    m_gridY = 0;
    const auto& grid = (m_layout == LayoutMode::QWERTY) ? kCharGridQWERTY : kCharGridABC;
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 10; ++x) {
            if (grid[y][x] == currentChar && currentChar != ' ') {
                m_gridX = x;
                m_gridY = y;
                return;
            }
        }
    }
}

void CharPicker::toggleLayout() {
    m_layout = (m_layout == LayoutMode::ABC) ? LayoutMode::QWERTY : LayoutMode::ABC;
}

void CharPicker::render(Renderer& renderer, SDL_Color textColor, SDL_Color bracketColor, SDL_Color accentColor) {
    renderer.clear({0, 0, 0, 255});

    const auto& grid = (m_layout == LayoutMode::QWERTY) ? kCharGridQWERTY : kCharGridABC;

    // Draw 5x10 matrix (stride = 2 columns, starting at col 4)
    for (int y = 0; y < 5; ++y) {
        int r = 4 + y * 2;
        for (int x = 0; x < 10; ++x) {
            int c = 4 + x * 2;
            char ch = grid[y][x];
            if (ch != ' ') {
                std::string s(1, ch);
                renderer.drawString(s, c, r, textColor);
            }
        }
    }

    // Draw bottom row options
    int optRow = 16;
    SDL_Color randColor = (m_gridY == 5 && m_gridX < 5) ? bracketColor : textColor;
    SDL_Color modeColor = (m_gridY == 5 && m_gridX >= 5) ? bracketColor : accentColor;
    
    renderer.drawString("RANDOM", 4, optRow, randColor);
    
    if (m_layout == LayoutMode::ABC) {
        renderer.drawString("ABC>", 19, optRow, modeColor);
    } else {
        renderer.drawString("QWERTY>", 16, optRow, modeColor);
    }

    // Draw active selection bracket
    if (m_gridY < 5) {
        int c = 4 + m_gridX * 2;
        int r = 4 + m_gridY * 2;
        renderer.drawBracket(c, r, 1, bracketColor);
    } else {
        if (m_gridX < 5) {
            renderer.drawBracket(4, optRow, 6, bracketColor);
        } else {
            if (m_layout == LayoutMode::ABC) {
                renderer.drawBracket(19, optRow, 4, bracketColor);
            } else {
                renderer.drawBracket(16, optRow, 7, bracketColor);
            }
        }
    }
}

void CharPicker::handleInput(const SDL_Event& event) {
    if (event.type == SDL_EVENT_KEY_DOWN) {
        SDL_Keycode key = event.key.key;
        if (key == SDLK_LEFT) {
            if (m_gridY == 5) {
                m_gridX = 0; // Move from ABC/QWERTY left to RANDOM
            } else {
                m_gridX = (m_gridX - 1 + 10) % 10;
            }
        } else if (key == SDLK_RIGHT) {
            if (m_gridY == 5) {
                if (m_gridX < 5) {
                    m_gridX = 8; // Move from RANDOM right to ABC/QWERTY
                } else {
                    toggleLayout(); // Pressing RIGHT on ABC/QWERTY toggles the layout
                }
            } else {
                m_gridX = (m_gridX + 1) % 10;
            }
        } else if (key == SDLK_UP) {
            m_gridY = (m_gridY - 1 + 6) % 6;
        } else if (key == SDLK_DOWN) {
            m_gridY = (m_gridY + 1) % 6;
        }
    }
}

char CharPicker::getSelectedChar() const {
    if (m_gridY < 5) {
        const auto& grid = (m_layout == LayoutMode::QWERTY) ? kCharGridQWERTY : kCharGridABC;
        return grid[m_gridY][m_gridX];
    }
    return ' ';
}

bool CharPicker::isRandomSelected() const {
    return (m_gridY == 5 && m_gridX < 5);
}

bool CharPicker::isModeToggleSelected() const {
    return (m_gridY == 5 && m_gridX >= 5);
}

std::string CharPicker::generateRandomName() {
    static const std::vector<std::string> prefixes = {
        "NEO", "ACID", "CYBER", "STAR", "HYPER", "RETRO", "SOLAR", "LUNAR",
        "MICRO", "MEGA", "CHIP", "DARK", "VAPOR", "PULSE", "DRIFT", "NOVA",
        "ECHO", "GHOST", "ZERO", "VOID", "SYNTH", "AURA", "ORBIT", "QUANT"
    };
    static const std::vector<std::string> suffixes = {
        "DRIVE", "WAVE", "PUNK", "GAZER", "CORE", "GLITCH", "BEAT", "RUN",
        "BURST", "ZONE", "SPACE", "DUST", "WALK", "RAYS", "STORM", "SCAPE",
        "MODE", "TRAIL", "SHOCK", "FLUX", "GRID", "CHORD", "STEP", "NIGHT"
    };
    static const std::vector<std::string> standalone = {
        "SUNRISE", "NIGHTDRIVE", "NEO TOKYO", "ACID WAVE", "CYBERPUNK",
        "CHIPJUNE", "STELLAR", "HYPERDRIVE", "RETROGRADE", "PULSE WIDTH",
        "GLITCHCORE", "MONOLITH", "VAPORTRAIL", "STARGAZER", "ORBITAL",
        "HORIZON", "DREAMSCAPE", "VELOCITY", "SUB ZERO", "SUPERNOVA"
    };

    if (std::rand() % 3 == 0) {
        int idx = std::rand() % standalone.size();
        return standalone[idx];
    } else {
        std::string p = prefixes[std::rand() % prefixes.size()];
        std::string s = suffixes[std::rand() % suffixes.size()];
        std::string combined = p + " " + s;
        if (combined.length() > 12) {
            combined = p + s;
        }
        if (combined.length() > 12) {
            combined = combined.substr(0, 12);
        }
        return combined;
    }
}

} // namespace m8::ui
