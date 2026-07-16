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
namespace phrase {

void RenderPhraseScreen(Renderer& renderer,
                        const engine::Sequencer& uiSequencer,
                        const engine::EngineState& engState, const engine::Playhead* playheads,
                        int currentPhrase,
                        int cursor_x, int cursor_y,
                        int songCol, // for playback indication
                        bool isPlaying);

// Arrow-key navigation/edit (SDL_EVENT_KEY_DOWN dispatch).
void HandlePhraseInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                        engine::Sequencer& uiSequencer, int currentPhrase,
                        int& cursor_x, int& cursor_y, CommandSink& commandSink);

// X-release insert-default when no arrow was pressed during the edit hold.
void HandlePhraseEditRelease(engine::Sequencer& uiSequencer, int currentPhrase,
                              int cursor_x, int cursor_y, CommandSink& commandSink);

} // namespace phrase
} // namespace ui
} // namespace m8
