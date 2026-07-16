#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"
#include <vector>
#include <SDL3/SDL.h>

namespace m8 {
namespace ui {
namespace table {

void RenderTableScreen(Renderer& renderer,
                       const engine::Sequencer& uiSequencer,
                       const engine::EngineState& engState,
                       int currentTableIndex,
                       int cursor_x, int cursor_y);

// Cursor-only navigation (SDL_EVENT_KEY_DOWN dispatch) -- Table has no edit
// mode wired up yet (AGENTS.md §8: "Tables are edited by the UI and ignored
// by the engine"), so this is pure cursor movement, no command pushes.
void HandleTableInput(const SDL_Event& event, bool editHeld, int& cursor_x, int& cursor_y);

} // namespace table
} // namespace ui
} // namespace m8
