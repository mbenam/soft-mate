#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"
#include "../../UiCommands.h"
#include "EffectsScreenLayout.h"
#include <string>
#include <SDL3/SDL.h>

namespace m8 {
namespace ui {
namespace effects {

void RenderEffectsScreen(Renderer& renderer,
                         const engine::EngineState& engState,
                         CursorId active_cursor_id);

void HandleEffectsInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                         engine::EngineState& uiEngineState, CursorId& cursor_id,
                         CommandSink& commandSink);

} // namespace effects
} // namespace ui
} // namespace m8
