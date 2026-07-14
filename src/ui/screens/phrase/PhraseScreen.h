#pragma once

#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Sequencer.h"
#include "../../../engine/Engine.h"
#include <vector>

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

} // namespace phrase
} // namespace ui
} // namespace m8
