#include "ConfirmationDialog.h"
#include <algorithm>

namespace m8::ui {

ConfirmationDialog::ConfirmationDialog() : m_prompt("LOSE CHANGES TO CURRENT SONG?"), m_selection(0) {}

void ConfirmationDialog::init(const std::string& prompt, int defaultSelection) {
    m_prompt = prompt;
    m_selection = std::clamp(defaultSelection, 0, 1);
}

void ConfirmationDialog::render(Renderer& renderer, SDL_Color promptColor, SDL_Color textColor, SDL_Color bracketColor) {
    // Fill / ensure background is black
    renderer.clear({0, 0, 0, 255});

    // Center prompt horizontally if possible, or start at col 6
    int promptCol = 6;
    if (m_prompt.length() < 40) {
        promptCol = std::max(0, static_cast<int>((40 - m_prompt.length()) / 2));
    }
    int promptRow = 9;
    renderer.drawString(m_prompt, promptCol, promptRow, promptColor);

    // Option row
    int optRow = 11;
    int okCol = promptCol;
    int cancelCol = promptCol + 5;

    renderer.drawString("OK", okCol, optRow, textColor);
    renderer.drawString("CANCEL", cancelCol, optRow, textColor);

    if (m_selection == 0) {
        renderer.drawBracket(okCol, optRow, 2, bracketColor);
    } else {
        renderer.drawBracket(cancelCol, optRow, 6, bracketColor);
    }
}

ConfirmationDialog::Result ConfirmationDialog::handleInput(const SDL_Event& event, bool editHeld) {
    (void)editHeld;
    if (event.type == SDL_EVENT_KEY_DOWN) {
        SDL_Keycode key = event.key.key;
        if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_UP || key == SDLK_DOWN) {
            m_selection = (m_selection == 0) ? 1 : 0;
            return Result::NONE;
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_X || key == SDLK_SPACE) {
            return (m_selection == 0) ? Result::CONFIRMED : Result::CANCELLED;
        } else if (key == SDLK_ESCAPE || key == SDLK_Z) {
            return Result::CANCELLED;
        }
    }
    return Result::NONE;
}

} // namespace m8::ui
