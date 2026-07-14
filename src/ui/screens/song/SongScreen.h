#pragma once

#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Sequencer.h"
#include "../../../engine/Engine.h"
#include <vector>

namespace m8 {
namespace ui {
namespace song {

void RenderSongScreen(Renderer& renderer, 
                      const engine::Sequencer& uiSequencer,
                      const engine::EngineState& engState, const engine::Playhead* playheads,
                      int cursor_x, int cursor_y,
                      bool isPlaying);

} // namespace song
} // namespace ui
} // namespace m8
