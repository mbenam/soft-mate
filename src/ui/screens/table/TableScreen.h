#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../UiCommands.h"
#include "../../../engine/Engine.h"
#include <SDL3/SDL.h>

namespace m8 {
namespace ui {
namespace table {

void RenderTableScreen(Renderer& renderer,
                       const engine::Sequencer& uiSequencer,
                       const engine::EngineState& engState,
                       int currentTableIndex,
                       int cursor_x, int cursor_y,
                       int activeTableRow = -1);

void HandleTableInput(const SDL_Event& event,
                      bool editHeld, bool optHeld, bool shiftHeld,
                      bool& arrowPressedDuringEdit,
                      engine::Sequencer& uiSequencer,
                      int& currentTableIndex,
                      int& cursor_x, int& cursor_y,
                      CommandSink& commandSink);

void HandleTableEditRelease(engine::TableStep& step,
                            int currentTableIndex,
                            int cursor_x, int cursor_y,
                            CommandSink& commandSink);

} // namespace table
} // namespace ui
} // namespace m8
