#pragma once
#include "../engine/SeqTypes.h"
#include <cstdint>
#include <algorithm>

// Small value-editing helpers shared by several screens (CODE_CLEANUP_SPEC.md
// #1) -- moved out of main.cpp verbatim, no behavior change.

namespace m8::ui {

inline uint8_t AdjustU8(uint8_t val, int delta, int minVal, int maxVal, uint8_t emptyVal) {
    if (val == emptyVal) {
        return (delta > 0) ? minVal : maxVal;
    }
    int newVal = val + delta;
    if (newVal < minVal) newVal = minVal;
    if (newVal > maxVal) newVal = maxVal;
    return newVal;
}

inline int8_t AdjustS8(int8_t val, int delta, int minVal, int maxVal, int8_t emptyVal) {
    if (val == emptyVal) {
        return (delta > 0) ? minVal : maxVal;
    }
    int newVal = val + delta;
    if (newVal < minVal) newVal = minVal;
    if (newVal > maxVal) newVal = maxVal;
    return newVal;
}

inline void ModifyValue(m8::engine::Step& step, int col, int delta, bool largeStep) {
    using namespace m8::engine;
    if (col == 0) {
        if (step.note == NOTE_EMPTY) {
            step.note = 60; // C-4
            if (step.vol == VOL_EMPTY) step.vol = 0x64;
            if (step.instr == INST_EMPTY) step.instr = 0;
        } else {
            int midi = step.note;
            midi += (largeStep ? delta * 12 : delta);
            if (midi < 0) midi = 0;
            if (midi > 127) midi = 127;
            step.note = midi;
        }
    } else if (col == 1) {
        int d = largeStep ? delta * 0x10 : delta;
        if (step.vol == VOL_EMPTY) step.vol = 0x64;
        else step.vol = std::clamp((int)step.vol + d, 0, 127);
    } else if (col == 2) {
        int d = largeStep ? delta * 0x10 : delta;
        if (step.instr == INST_EMPTY) step.instr = 0;
        else step.instr = std::clamp((int)step.instr + d, 0, 127);
    } else if (col == 4 || col == 6 || col == 8) {
        int d = largeStep ? delta * 0x10 : delta;
        int idx = (col == 4) ? 0 : (col == 6) ? 1 : 2;
        if (step.fx[idx].cmd != FxCmd::NONE) {
            step.fx[idx].val = std::clamp((int)step.fx[idx].val + d, 0, 255);
        }
    } else if (col == 3 || col == 5 || col == 7) {
        int idx = (col == 3) ? 0 : (col == 5) ? 1 : 2;
        // Cycle through the modeled FX commands NONE(0)..TIC(9). UNKNOWN(0xFE) is a
        // load/save passthrough and is not authorable — editing such a slot snaps it
        // into the modeled range.
        constexpr int kMaxFx = static_cast<int>(FxCmd::TIC); // 9
        int cmd = static_cast<int>(step.fx[idx].cmd);
        if (cmd > kMaxFx) cmd = 0;
        cmd += delta;
        if (cmd < 0) cmd = kMaxFx;
        if (cmd > kMaxFx) cmd = 0;
        step.fx[idx].cmd = static_cast<FxCmd>(cmd);
    }
}

inline void InsertDefault(m8::engine::Step& step, int col) {
    using namespace m8::engine;
    if (col == 0 && step.note == NOTE_EMPTY) {
        step.note = 60; // C-4
        if (step.vol == VOL_EMPTY) step.vol = 0x64;
        if (step.instr == INST_EMPTY) step.instr = 0;
    } else if (col == 1 && step.vol == VOL_EMPTY) {
        step.vol = 0x64;
    } else if (col == 2 && step.instr == INST_EMPTY) {
        step.instr = 0;
    } else if (col == 3 && step.fx[0].cmd == FxCmd::NONE) {
        step.fx[0] = {FxCmd::VOL, 0};
    } else if (col == 5 && step.fx[1].cmd == FxCmd::NONE) {
        step.fx[1] = {FxCmd::VOL, 0};
    } else if (col == 7 && step.fx[2].cmd == FxCmd::NONE) {
        step.fx[2] = {FxCmd::VOL, 0};
    }
}

} // namespace m8::ui
