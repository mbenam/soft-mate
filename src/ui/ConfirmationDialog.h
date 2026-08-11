#pragma once
#include <string>
#include <SDL3/SDL.h>
#include "Renderer.h"

namespace m8::ui {

class ConfirmationDialog {
public:
    enum class Result {
        NONE,
        CONFIRMED,
        CANCELLED
    };

    ConfirmationDialog();

    void init(const std::string& prompt, int defaultSelection = 0);
    void render(Renderer& renderer, SDL_Color promptColor, SDL_Color textColor, SDL_Color bracketColor);
    Result handleInput(const SDL_Event& event, bool editHeld);

    int getSelection() const { return m_selection; }
    const std::string& getPrompt() const { return m_prompt; }

private:
    std::string m_prompt = "LOSE CHANGES TO CURRENT SONG?";
    int m_selection = 0; // 0 = OK, 1 = CANCEL
};

} // namespace m8::ui
