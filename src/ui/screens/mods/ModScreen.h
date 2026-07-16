#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"
#include "../../UiCommands.h"
#include "ModScreenLayout.h"
#include <string>
#include <SDL3/SDL.h>

namespace m8 {
namespace ui {
namespace mods {

void RenderModScreen(Renderer& renderer,
                     const engine::EngineState& engState,
                     int currentInstIndex,
                     CursorId active_cursor_id);

void HandleModInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                     engine::EngineState& uiEngineState, int currentInstIndex,
                     CursorId& cursor_id, CommandSink& commandSink);

} // namespace mods
} // namespace ui
} // namespace m8
