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

// Live levels for the meters, read from the engine's atomics by main.cpp and
// passed in -- the screen never touches engine state itself (AGENTS.md §6),
// exactly like the playhead array the sequencer screens receive.
struct MixerLevels {
    engine::MeterLevel track[8]{};
    engine::MeterLevel master{};
    engine::MeterLevel send[3]{};   // 0 = CHO (MX), 1 = DEL, 2 = REV
};

void RenderMixerScreen(Renderer& renderer,
                       const engine::EngineState& engState,
                       CursorId active_cursor_id,
                       const MixerLevels& levels);

void HandleMixerInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                       engine::EngineState& uiEngineState, CursorId& cursor_id,
                       CommandSink& commandSink);

// X-tap action: on EQ, open the editor on the main mix EQ. Returns true if the
// editor should be opened. Call only when no arrow was pressed during the hold.
bool HandleMixerEditRelease(CursorId cursor_id);

} // namespace mixer
} // namespace ui
} // namespace m8
