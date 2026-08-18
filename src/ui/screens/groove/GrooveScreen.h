#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"
#include "../../../engine/Sequencer.h"
#include "../../UiCommands.h"
#include <SDL3/SDL.h>

namespace m8 {
namespace ui {
namespace groove {

void RenderGrooveScreen(Renderer& renderer,
                        const engine::EngineState& engState,
                        const engine::Groove& grooveData,
                        int currentGrooveIndex,
                        int cursor_x,
                        int cursor_y);

// Arrow-key navigation/edit, OPTION navigation, and delete.
void HandleGrooveInput(const SDL_Event& event, bool editHeld, bool optHeld, bool shiftHeld,
                        bool& arrowPressedDuringEdit,
                        engine::Sequencer& sequencer, int& currentGrooveIndex,
                        int& cursor_x, int& cursor_y,
                        uint8_t& lastEditedValue,
                        CommandSink& commandSink);

// Edit release toggle / insert.
void HandleGrooveEditRelease(engine::Groove& groove, int currentGrooveIndex,
                              int cursor_x, int cursor_y,
                              uint8_t lastEditedValue,
                              CommandSink& commandSink);

} // namespace groove
} // namespace ui
} // namespace m8
