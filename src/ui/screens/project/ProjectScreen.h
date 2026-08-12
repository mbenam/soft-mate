#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"
#include "../../UiCommands.h"
#include "../../ViewManager.h"
#include "../../FileBrowser.h"
#include "../../../io/SongIO.h"
#include "../../ConfirmationDialog.h"
#include "../../CharPicker.h"
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
                         CursorId active_cursor_id,
                         int nameCharIndex = 0);

// Cross-cutting state the PROJECT screen's LOAD/SAVE/NEW/SAMPLE ROOT actions
// touch, beyond the screen's own cursor -- bundled since main.cpp already
// owned all of it and passing separate references is unreadable.
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
    ConfirmationDialog& confirmDialog;
    CharPicker& charPicker;
    int& nameCharIndex;
};

// Arrow-key navigation/edit + ENTER action (SDL_EVENT_KEY_DOWN dispatch).
void HandleProjectInput(const SDL_Event& event, bool editHeld, bool& arrowPressedDuringEdit,
                         engine::EngineState& uiEngineState, CursorId& cursor_id,
                         int& nameCharIndex, CommandSink& commandSink, ProjectActionState& actions);

// Key release handler (e.g. for tempo nudge reset).
void HandleProjectKeyUp(const SDL_Event& event, engine::EngineState& uiEngineState,
                        CursorId cursor_id, CommandSink& commandSink);

// X-release action (same LOAD/SAVE/SAMPLE ROOT dispatch as ENTER) when no
// arrow was pressed during the edit hold.
void HandleProjectEditRelease(CursorId cursor_id, int& nameCharIndex,
                               engine::EngineState& uiEngineState, CommandSink& commandSink,
                               ProjectActionState& actions);

void setTransposeNoticeUntil(uint64_t ticks);
uint64_t getTransposeNoticeUntil();

} // namespace project
} // namespace ui
} // namespace m8
