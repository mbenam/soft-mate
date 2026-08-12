#pragma once
#include <SDL3/SDL.h>
#include "Renderer.h"
#include <string>
#include <vector>

namespace m8 {
namespace ui {

enum class ViewType {
    SONG, CHAIN, PHRASE, INSTRUMENT, TABLE,
    PROJECT, GROOVE, MIXER, INST_MOD, EFFECTS,
    SCALE, INST_POOL, RENDER, FILE_BROWSER, CONFIRMATION, CHAR_PICKER, NONE
};

class ViewManager {
public:
    ViewManager();

    ViewType getCurrentView() const;
    ViewType getBaseView() const { return getViewAt(m_x, m_y); }
    int getCol() const { return m_x; }
    int getRow() const { return m_y; }
    void setCoords(int x, int y) { m_x = x; m_y = y; }

    // Intercepts Shift + D-Pad. Returns true if it consumed the event.
    bool handleNavigation(const SDL_Event& event, bool shiftHeld);

    // Renders the global Tempo and Minimap overlay
    void renderChrome(Renderer& renderer, int bpm);

    // Modal stack support (supports nested modals like RENDER -> CHAR_PICKER)
    void pushModal(ViewType view);
    void popModal();
    void clearModals();
    bool isModal() const { return !m_modalStack.empty(); }

private:
    int m_x = 0; // 0=Song, 1=Chain, 2=Phrase, 3=Inst, 4=Table
    int m_y = 0; // 0=Base, 1=Up1, 2=Up2, -1=Mixer, -2=FX
    
    std::vector<ViewType> m_modalStack;
    ViewType getViewAt(int x, int y) const;
};

} // namespace ui
} // namespace m8
