#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"
#include "../../UiCommands.h"
#include "../../ViewManager.h"
#include "../../FileBrowser.h"
#include "InstrumentCursorId.h"
#include <string>
#include <SDL3/SDL.h>

namespace m8 {
namespace ui {
namespace instrument {

enum class InstrumentBrowserMode {
    NONE,
    LOAD,
    SAVE_DIR
};

void RenderInstrumentScreen(Renderer& renderer,
                            const engine::EngineState& engState,
                            int currentInstIndex,
                            CursorId active_cursor_id);

// Arrow-key navigation/edit + ENTER (opens the file browser for SAMPLE_LOAD/
// CMD_LOAD/CMD_SAVE) on SDL_EVENT_KEY_DOWN.
void HandleInstrumentInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                            engine::EngineState& uiEngineState, int currentInstIndex,
                            CursorId& cursor_id, CommandSink& commandSink,
                            ViewManager& viewManager, bool& browserForSongLoad,
                            ::FileBrowser& fileBrowser, InstrumentBrowserMode& instrumentBrowserMode);

// X-release: opens the file browser for SAMPLE_LOAD/CMD_LOAD/CMD_SAVE
void HandleInstrumentEditRelease(CursorId cursor_id, bool& browserForSongLoad,
                                  ::FileBrowser& fileBrowser, ViewManager& viewManager,
                                  InstrumentBrowserMode& instrumentBrowserMode);

} // namespace instrument
} // namespace ui
} // namespace m8
