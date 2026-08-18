#pragma once
#include "../../ui_types.h"
#include "../../Renderer.h"
#include "../../../engine/Engine.h"
#include "../../UiCommands.h"
#include "../../ViewManager.h"
#include "SampleEditorLayout.h"
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cstring>

namespace m8 {
namespace ui {
namespace sample_editor {

struct SampleEditorState {
    int instIndex = 0;
    uint32_t selectStart = 0;
    uint32_t selectEnd = 0;
    uint32_t loopStart = 0;
    uint32_t loopEnd = 0;
    int currentSliceMarker = 0;
    int processIndex = 0;
    char name[19] = {};

    // RECORD row fields
    int recSrc = 0;   // 0 = L&R
    int recVol = 0xD0;
    int recArm = 0x20;
    int recSong = 0;  // 0 = NO

    // Cursor position
    CursorRow row = CursorRow::SELECT;
    int subCol = 0;

    // Single-level undo buffer
    float* undoBuffer = nullptr;
    uint32_t undoFrames = 0;
    uint8_t undoChannels = 1;
    uint32_t undoSampleRate = 44100;
    uint32_t undoSliceMarkers[128] = {};
    int undoSliceMarkerCount = 0;

    void init(int instIdx, const engine::Instrument& inst, const engine::SampleData* sd) {
        instIndex = instIdx;
        row = CursorRow::SELECT;
        subCol = 0;
        processIndex = 0;
        currentSliceMarker = 0;
        recSrc = 0;
        recVol = 0xD0;
        recArm = 0x20;
        recSong = 0;

        std::memset(name, 0, sizeof(name));
        if (inst.sampler.samplePath[0] != '\0') {
            std::string p = inst.sampler.samplePath;
            size_t slash = p.find_last_of("/\\");
            std::string n = (slash != std::string::npos) ? p.substr(slash + 1) : p;
            size_t dot = n.find_last_of('.');
            if (dot != std::string::npos) n = n.substr(0, dot);
            std::strncpy(name, n.c_str(), sizeof(name) - 1);
        } else {
            std::strncpy(name, "UNTITLED", sizeof(name) - 1);
        }

        if (sd && sd->frames > 0) {
            selectStart = 0;
            selectEnd = sd->frames;
            loopStart = sd->loopStartFrame;
            loopEnd = (sd->loopEndFrame > 0) ? sd->loopEndFrame : sd->frames;
        } else {
            selectStart = 0;
            selectEnd = 0;
            loopStart = 0;
            loopEnd = 0;
        }
    }

    void freeUndo() {
        if (undoBuffer) {
            free(undoBuffer);
            undoBuffer = nullptr;
            undoFrames = 0;
        }
    }
};

void RenderSampleEditorScreen(Renderer& renderer,
                              const engine::EngineState& engState,
                              const SampleEditorState& st,
                              const engine::SampleData* sd);

// Handles input for Sample Editor. Returns true when OPT is pressed to pop modal.
bool HandleSampleEditorInput(const SDL_Event& event, bool editHeld, bool optHeld,
                             engine::EngineState& uiEngineState, SampleEditorState& st,
                             engine::SampleData* sd, CommandSink& commandSink);

} // namespace sample_editor
} // namespace ui
} // namespace m8
