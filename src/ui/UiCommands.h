#pragma once
#include "../engine/CommandRing.h"
#include "../engine/EngineStateUpdater.h"

// Shared by main.cpp and every screen's HandleInput function (CODE_CLEANUP_SPEC.md #1).
// CommandSink used to be a struct local to main(); moved here so per-screen input
// handlers extracted out of main()'s big if/else chain can push commands too.

namespace m8::ui {

struct CommandSink {
    m8::engine::CommandRing<m8::engine::EngineCommand, 1024>& ring;
    uint32_t dropped = 0;
    bool send(const m8::engine::EngineCommand& cmd) {
        if (ring.push(cmd)) return true;
        ++dropped;
        return false;
    }
};

// Pushes an UPDATE_PARAM command and applies it to the UI mirror immediately
// (same pattern main.cpp always used: the command goes to the engine, and the
// mirror is updated locally so the screen redraws with the new value this frame
// without waiting a round trip through the command ring).
inline void PushParam(CommandSink& sink, m8::engine::EngineState& uiEngineState,
                       m8::engine::ParamID id, int val, int target = 0, int row = 0,
                       float fVal = 0.0f) {
    m8::engine::EngineCommand cmd;
    cmd.type = m8::engine::CommandType::UPDATE_PARAM;
    cmd.paramId = id;
    cmd.targetId = target;
    cmd.row = row;
    cmd.value = val;
    cmd.fValue = fVal;
    sink.send(cmd);
    m8::engine::EngineStateUpdater::applyParameterUpdate(uiEngineState, cmd);
}

} // namespace m8::ui
