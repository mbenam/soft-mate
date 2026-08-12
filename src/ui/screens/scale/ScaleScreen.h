#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"
#include "../../UiCommands.h"
#include "../../ViewManager.h"
#include "../../FileBrowser.h"
#include "ScaleScreenLayout.h"
#include <string>
#include <SDL3/SDL.h>

namespace m8 {
namespace ui {
namespace scale {

enum class ScaleBrowserMode {
    NONE,
    LOAD,
    SAVE_DIR
};

struct ScaleActionState {
    ::FileBrowser& fileBrowser;
    ViewManager& viewManager;
    ScaleBrowserMode& scaleBrowserMode;
};

void RenderScaleScreen(Renderer& renderer,
                       const engine::EngineState& engState,
                       int currentScaleIndex,
                       CursorId active_cursor_id,
                       int nameCharIndex = 0);

void HandleScaleInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                       engine::EngineState& uiEngineState, int currentScaleIndex,
                       CursorId& cursor_id, int& nameCharIndex, CommandSink& commandSink,
                       ScaleActionState* actionState = nullptr);

void HandleScaleEditRelease(CursorId cursor_id, int currentScaleIndex,
                            engine::EngineState& uiEngineState, CommandSink& commandSink,
                            ScaleActionState& actionState);

} // namespace scale
} // namespace ui
} // namespace m8

