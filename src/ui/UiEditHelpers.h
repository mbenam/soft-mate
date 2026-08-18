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

inline m8::engine::FxCmd g_defaultFxCmd = m8::engine::FxCmd::VOL;

inline void ModifyValue(m8::engine::Step& step, int col, int delta, bool largeStep) {
    using namespace m8::engine;
    if (col == 0) {
        // Note
        if (step.note == NOTE_EMPTY) step.note = 60; // C-4
        else {
            int d = largeStep ? delta * 12 : delta;
            step.note = static_cast<uint8_t>(std::clamp(static_cast<int>(step.note) + d, 0, 127));
        }
    } else if (col == 1) {
        // Velocity (0..127)
        int d = largeStep ? delta * 0x10 : delta;
        if (step.vol == VOL_EMPTY) step.vol = 0x64;
        else step.vol = static_cast<uint8_t>(std::clamp(static_cast<int>(step.vol) + d, 0, 127));
    } else if (col == 2) {
        // Instrument (0..127)
        int d = largeStep ? delta * 0x10 : delta;
        if (step.instr == INST_EMPTY) step.instr = 0;
        else step.instr = static_cast<uint8_t>(std::clamp(static_cast<int>(step.instr) + d, 0, 127));
    } else if (col == 4 || col == 6 || col == 8) {
        // FX Value (0..255)
        int d = largeStep ? delta * 0x10 : delta;
        int idx = (col == 4) ? 0 : (col == 6) ? 1 : 2;
        if (step.fx[idx].cmd != FxCmd::NONE) {
            step.fx[idx].val = static_cast<uint8_t>(std::clamp(static_cast<int>(step.fx[idx].val) + d, 0, 255));
        }
    } else if (col == 3 || col == 5 || col == 7) {
        // FX Command
        int idx = (col == 3) ? 0 : (col == 5) ? 1 : 2;
        constexpr int kMaxFx = static_cast<int>(FxCmd::LT2);
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
        step.fx[0] = {g_defaultFxCmd, 0};
    } else if (col == 5 && step.fx[1].cmd == FxCmd::NONE) {
        step.fx[1] = {g_defaultFxCmd, 0};
    } else if (col == 7 && step.fx[2].cmd == FxCmd::NONE) {
        step.fx[2] = {g_defaultFxCmd, 0};
    }
}

inline void ModifyTableValue(m8::engine::TableStep& step, int col, int delta, bool largeStep) {
    using namespace m8::engine;
    if (col == 0) {
        // N Transpose (signed, -128..127)
        int d = largeStep ? delta * 12 : delta;
        step.transp = static_cast<int8_t>(std::clamp(static_cast<int>(step.transp) + d, -128, 127));
    } else if (col == 1) {
        // V Volume (0..127 or VOL_EMPTY)
        int d = largeStep ? delta * 0x10 : delta;
        if (step.vol == VOL_EMPTY) step.vol = 0x64;
        else step.vol = static_cast<uint8_t>(std::clamp(static_cast<int>(step.vol) + d, 0, 127));
    } else if (col == 3 || col == 5 || col == 7) {
        // FX Value (0..255)
        int d = largeStep ? delta * 0x10 : delta;
        int idx = (col == 3) ? 0 : (col == 5) ? 1 : 2;
        if (step.fx[idx].cmd != FxCmd::NONE) {
            step.fx[idx].val = static_cast<uint8_t>(std::clamp(static_cast<int>(step.fx[idx].val) + d, 0, 255));
        }
    } else if (col == 2 || col == 4 || col == 6) {
        // FX Command
        int idx = (col == 2) ? 0 : (col == 4) ? 1 : 2;
        constexpr int kMaxFx = static_cast<int>(FxCmd::LT2);
        int cmd = static_cast<int>(step.fx[idx].cmd);
        if (cmd > kMaxFx) cmd = 0;
        cmd += delta;
        if (cmd < 0) cmd = kMaxFx;
        if (cmd > kMaxFx) cmd = 0;
        step.fx[idx].cmd = static_cast<FxCmd>(cmd);
    }
}

inline void InsertTableDefault(m8::engine::TableStep& step, int col) {
    using namespace m8::engine;
    if (col == 0) {
        // N: set 00
        step.transp = 0;
    } else if (col == 1 && step.vol == VOL_EMPTY) {
        step.vol = 0x64;
    } else if (col == 2 && step.fx[0].cmd == FxCmd::NONE) {
        step.fx[0] = {g_defaultFxCmd, 0};
    } else if (col == 4 && step.fx[1].cmd == FxCmd::NONE) {
        step.fx[1] = {g_defaultFxCmd, 0};
    } else if (col == 6 && step.fx[2].cmd == FxCmd::NONE) {
        step.fx[2] = {g_defaultFxCmd, 0};
    }
}

inline void DeleteTableValue(m8::engine::TableStep& step, int col) {
    using namespace m8::engine;
    if (col == 0) {
        step.transp = 0;
    } else if (col == 1) {
        step.vol = VOL_EMPTY;
    } else if (col == 2 || col == 3) {
        step.fx[0] = {};
    } else if (col == 4 || col == 5) {
        step.fx[1] = {};
    } else if (col == 6 || col == 7) {
        step.fx[2] = {};
    }
}

} // namespace m8::ui
