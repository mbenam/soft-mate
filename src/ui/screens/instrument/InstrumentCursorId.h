#pragma once
#include <cstdint>

namespace m8 {
namespace ui {
namespace instrument {

// Shared between the Sampler and Macrosyn layouts -- most fields are common
// to both instrument types, so one enum covers the union of both variants'
// field sets rather than having two incompatible cursor types per screen.
enum class CursorId : uint8_t {
    NONE = 0,
    TYPE, CMD_LOAD, CMD_SAVE, NAME, TRANSP, TBL_TIC, EQ,
    FILTER, CUTOFF, RES, AMP, LIM, PAN, DRY, CHO, DEL, REV, DEGRADE,
    // Sampler-only
    SAMPLE_LOAD, SAMPLE_REC, SLICE, PLAY, START, LOOP_ST, LENGTH, DETUNE,
    // Macrosyn-only
    SHAPE, TIMBRE, COLOR, REDUX,
    // FMSynth-only
    FM_ALGO,
    FM_OP_A_SHAPE, FM_OP_B_SHAPE, FM_OP_C_SHAPE, FM_OP_D_SHAPE,
    FM_OP_A_RATIO, FM_OP_B_RATIO, FM_OP_C_RATIO, FM_OP_D_RATIO,
    FM_OP_A_LEV,   FM_OP_B_LEV,   FM_OP_C_LEV,   FM_OP_D_LEV,
    FM_OP_A_FB,    FM_OP_B_FB,    FM_OP_C_FB,    FM_OP_D_FB,
    FM_OP_A_MOD1,  FM_OP_B_MOD1,  FM_OP_C_MOD1,  FM_OP_D_MOD1,
    FM_OP_A_MOD2,  FM_OP_B_MOD2,  FM_OP_C_MOD2,  FM_OP_D_MOD2,
    FM_MOD1, FM_MOD2, FM_MOD3, FM_MOD4,
};

} // namespace instrument
} // namespace ui
} // namespace m8
