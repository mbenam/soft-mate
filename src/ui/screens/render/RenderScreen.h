#pragma once
#include <SDL3/SDL.h>
#include "../../Renderer.h"
#include "../../ViewManager.h"
#include "../../CharPicker.h"
#include "../../../engine/Engine.h"
#include "../../../engine/SeqTypes.h"
#include "../../../io/RenderAudio.h"
#include "RenderScreenLayout.h"

namespace m8 {
namespace ui {
namespace render {

struct RenderScreenState {
    CursorId cursorId = CursorId::SONG_ROW_START;
    io::RenderSettings settings;
    int nameCharIndex = 0;
};

void RenderRenderScreen(Renderer& renderer, const RenderScreenState& state);

void HandleRenderInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                       RenderScreenState& state, const engine::Sequencer& uiSequencer,
                       const engine::EngineState& uiEngineState, ViewManager& viewManager,
                       CharPicker& charPicker);

void HandleRenderEditRelease(RenderScreenState& state, const engine::Sequencer& uiSequencer,
                             const engine::EngineState& uiEngineState, ViewManager& viewManager,
                             CharPicker& charPicker);

} // namespace render
} // namespace ui
} // namespace m8
