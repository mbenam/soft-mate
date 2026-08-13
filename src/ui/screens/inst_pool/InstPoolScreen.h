#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"
#include "../../UiCommands.h"
#include "../../ViewManager.h"
#include <vector>
#include <SDL3/SDL.h>

namespace m8 {
namespace ui {
namespace inst_pool {

void RenderInstPoolScreen(Renderer& renderer,
                          const engine::EngineState& engState,
                          int cursor_x, int cursor_y);

// The pool doubles as a jump list: selecting a row opens that instrument on the
// INSTRUMENT screen. That needs two things the pool didn't previously have
// access to -- the view position, and the instrument index the INSTRUMENT and
// MODS screens read -- so both are passed in by reference.
//
// Triggered by ENTER (here) and by an X *tap* (HandleInstPoolEditRelease). It
// has to be the tap, not X-down: X is also the pool's edit modifier, and
// X+UP/DOWN adjusts the value under the cursor. main.cpp guards the release
// with !arrowPressedDuringEdit so an edit gesture never navigates away
// mid-edit -- the same split PHRASE/CHAIN/SONG already use.
void HandleInstPoolInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                          engine::EngineState& uiEngineState, int& cursor_x, int& cursor_y,
                          CommandSink& commandSink,
                          ViewManager& viewManager, int& currentInstIndex);

// X-tap action: open the instrument under the cursor. Call only when no arrow
// was pressed during the edit hold.
void HandleInstPoolEditRelease(int cursor_y, ViewManager& viewManager, int& currentInstIndex);

} // namespace inst_pool
} // namespace ui
} // namespace m8
