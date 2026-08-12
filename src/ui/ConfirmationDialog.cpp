#include "ConfirmationDialog.h"
#include <algorithm>
#include <sstream>

namespace m8::ui {

ConfirmationDialog::ConfirmationDialog() : m_prompt("LOSE CHANGES TO CURRENT SONG?"), m_selection(0), m_actionTag(0) {}

void ConfirmationDialog::init(const std::string& prompt, int defaultSelection, int actionTag) {
    m_prompt = prompt;
    m_selection = std::clamp(defaultSelection, 0, 1);
    m_actionTag = actionTag;
}

void ConfirmationDialog::render(Renderer& renderer, SDL_Color promptColor, SDL_Color textColor, SDL_Color bracketColor) {
    // Fill / ensure background is black
    renderer.clear({0, 0, 0, 255});

    // Split prompt by newline
    std::vector<std::string> lines;
    std::stringstream ss(m_prompt);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }
    if (lines.empty()) lines.push_back(m_prompt);

    if (lines.size() == 1) {
        int promptCol = 6;
        if (lines[0].length() < 40) {
            promptCol = std::max(0, static_cast<int>((40 - lines[0].length()) / 2));
        }
        int promptRow = 9;
        renderer.drawString(lines[0], promptCol, promptRow, promptColor);

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
    } else {
        int startRow = 6;
        int startCol = 3;
        for (size_t i = 0; i < lines.size(); ++i) {
            renderer.drawString(lines[i], startCol, static_cast<int>(startRow + i), promptColor);
        }

        int optRow = static_cast<int>(startRow + lines.size() + 1);
        int okCol = startCol;
        int cancelCol = startCol + 5;

        renderer.drawString("OK", okCol, optRow, textColor);
        renderer.drawString("CANCEL", cancelCol, optRow, textColor);

        if (m_selection == 0) {
            renderer.drawBracket(okCol, optRow, 2, bracketColor);
        } else {
            renderer.drawBracket(cancelCol, optRow, 6, bracketColor);
        }
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
