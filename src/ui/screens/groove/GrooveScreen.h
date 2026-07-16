#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"
#include "../../UiCommands.h"
#include <SDL3/SDL.h>

namespace m8 {
namespace ui {
namespace groove {

void RenderGrooveScreen(Renderer& renderer,
                        const engine::EngineState& engState,
                        const engine::Groove& grooveData,
                        int currentGrooveIndex,
                        int cursor_y);

// Arrow-key navigation/edit (SDL_EVENT_KEY_DOWN dispatch).
void HandleGrooveInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                        engine::Groove& groove, int currentGrooveIndex, int& cursor_y,
                        CommandSink& commandSink);

// X-release toggle (0 <-> 6) when no arrow was pressed during the edit hold.
void HandleGrooveEditRelease(engine::Groove& groove, int currentGrooveIndex, int cursor_y,
                              CommandSink& commandSink);

} // namespace groove
} // namespace ui
} // namespace m8
