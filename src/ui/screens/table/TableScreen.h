#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"
#include <vector>

namespace m8 {
namespace ui {
namespace table {

void RenderTableScreen(Renderer& renderer, 
                       const engine::Sequencer& uiSequencer,
                       const engine::EngineState& engState,
                       int currentTableIndex,
                       int cursor_x, int cursor_y);

} // namespace table
} // namespace ui
} // namespace m8
