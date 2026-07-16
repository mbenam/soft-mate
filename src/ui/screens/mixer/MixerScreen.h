#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"
#include "../../UiCommands.h"
#include "MixerScreenLayout.h"
#include <string>
#include <SDL3/SDL.h>

namespace m8 {
namespace ui {
namespace mixer {

void RenderMixerScreen(Renderer& renderer,
                       const engine::EngineState& engState,
                       CursorId active_cursor_id);

void HandleMixerInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                       engine::EngineState& uiEngineState, CursorId& cursor_id,
                       CommandSink& commandSink);

} // namespace mixer
} // namespace ui
} // namespace m8
