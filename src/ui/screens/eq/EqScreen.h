#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"
#include "../../UiCommands.h"
#include "../../ViewManager.h"
#include "EqScreenLayout.h"
#include <SDL3/SDL.h>

namespace m8 {
namespace ui {
namespace eq {

// Which bank the editor is pointed at, and where the cursor sits in the 5x3
// parameter grid. Owned by main.cpp, like every other screen's cursor state.
struct EqScreenState {
    int bank = 0;     // index into EngineState::eqs
    int param = 0;    // Param enum: GAIN/FREQ/Q/TYPE/MODE
    int band = 0;     // 0 LOW, 1 MID, 2 HIGH
};

void RenderEqScreen(Renderer& renderer,
                    const engine::EngineState& engState,
                    const EqScreenState& st);

// Arrows move the cursor; X + arrows edit. Returns true if the screen wants to
// close (OPTION pressed), which main.cpp turns into popModal().
bool HandleEqInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                   engine::EngineState& uiEngineState, EqScreenState& st,
                   CommandSink& commandSink);

} // namespace eq
} // namespace ui
} // namespace m8
