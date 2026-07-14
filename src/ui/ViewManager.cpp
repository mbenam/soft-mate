#include "ViewManager.h"
#include <algorithm>

namespace m8 {
namespace ui {

ViewManager::ViewManager() : m_x(0), m_y(0) {}

ViewType ViewManager::getViewAt(int x, int y) const {
    if (x < 0 || x > 4) return ViewType::NONE;
    if (y == -1) return ViewType::MIXER;
    if (y == -2) return ViewType::EFFECTS;
    if (y < -2) return ViewType::NONE;

    if (y == 0) {
        if (x == 0) return ViewType::SONG;
        if (x == 1) return ViewType::CHAIN;
        if (x == 2) return ViewType::PHRASE;
        if (x == 3) return ViewType::INSTRUMENT;
        if (x == 4) return ViewType::TABLE;
    }
    if (y == 1) {
        if (x == 0 || x == 1) return ViewType::PROJECT;
        if (x == 2) return ViewType::GROOVE;
        if (x == 3 || x == 4) return ViewType::INST_MOD;
    }
    if (y == 2) {
        if (x == 2) return ViewType::SCALE;
        if (x == 3) return ViewType::INST_POOL;
    }
    return ViewType::NONE;
}

ViewType ViewManager::getCurrentView() const {
    if (m_modalView != ViewType::NONE) return m_modalView;
    return getViewAt(m_x, m_y);
}

void ViewManager::pushModal(ViewType view) { m_modalView = view; }
void ViewManager::popModal() { m_modalView = ViewType::NONE; }

bool ViewManager::handleNavigation(const SDL_Event& event, bool shiftHeld) {
    if (m_modalView != ViewType::NONE) return false;

    if (shiftHeld && event.type == SDL_EVENT_KEY_DOWN) {
        int targetX = m_x;
        int targetY = m_y;

        if (event.key.key == SDLK_UP) targetY++;
        else if (event.key.key == SDLK_DOWN) targetY--;
        else if (event.key.key == SDLK_LEFT) targetX--;
        else if (event.key.key == SDLK_RIGHT) {
            if (m_x == 3 && m_y == 2) {
                targetX = 3;
                targetY = 0;
            } else {
                targetX++;
            }
        }
        else return false;

        if (getViewAt(targetX, targetY) != ViewType::NONE) {
            m_x = targetX;
            m_y = targetY;
        }
        return true; 
    }
    return false;
}

void ViewManager::renderChrome(Renderer& renderer, int bpm) {
    if (m_modalView != ViewType::NONE) return;

    SDL_Color dim = {100, 100, 100, 255};
    SDL_Color lite = {0, 255, 255, 255};

    renderer.drawString("T>" + std::to_string(bpm), 34, 2, dim);

    int baseX = 34;
    int baseY = 26;
    renderer.drawString("S", baseX + 0, baseY, (m_y == 0 && m_x == 0) ? lite : dim);
    renderer.drawString("C", baseX + 1, baseY, (m_y == 0 && m_x == 1) ? lite : dim);
    renderer.drawString("P", baseX + 2, baseY, (m_y == 0 && m_x == 2) ? lite : dim);
    renderer.drawString("I", baseX + 3, baseY, (m_y == 0 && m_x == 3) ? lite : dim);
    renderer.drawString("T", baseX + 4, baseY, (m_y == 0 && m_x == 4) ? lite : dim);

    int activeX = baseX + m_x;

    ViewType up1 = getViewAt(m_x, 1);
    if (up1 != ViewType::NONE) {
        std::string label = "P";
        if (up1 == ViewType::GROOVE) label = "G";
        else if (up1 == ViewType::INST_MOD) label = "M";
        renderer.drawString(label, activeX, baseY - 1, (m_y == 1) ? lite : dim);
    }

    ViewType up2 = getViewAt(m_x, 2);
    if (up2 != ViewType::NONE) {
        if (m_x == 2) {
            renderer.drawString("S", activeX, baseY - 2, (m_y == 2) ? lite : dim);
        } else if (m_x == 3) {
            renderer.drawString("P", activeX, baseY - 2, (m_y == 2) ? lite : dim);
        }
    }

    renderer.drawString("V", activeX, baseY + 1, (m_y == -1) ? lite : dim);
    if (m_y <= -1) {
        renderer.drawString("X", activeX, baseY + 2, (m_y == -2) ? lite : dim);
    }
}

} // namespace ui
} // namespace m8
