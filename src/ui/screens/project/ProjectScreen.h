#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"
#include "../../UiCommands.h"
#include "../../ViewManager.h"
#include "../../FileBrowser.h"
#include "../../../io/SongIO.h"
#include "ProjectScreenLayout.h"
#include <string>
#include <SDL3/SDL.h>

namespace m8 {
namespace ui {
namespace project {

void setSampleRoot(const std::string& root);
const std::string& getSampleRoot();

void RenderProjectScreen(Renderer& renderer,
                         const engine::EngineState& engState,
                         CursorId active_cursor_id);

// Cross-cutting state the PROJECT screen's LOAD/SAVE/SAMPLE ROOT actions
// touch, beyond the screen's own cursor -- bundled since main.cpp already
// owned all of it and passing 9 separate references is unreadable.
struct ProjectActionState {
    bool& browserForSongLoad;
    ::FileBrowser& fileBrowser;
    ViewManager& viewManager;
    bool& textInputActive;
    std::string& textInputBuffer;
    std::string& textInputPrompt;
    std::string& currentSongPath;
    m8::io::LoadResult& currentLoadResult;
    m8::engine::Sequencer& uiSequencer;
    std::string& missingSamplesMsg;
};

// Arrow-key navigation/edit + ENTER action (SDL_EVENT_KEY_DOWN dispatch).
void HandleProjectInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                         engine::EngineState& uiEngineState, CursorId& cursor_id,
                         CommandSink& commandSink, ProjectActionState& actions);

// X-release action (same LOAD/SAVE/SAMPLE ROOT dispatch as ENTER) when no
// arrow was pressed during the edit hold.
void HandleProjectEditRelease(CursorId cursor_id, engine::EngineState& uiEngineState,
                               ProjectActionState& actions);

} // namespace project
} // namespace ui
} // namespace m8
