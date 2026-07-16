#pragma once

#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Sequencer.h"
#include "../../../engine/Engine.h"
#include "../../UiCommands.h"
#include <vector>
#include <SDL3/SDL.h>

namespace m8 {
namespace ui {
namespace song {

void RenderSongScreen(Renderer& renderer,
                      const engine::Sequencer& uiSequencer,
                      const engine::EngineState& engState, const engine::Playhead* playheads,
                      int cursor_x, int cursor_y,
                      bool isPlaying);

// Arrow-key navigation/edit (SDL_EVENT_KEY_DOWN dispatch).
void HandleSongInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                      engine::Sequencer& uiSequencer, int& cursor_x, int& cursor_y,
                      CommandSink& commandSink);

// X-release toggle (empty <-> 0) when no arrow was pressed during the edit hold.
void HandleSongEditRelease(engine::Sequencer& uiSequencer, int cursor_x, int cursor_y,
                            CommandSink& commandSink);

} // namespace song
} // namespace ui
} // namespace m8
