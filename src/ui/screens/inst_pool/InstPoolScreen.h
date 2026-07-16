#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"
#include "../../UiCommands.h"
#include <vector>
#include <SDL3/SDL.h>

namespace m8 {
namespace ui {
namespace inst_pool {

void RenderInstPoolScreen(Renderer& renderer,
                          const engine::EngineState& engState,
                          int cursor_x, int cursor_y);

void HandleInstPoolInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                          engine::EngineState& uiEngineState, int& cursor_x, int& cursor_y,
                          CommandSink& commandSink);

} // namespace inst_pool
} // namespace ui
} // namespace m8
