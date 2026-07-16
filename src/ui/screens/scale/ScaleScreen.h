#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"
#include "../../UiCommands.h"
#include "ScaleScreenLayout.h"
#include <string>
#include <SDL3/SDL.h>

namespace m8 {
namespace ui {
namespace scale {

void RenderScaleScreen(Renderer& renderer,
                       const engine::EngineState& engState,
                       int currentScaleIndex,
                       CursorId active_cursor_id);

void HandleScaleInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                       engine::EngineState& uiEngineState, int currentScaleIndex,
                       CursorId& cursor_id, CommandSink& commandSink);

} // namespace scale
} // namespace ui
} // namespace m8
