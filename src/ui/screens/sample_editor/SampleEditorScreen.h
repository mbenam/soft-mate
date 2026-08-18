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
        isRecording = false;
        isArmed = false;
        recordedFrames = 0;
    }

    // Live Recording state
    bool isRecording = false;
    bool isArmed = false;
    uint32_t recordedFrames = 0;
    static constexpr uint32_t kMaxRecordFrames = 48000 * 60; // 60s @ 48kHz
    float* recordBuffer = nullptr;

    void ensureRecordBuffer() {
        if (!recordBuffer) {
            recordBuffer = (float*)calloc(kMaxRecordFrames * 2, sizeof(float));
        }
    }

    void freeRecordBuffer() {
        if (recordBuffer) {
            free(recordBuffer);
            recordBuffer = nullptr;
        }
    }

    void processIncomingAudio(const float* incomingFrames, int count, engine::SampleData* sd, CommandSink& sink) {
        if (!incomingFrames || count <= 0) return;
        ensureRecordBuffer();
        if (!recordBuffer) return;

        float volScale = (recVol / 208.0f); // 0xD0 (208) is unity 1.0

        if (isArmed) {
            float sumSq = 0.0f;
            for (int i = 0; i < count * 2; ++i) {
                sumSq += incomingFrames[i] * incomingFrames[i];
            }
            float rms = std::sqrt(sumSq / (count * 2));
            float armThreshold = (recArm / 255.0f) * 0.2f;
            if (rms >= armThreshold) {
                isArmed = false;
                isRecording = true;
                recordedFrames = 0;
            }
        }

        if (isRecording) {
            uint32_t toCopy = std::min<uint32_t>(count, kMaxRecordFrames - recordedFrames);
            for (uint32_t i = 0; i < toCopy; ++i) {
                recordBuffer[(recordedFrames + i) * 2 + 0] = incomingFrames[i * 2 + 0] * volScale;
                recordBuffer[(recordedFrames + i) * 2 + 1] = incomingFrames[i * 2 + 1] * volScale;
            }
            recordedFrames += toCopy;
            if (recordedFrames >= kMaxRecordFrames) {
                stopRecording(sd, sink);
            }
        }
    }

    void startOrArm() {
        ensureRecordBuffer();
        recordedFrames = 0;
        if (recArm > 0) {
            isArmed = true;
            isRecording = false;
        } else {
            isArmed = false;
            isRecording = true;
        }
    }

    void stopRecording(engine::SampleData* sd, CommandSink& sink) {
        if (isRecording || isArmed) {
            isRecording = false;
            isArmed = false;
            if (recordedFrames > 0 && recordBuffer) {
                size_t sz = static_cast<size_t>(recordedFrames) * 2 * sizeof(float);
                float* newPcm = (float*)malloc(sz);
                if (newPcm) {
                    std::memcpy(newPcm, recordBuffer, sz);
                    if (sd) {
                        free(sd->data);
                        sd->data = newPcm;
                        sd->frames = recordedFrames;
                        sd->channels = 2;
                        sd->sampleRate = 48000;
                        sd->sliceMarkerCount = 0;
                        sd->loopStartFrame = 0;
                        sd->loopEndFrame = recordedFrames;

                        selectStart = 0;
                        selectEnd = recordedFrames;
                        loopStart = 0;
                        loopEnd = recordedFrames;

                        engine::EngineCommand cmd{};
                        cmd.type = engine::CommandType::LOAD_SAMPLE;
                        cmd.targetId = instIndex;
                        cmd.u.sample = *sd;
                        sink.send(cmd);
                    }
                }
            }
        }
    }

    void freeUndo() {
        if (undoBuffer) {
            free(undoBuffer);
            undoBuffer = nullptr;
            undoFrames = 0;
        }
        freeRecordBuffer();
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
