#pragma once

#include "Renderer.h"
#include <SDL3/SDL.h>
#include <string>

namespace m8::ui {

enum class LayoutMode {
    ABC,
    QWERTY
};

class CharPicker {
public:
    CharPicker();

    void init(char currentChar = '1');
    void render(Renderer& renderer, SDL_Color textColor, SDL_Color bracketColor, SDL_Color accentColor);
    void handleInput(const SDL_Event& event);

    char getSelectedChar() const;
    bool isRandomSelected() const;
    bool isModeToggleSelected() const;

    void toggleLayout();
    LayoutMode getLayoutMode() const { return m_layout; }
    void setLayoutMode(LayoutMode mode) { m_layout = mode; }

    int getGridX() const { return m_gridX; }
    int getGridY() const { return m_gridY; }

    static std::string generateRandomName();

private:
    int m_gridX = 0; // 0..9
    int m_gridY = 0; // 0..4 (grid rows), 5 (bottom options: RANDOM / MODE>)
    LayoutMode m_layout = LayoutMode::ABC;
};

} // namespace m8::ui
