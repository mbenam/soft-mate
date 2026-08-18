#include "SampleEditorScreen.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace m8 {
namespace ui {
namespace sample_editor {

static SDL_Color GetColorFromString(const std::string& name) {
    if (name == "TEXT_ACCENT") return {0, 255, 255, 255};
    if (name == "LABEL_LITE")  return {200, 220, 255, 255};
    if (name == "LABEL_DIM")   return {80, 100, 140, 255};
    if (name == "VALUE")       return {255, 255, 255, 255};
    if (name == "ACCENT")      return {0, 255, 255, 255};
    return {160, 160, 160, 255};
}

void RenderSampleEditorScreen(Renderer& renderer,
                              const engine::EngineState& engState,
                              const SampleEditorState& st,
                              const engine::SampleData* sd) {
    // Title
    renderer.drawString("SAMPLE", 0, 0, GetColorFromString("LABEL_LITE"));

    // Header row 1 (for RECORD)
    renderer.drawString("REC", 8, 1, GetColorFromString("LABEL_DIM"));
    renderer.drawString("SRC", 14, 1, GetColorFromString("LABEL_DIM"));
    renderer.drawString("VOL", 18, 1, GetColorFromString("LABEL_DIM"));
    renderer.drawString("ARM", 22, 1, GetColorFromString("LABEL_DIM"));
    renderer.drawString("SONG", 26, 1, GetColorFromString("LABEL_DIM"));

    // Row 2: RECORD row
    renderer.drawString("RECORD", 0, 2, GetColorFromString("LABEL_DIM"));
    std::string recBtnText = st.isRecording ? "STOP " : (st.isArmed ? "ARMED" : "START");
    SDL_Color recBtnColor = st.isRecording ? SDL_Color{255, 60, 60, 255} : (st.isArmed ? SDL_Color{255, 200, 50, 255} : GetColorFromString(st.row == CursorRow::RECORD && st.subCol == 0 ? "VALUE" : "LABEL_DIM"));
    renderer.drawString(recBtnText, 8, 2, recBtnColor);
    renderer.drawString(kRecordSources[std::clamp(st.recSrc, 0, 15)], 14, 2, GetColorFromString(st.row == CursorRow::RECORD && st.subCol == 1 ? "VALUE" : "LABEL_DIM"));
    char vBuf[16];
    std::snprintf(vBuf, sizeof(vBuf), "%02X", st.recVol & 0xFF);
    renderer.drawString(vBuf, 18, 2, GetColorFromString(st.row == CursorRow::RECORD && st.subCol == 2 ? "VALUE" : "LABEL_DIM"));
    std::snprintf(vBuf, sizeof(vBuf), "%02X", st.recArm & 0xFF);
    renderer.drawString(vBuf, 22, 2, GetColorFromString(st.row == CursorRow::RECORD && st.subCol == 3 ? "VALUE" : "LABEL_DIM"));
    renderer.drawString(st.recSong == 0 ? "NO " : "YES", 26, 2, GetColorFromString(st.row == CursorRow::RECORD && st.subCol == 4 ? "VALUE" : "LABEL_DIM"));

    // Waveform Canvas (Rows 3..10, 8 cells tall, 40 cols wide)
    const int waveX0 = 0;
    const int waveY0 = kWaveTopRow * 8;
    const int waveW  = kGridCols * 8;
    const int waveH  = kWaveCanvasHeight * 8;
    const int midY   = waveY0 + waveH / 2;

    // Draw background boundary / box
    renderer.fillRectPixel(waveX0, waveY0, waveW, waveH, {20, 25, 35, 255});

    if (sd && sd->data && sd->frames > 0) {
        uint32_t totalFrames = sd->frames;
        int ch = sd->channels;

        // Draw selection region highlight
        if (st.selectEnd > st.selectStart && totalFrames > 0) {
            int selPxStart = (static_cast<uint64_t>(st.selectStart) * waveW) / totalFrames;
            int selPxEnd   = (static_cast<uint64_t>(st.selectEnd) * waveW) / totalFrames;
            int selW = std::max(1, selPxEnd - selPxStart);
            renderer.fillRectPixel(waveX0 + selPxStart, waveY0, selW, waveH, {40, 55, 75, 255});
        }

        // Draw center line
        renderer.fillRectPixel(waveX0, midY, waveW, 1, {45, 60, 80, 255});

        // Draw downsampled waveform columns
        for (int px = 0; px < waveW; ++px) {
            uint32_t f0 = (static_cast<uint64_t>(px) * totalFrames) / waveW;
            uint32_t f1 = (static_cast<uint64_t>(px + 1) * totalFrames) / waveW;
            if (f1 <= f0) f1 = f0 + 1;
            if (f1 > totalFrames) f1 = totalFrames;

            float minV = 0.0f;
            float maxV = 0.0f;
            for (uint32_t f = f0; f < f1; ++f) {
                float s = sd->data[f * ch]; // channel 0
                if (s < minV) minV = s;
                if (s > maxV) maxV = s;
            }

            int yTop = midY - static_cast<int>(maxV * (waveH / 2 - 2));
            int yBot = midY - static_cast<int>(minV * (waveH / 2 - 2));
            if (yBot <= yTop) yBot = yTop + 1;
            int barH = yBot - yTop;

            SDL_Color waveColor = {0, 255, 255, 255};
            renderer.fillRectPixel(waveX0 + px, yTop, 1, barH, waveColor);
        }

        // Draw slice markers (yellow/amber vertical lines)
        for (int m = 0; m < sd->sliceMarkerCount; ++m) {
            uint32_t mf = sd->sliceMarkers[m];
            int mpx = (static_cast<uint64_t>(mf) * waveW) / totalFrames;
            renderer.fillRectPixel(waveX0 + mpx, waveY0, 1, waveH, {255, 200, 50, 255});
        }

        // Draw loop start and loop end markers (green / red)
        if (st.loopEnd > st.loopStart) {
            int lsPx = (static_cast<uint64_t>(st.loopStart) * waveW) / totalFrames;
            int lePx = (static_cast<uint64_t>(st.loopEnd) * waveW) / totalFrames;
            renderer.fillRectPixel(waveX0 + lsPx, waveY0, 1, waveH, {50, 255, 100, 255});
            renderer.fillRectPixel(waveX0 + lePx, waveY0, 1, waveH, {255, 80, 80, 255});
        }
    }

    // Row 11: SELECT
    renderer.drawString("SELECT", 0, 11, GetColorFromString("LABEL_DIM"));
    char hexBuf[16];
    std::snprintf(hexBuf, sizeof(hexBuf), "%08X", st.selectStart);
    renderer.drawString(hexBuf, 13, 11, GetColorFromString(st.row == CursorRow::SELECT && st.subCol == 0 ? "VALUE" : "LABEL_DIM"));
    std::snprintf(hexBuf, sizeof(hexBuf), "%08X", st.selectEnd);
    renderer.drawString(hexBuf, 22, 11, GetColorFromString(st.row == CursorRow::SELECT && st.subCol == 1 ? "VALUE" : "LABEL_DIM"));

    // Row 12: LOOP REGION
    renderer.drawString("LOOP REGION", 0, 12, GetColorFromString("LABEL_DIM"));
    std::snprintf(hexBuf, sizeof(hexBuf), "%08X", st.loopStart);
    renderer.drawString(hexBuf, 13, 12, GetColorFromString(st.row == CursorRow::LOOP_REGION && st.subCol == 0 ? "VALUE" : "LABEL_DIM"));
    std::snprintf(hexBuf, sizeof(hexBuf), "%08X", st.loopEnd);
    renderer.drawString(hexBuf, 22, 12, GetColorFromString(st.row == CursorRow::LOOP_REGION && st.subCol == 1 ? "VALUE" : "LABEL_DIM"));

    // Row 13: SLICE MARKER
    renderer.drawString("SLICE MARKER", 0, 13, GetColorFromString("LABEL_DIM"));
    uint32_t markerPos = (sd && st.currentSliceMarker < sd->sliceMarkerCount) ? sd->sliceMarkers[st.currentSliceMarker] : 0;
    std::snprintf(hexBuf, sizeof(hexBuf), "%02X:%08X", st.currentSliceMarker & 0xFF, markerPos);
    renderer.drawString(hexBuf, 13, 13, GetColorFromString(st.row == CursorRow::SLICE_MARKER ? "VALUE" : "LABEL_DIM"));

    // Row 15: PROCESS
    renderer.drawString("PROCESS", 0, 15, GetColorFromString("LABEL_DIM"));
    std::string procStr = GetProcessString(st.processIndex);
    renderer.drawString(procStr, 13, 15, GetColorFromString(st.row == CursorRow::PROCESS && st.subCol == 0 ? "VALUE" : "LABEL_DIM"));
    renderer.drawString(">", 24, 15, GetColorFromString(st.row == CursorRow::PROCESS && st.subCol == 1 ? "VALUE" : "LABEL_DIM"));
    renderer.drawString("UNDO", 26, 15, GetColorFromString(st.row == CursorRow::PROCESS && st.subCol == 2 ? "VALUE" : "LABEL_DIM"));

    // Row 17: NAME
    renderer.drawString("NAME", 0, 17, GetColorFromString("LABEL_DIM"));
    char namePadded[19];
    std::snprintf(namePadded, sizeof(namePadded), "%-18s", st.name);
    renderer.drawString(namePadded, 13, 17, GetColorFromString(st.row == CursorRow::NAME ? "VALUE" : "LABEL_DIM"));

    // Row 18: SAVE / OVERWRITE
    renderer.drawString("SAVE", 13, 18, GetColorFromString(st.row == CursorRow::SAVE && st.subCol == 0 ? "VALUE" : "LABEL_DIM"));
    renderer.drawString("OVERWRITE", 18, 18, GetColorFromString(st.row == CursorRow::SAVE && st.subCol == 1 ? "VALUE" : "LABEL_DIM"));

    // Render active cursor brackets
    SDL_Color cursorColor = {0, 255, 255, 255};
    switch (st.row) {
    case CursorRow::RECORD:
        if (st.subCol == 0) renderer.drawBracket(8, 2, 5, cursorColor);
        else if (st.subCol == 1) renderer.drawBracket(14, 2, 3, cursorColor);
        else if (st.subCol == 2) renderer.drawBracket(18, 2, 2, cursorColor);
        else if (st.subCol == 3) renderer.drawBracket(22, 2, 2, cursorColor);
        else if (st.subCol == 4) renderer.drawBracket(26, 2, 3, cursorColor);
        break;
    case CursorRow::SELECT:
        if (st.subCol == 0) renderer.drawBracket(13, 11, 8, cursorColor);
        else renderer.drawBracket(22, 11, 8, cursorColor);
        break;
    case CursorRow::LOOP_REGION:
        if (st.subCol == 0) renderer.drawBracket(13, 12, 8, cursorColor);
        else renderer.drawBracket(22, 12, 8, cursorColor);
        break;
    case CursorRow::SLICE_MARKER:
        renderer.drawBracket(13, 13, 11, cursorColor);
        break;
    case CursorRow::PROCESS:
        if (st.subCol == 0) renderer.drawBracket(13, 15, static_cast<int>(procStr.length()), cursorColor);
        else if (st.subCol == 1) renderer.drawBracket(24, 15, 1, cursorColor);
        else if (st.subCol == 2) renderer.drawBracket(26, 15, 4, cursorColor);
        break;
    case CursorRow::NAME:
        renderer.drawBracket(13, 17, 18, cursorColor);
        break;
    case CursorRow::SAVE:
        if (st.subCol == 0) renderer.drawBracket(13, 18, 4, cursorColor);
        else renderer.drawBracket(18, 18, 9, cursorColor);
        break;
    default:
        break;
    }
}

// Helpers for buffer processes
static void SaveUndo(SampleEditorState& st, const engine::SampleData* sd) {
    if (!sd || !sd->data || sd->frames == 0) return;
    st.freeUndo();
    size_t sz = static_cast<size_t>(sd->frames) * sd->channels * sizeof(float);
    st.undoBuffer = (float*)malloc(sz);
    if (st.undoBuffer) {
        std::memcpy(st.undoBuffer, sd->data, sz);
        st.undoFrames = sd->frames;
        st.undoChannels = sd->channels;
        st.undoSampleRate = sd->sampleRate;
        st.undoSliceMarkerCount = sd->sliceMarkerCount;
        std::memcpy(st.undoSliceMarkers, sd->sliceMarkers, sizeof(st.undoSliceMarkers));
    }
}

static void RestoreUndo(SampleEditorState& st, engine::SampleData* sd) {
    if (!sd || !st.undoBuffer || st.undoFrames == 0) return;
    free(sd->data);
    size_t sz = static_cast<size_t>(st.undoFrames) * st.undoChannels * sizeof(float);
    sd->data = (float*)malloc(sz);
    if (sd->data) {
        std::memcpy(sd->data, st.undoBuffer, sz);
        sd->frames = st.undoFrames;
        sd->channels = st.undoChannels;
        sd->sampleRate = st.undoSampleRate;
        sd->sliceMarkerCount = st.undoSliceMarkerCount;
        std::memcpy(sd->sliceMarkers, st.undoSliceMarkers, sizeof(sd->sliceMarkers));
        st.selectStart = 0;
        st.selectEnd = sd->frames;
    }
}

static void ExecuteProcess(SampleEditorState& st, engine::SampleData* sd, int processIdx) {
    if (!sd || !sd->data || sd->frames == 0) return;
    SaveUndo(st, sd);

    uint32_t frames = sd->frames;
    int ch = sd->channels;
    uint32_t s0 = std::min(st.selectStart, frames);
    uint32_t s1 = std::min(st.selectEnd, frames);
    if (s1 < s0) std::swap(s0, s1);
    if (s0 == s1 && processIdx != 16 && processIdx != 17 && processIdx < 18) return;

    switch (processIdx) {
    case 0: { // CROP
        uint32_t newFrames = s1 - s0;
        if (newFrames == 0) return;
        float* newData = (float*)malloc(newFrames * ch * sizeof(float));
        if (newData) {
            std::memcpy(newData, sd->data + s0 * ch, newFrames * ch * sizeof(float));
            free(sd->data);
            sd->data = newData;
            sd->frames = newFrames;
            st.selectStart = 0;
            st.selectEnd = newFrames;
        }
        break;
    }
    case 1: { // DELETE
        uint32_t delLen = s1 - s0;
        uint32_t newFrames = frames - delLen;
        if (newFrames == 0) return;
        float* newData = (float*)malloc(newFrames * ch * sizeof(float));
        if (newData) {
            if (s0 > 0) std::memcpy(newData, sd->data, s0 * ch * sizeof(float));
            if (frames > s1) std::memcpy(newData + s0 * ch, sd->data + s1 * ch, (frames - s1) * ch * sizeof(float));
            free(sd->data);
            sd->data = newData;
            sd->frames = newFrames;
            st.selectStart = s0;
            st.selectEnd = s0;
        }
        break;
    }
    case 2: { // DUPLICATE
        uint32_t dupLen = s1 - s0;
        uint32_t newFrames = frames + dupLen;
        float* newData = (float*)malloc(newFrames * ch * sizeof(float));
        if (newData) {
            std::memcpy(newData, sd->data, s1 * ch * sizeof(float));
            std::memcpy(newData + s1 * ch, sd->data + s0 * ch, dupLen * ch * sizeof(float));
            if (frames > s1) std::memcpy(newData + (s1 + dupLen) * ch, sd->data + s1 * ch, (frames - s1) * ch * sizeof(float));
            free(sd->data);
            sd->data = newData;
            sd->frames = newFrames;
            st.selectEnd = s1 + dupLen;
        }
        break;
    }
    case 3: { // NORMALIZE
        float maxVal = 0.0f;
        for (uint32_t f = s0; f < s1; ++f) {
            for (int c = 0; c < ch; ++c) {
                float v = std::abs(sd->data[f * ch + c]);
                if (v > maxVal) maxVal = v;
            }
        }
        if (maxVal > 1e-6f) {
            float gain = 1.0f / maxVal;
            for (uint32_t f = s0; f < s1; ++f) {
                for (int c = 0; c < ch; ++c) {
                    sd->data[f * ch + c] *= gain;
                }
            }
        }
        break;
    }
    case 4: { // SILENCE
        for (uint32_t f = s0; f < s1; ++f) {
            for (int c = 0; c < ch; ++c) {
                sd->data[f * ch + c] = 0.0f;
            }
        }
        break;
    }
    case 5: { // REVERSE
        for (uint32_t i = 0; i < (s1 - s0) / 2; ++i) {
            uint32_t fA = s0 + i;
            uint32_t fB = s1 - 1 - i;
            for (int c = 0; c < ch; ++c) {
                std::swap(sd->data[fA * ch + c], sd->data[fB * ch + c]);
            }
        }
        break;
    }
    case 6: { // INVERT
        for (uint32_t f = s0; f < s1; ++f) {
            for (int c = 0; c < ch; ++c) {
                sd->data[f * ch + c] = -sd->data[f * ch + c];
            }
        }
        break;
    }
    case 7: { // FADE IN
        uint32_t len = s1 - s0;
        if (len > 0) {
            for (uint32_t i = 0; i < len; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(len);
                for (int c = 0; c < ch; ++c) {
                    sd->data[(s0 + i) * ch + c] *= t;
                }
            }
        }
        break;
    }
    case 8: { // FADE OUT
        uint32_t len = s1 - s0;
        if (len > 0) {
            for (uint32_t i = 0; i < len; ++i) {
                float t = 1.0f - static_cast<float>(i) / static_cast<float>(len);
                for (int c = 0; c < ch; ++c) {
                    sd->data[(s0 + i) * ch + c] *= t;
                }
            }
        }
        break;
    }
    case 9: { // XFADE LOOP
        uint32_t xfadeLen = std::min((s1 - s0) / 4, frames / 8);
        if (xfadeLen > 0) {
            for (uint32_t i = 0; i < xfadeLen; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(xfadeLen);
                for (int c = 0; c < ch; ++c) {
                    float vA = sd->data[(s0 + i) * ch + c];
                    float vB = sd->data[(s1 - xfadeLen + i) * ch + c];
                    sd->data[(s0 + i) * ch + c] = std::sqrt(t) * vA + std::sqrt(1.0f - t) * vB;
                }
            }
        }
        break;
    }
    case 10: { // SQUISH(OTT)
        for (uint32_t f = s0; f < s1; ++f) {
            for (int c = 0; c < ch; ++c) {
                float x = sd->data[f * ch + c];
                sd->data[f * ch + c] = std::tanh(x * 1.5f);
            }
        }
        break;
    }
    case 11: { // MONO:MIX
        if (ch >= 2) {
            for (uint32_t f = 0; f < frames; ++f) {
                float avg = 0.5f * (sd->data[f * ch + 0] + sd->data[f * ch + 1]);
                sd->data[f * ch + 0] = avg;
                sd->data[f * ch + 1] = avg;
            }
        }
        break;
    }
    case 12: { // MONO:LEFT
        if (ch >= 2) {
            for (uint32_t f = 0; f < frames; ++f) {
                sd->data[f * ch + 1] = sd->data[f * ch + 0];
            }
        }
        break;
    }
    case 13: { // MONO:RIGHT
        if (ch >= 2) {
            for (uint32_t f = 0; f < frames; ++f) {
                sd->data[f * ch + 0] = sd->data[f * ch + 1];
            }
        }
        break;
    }
    case 14: { // DOWNSAMPLE
        uint32_t newFrames = frames / 2;
        if (newFrames == 0) return;
        float* newData = (float*)malloc(newFrames * ch * sizeof(float));
        if (newData) {
            for (uint32_t f = 0; f < newFrames; ++f) {
                for (int c = 0; c < ch; ++c) {
                    newData[f * ch + c] = sd->data[(f * 2) * ch + c];
                }
            }
            free(sd->data);
            sd->data = newData;
            sd->frames = newFrames;
            st.selectStart /= 2;
            st.selectEnd /= 2;
        }
        break;
    }
    case 15: { // 8-BIT
        for (uint32_t f = s0; f < s1; ++f) {
            for (int c = 0; c < ch; ++c) {
                float v = sd->data[f * ch + c];
                sd->data[f * ch + c] = std::round(v * 127.0f) / 127.0f;
            }
        }
        break;
    }
    case 16: { // SLICE:AUTO (Transient Onset Detection)
        std::vector<uint32_t> markers;
        markers.push_back(0);
        constexpr int kHop = 32;
        constexpr int kWin = 64;
        constexpr int kMinDistance = 128;
        if (frames > kWin * 2) {
            float prevEnergy = 0.0f;
            uint32_t lastMarker = 0;
            for (uint32_t f = kWin; f + kWin <= frames; f += kHop) {
                float energy = 0.0f;
                for (int i = 0; i < kWin; ++i) {
                    for (int c = 0; c < ch; ++c) {
                        float v = sd->data[(f + i) * ch + c];
                        energy += v * v;
                    }
                }
                energy /= (kWin * ch);
                float diff = energy - prevEnergy;
                if (diff > 0.004f && (f - lastMarker >= kMinDistance)) {
                    // Pinpoint the first sample in this window crossing above floor
                    uint32_t onset = f;
                    for (int i = 0; i < kWin && f + i < frames; ++i) {
                        float maxSamp = 0.0f;
                        for (int c = 0; c < ch; ++c) maxSamp = std::max(maxSamp, std::abs(sd->data[(f + i) * ch + c]));
                        if (maxSamp > 0.05f) { onset = f + i; break; }
                    }
                    if (onset > lastMarker + 64) {
                        markers.push_back(onset);
                        lastMarker = onset;
                        if (markers.size() >= engine::SampleData::kMaxSliceMarkers) break;
                    }
                }
                prevEnergy = energy * 0.85f;
            }
        }
        sd->sliceMarkerCount = static_cast<int>(markers.size());
        for (size_t m = 0; m < markers.size(); ++m) {
            sd->sliceMarkers[m] = markers[m];
        }
        break;
    }
    case 17: { // SLICE:SILEN (Silence Slicing)
        std::vector<uint32_t> markers;
        markers.push_back(0);
        constexpr int kBlock = 128;
        constexpr float kSilenceThreshold = 0.002f; // ~ -54 dB
        bool inSilence = false;
        uint32_t silenceLen = 0;
        for (uint32_t f = 0; f + kBlock <= frames; f += kBlock) {
            float rms = 0.0f;
            for (int i = 0; i < kBlock; ++i) {
                for (int c = 0; c < ch; ++c) {
                    float v = sd->data[(f + i) * ch + c];
                    rms += v * v;
                }
            }
            rms = std::sqrt(rms / (kBlock * ch));
            if (rms < kSilenceThreshold) {
                silenceLen += kBlock;
                if (silenceLen >= 256) inSilence = true;
            } else {
                if (inSilence && f > 0) {
                    if (markers.back() != f) {
                        markers.push_back(f);
                        if (markers.size() >= engine::SampleData::kMaxSliceMarkers) break;
                    }
                    inSilence = false;
                }
                silenceLen = 0;
            }
        }
        sd->sliceMarkerCount = static_cast<int>(markers.size());
        for (size_t m = 0; m < markers.size(); ++m) {
            sd->sliceMarkers[m] = markers[m];
        }
        break;
    }
    default: { // SLICE:002..128
        int sliceCount = (processIdx >= 18) ? (processIdx - 18 + 2) : 16;
        sliceCount = std::clamp(sliceCount, 2, 128);
        sd->sliceMarkerCount = sliceCount;
        for (int m = 0; m < sliceCount; ++m) {
            sd->sliceMarkers[m] = (static_cast<uint64_t>(m) * frames) / sliceCount;
        }
        break;
    }
    }
}

bool HandleSampleEditorInput(const SDL_Event& event, bool editHeld, bool optHeld,
                             engine::EngineState& uiEngineState, SampleEditorState& st,
                             engine::SampleData* sd, CommandSink& commandSink) {
    if (event.type != SDL_EVENT_KEY_DOWN) return false;

    SDL_Keycode key = event.key.key;

    // OPT alone exits the modal
    if (key == SDLK_LALT || key == SDLK_RALT || key == SDLK_ESCAPE) {
        return true;
    }

    uint32_t frames = (sd && sd->frames > 0) ? sd->frames : 1;

    if (editHeld) {
        int stepSmall = 16;
        int stepLarge = 256;

        if (key == SDLK_UP || key == SDLK_DOWN) {
            int dir = (key == SDLK_UP) ? 1 : -1;
            int step = dir * stepLarge;

            switch (st.row) {
            case CursorRow::RECORD:
                if (st.subCol == 2) st.recVol = std::clamp(st.recVol + dir * 16, 0, 255);
                else if (st.subCol == 3) st.recArm = std::clamp(st.recArm + dir * 16, 0, 255);
                break;
            case CursorRow::SELECT:
                if (st.subCol == 0) st.selectStart = std::clamp<int32_t>(static_cast<int32_t>(st.selectStart) + step, 0, frames);
                else st.selectEnd = std::clamp<int32_t>(static_cast<int32_t>(st.selectEnd) + step, 0, frames);
                break;
            case CursorRow::LOOP_REGION:
                if (st.subCol == 0) st.loopStart = std::clamp<int32_t>(static_cast<int32_t>(st.loopStart) + step, 0, frames);
                else st.loopEnd = std::clamp<int32_t>(static_cast<int32_t>(st.loopEnd) + step, 0, frames);
                break;
            case CursorRow::SLICE_MARKER:
                if (sd && sd->sliceMarkerCount > 0) {
                    st.currentSliceMarker = std::clamp(st.currentSliceMarker + dir, 0, sd->sliceMarkerCount - 1);
                }
                break;
            case CursorRow::PROCESS:
                if (st.subCol == 0) st.processIndex = std::clamp(st.processIndex + dir * 5, 0, 144);
                break;
            default:
                break;
            }
        }
        else if (key == SDLK_LEFT || key == SDLK_RIGHT) {
            int dir = (key == SDLK_RIGHT) ? 1 : -1;
            int step = dir * stepSmall;

            switch (st.row) {
            case CursorRow::RECORD:
                if (st.subCol == 1) st.recSrc = std::clamp(st.recSrc + dir, 0, 15);
                else if (st.subCol == 2) st.recVol = std::clamp(st.recVol + dir, 0, 255);
                else if (st.subCol == 3) st.recArm = std::clamp(st.recArm + dir, 0, 255);
                else if (st.subCol == 4) st.recSong = std::clamp(st.recSong + dir, 0, 1);
                break;
            case CursorRow::SELECT:
                if (st.subCol == 0) st.selectStart = std::clamp<int32_t>(static_cast<int32_t>(st.selectStart) + step, 0, frames);
                else st.selectEnd = std::clamp<int32_t>(static_cast<int32_t>(st.selectEnd) + step, 0, frames);
                break;
            case CursorRow::LOOP_REGION:
                if (st.subCol == 0) st.loopStart = std::clamp<int32_t>(static_cast<int32_t>(st.loopStart) + step, 0, frames);
                else st.loopEnd = std::clamp<int32_t>(static_cast<int32_t>(st.loopEnd) + step, 0, frames);
                break;
            case CursorRow::SLICE_MARKER:
                if (sd && sd->sliceMarkerCount > 0) {
                    st.currentSliceMarker = std::clamp(st.currentSliceMarker + dir, 0, sd->sliceMarkerCount - 1);
                }
                break;
            case CursorRow::PROCESS:
                if (st.subCol == 0) st.processIndex = std::clamp(st.processIndex + dir, 0, 144);
                break;
            default:
                break;
            }
        }
        else if (key == SDLK_RETURN || key == SDLK_SPACE) {
            if (st.row == CursorRow::RECORD && st.subCol == 0) {
                if (st.isRecording || st.isArmed) {
                    st.stopRecording(sd, commandSink);
                } else {
                    st.startOrArm();
                }
            } else if (st.row == CursorRow::PROCESS) {
                if (st.subCol == 1) { // '>'
                    ExecuteProcess(st, sd, st.processIndex);
                } else if (st.subCol == 2) { // 'UNDO'
                    RestoreUndo(st, sd);
                }
            }
        }
    } else {
        // Normal D-Pad navigation
        if (key == SDLK_UP) {
            int r = static_cast<int>(st.row);
            if (r > 0) {
                st.row = static_cast<CursorRow>(r - 1);
                st.subCol = 0;
            }
        } else if (key == SDLK_DOWN) {
            int r = static_cast<int>(st.row);
            if (r < static_cast<int>(CursorRow::COUNT) - 1) {
                st.row = static_cast<CursorRow>(r + 1);
                st.subCol = 0;
            }
        } else if (key == SDLK_LEFT) {
            if (st.subCol > 0) --st.subCol;
        } else if (key == SDLK_RIGHT) {
            int maxCols = 1;
            if (st.row == CursorRow::RECORD) maxCols = 5;
            else if (st.row == CursorRow::SELECT) maxCols = 2;
            else if (st.row == CursorRow::LOOP_REGION) maxCols = 2;
            else if (st.row == CursorRow::PROCESS) maxCols = 3;
            else if (st.row == CursorRow::SAVE) maxCols = 2;

            if (st.subCol < maxCols - 1) ++st.subCol;
        } else if (key == SDLK_RETURN || key == SDLK_SPACE) {
            if (st.row == CursorRow::RECORD && st.subCol == 0) {
                if (st.isRecording || st.isArmed) {
                    st.stopRecording(sd, commandSink);
                } else {
                    st.startOrArm();
                }
            } else if (st.row == CursorRow::PROCESS) {
                if (st.subCol == 1) {
                    ExecuteProcess(st, sd, st.processIndex);
                } else if (st.subCol == 2) {
                    RestoreUndo(st, sd);
                }
            }
        }
    }

    return false;
}

} // namespace sample_editor
} // namespace ui
} // namespace m8
